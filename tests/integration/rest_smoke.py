"""Run four actual services; exercise REST, processing, validation, and bounded failures."""

import json
import queue
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
import urllib.parse
from concurrent.futures import ThreadPoolExecutor


class Process:
    def __init__(self, binary, *arguments):
        self.errors = tempfile.TemporaryFile(mode="w+")
        self.process = subprocess.Popen(
            [binary, *arguments], stdout=subprocess.PIPE, stderr=self.errors, text=True
        )
        lines = queue.Queue()

        def drain():
            for line in self.process.stdout:
                lines.put(line)

        self.reader = threading.Thread(target=drain, daemon=True)
        self.reader.start()
        try:
            until = time.monotonic() + 8
            while time.monotonic() < until:
                line = lines.get(timeout=max(0.01, until - time.monotonic()))
                if "bound port: " in line:
                    self.port = int(line.rsplit(":", 1)[1])
                    assert 0 < self.port <= 65535, line
                    return
            raise AssertionError("Service did not report its port")
        except BaseException:
            self.stop()
            raise

    @property
    def target(self):
        return f"127.0.0.1:{self.port}"

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)
                raise AssertionError("Service exceeded shutdown budget") from None
        self.reader.join(timeout=1)
        self.errors.seek(0)
        errors = self.errors.read()
        assert self.process.returncode == 0, errors


def main():
    state_binary, worker_binary, router_binary, gateway_binary = sys.argv[1:]
    processes = []

    def start(binary, *args):
        process = Process(binary, *args)
        processes.append(process)
        return process

    try:
        state = start(state_binary, "127.0.0.1:0")
        worker = start(worker_binary, "127.0.0.1:0", state.target)
        router = start(router_binary, "127.0.0.1:0", state.target, worker.target)
        gateway = start(gateway_binary, "0", router.target, state.target)
        base = f"http://{gateway.target}"

        def request(path, body=None, key=None, raw=None, headers=None):
            request_headers = {"X-Correlation-Id": "rest-smoke-correlation"}
            if key is not None:
                request_headers["Idempotency-Key"] = key
            if body is not None:
                raw = json.dumps(body, ensure_ascii=False).encode()
                request_headers["Content-Type"] = "application/json"
            request_headers.update(headers or {})
            call = urllib.request.Request(base + path, data=raw, headers=request_headers)
            try:
                response = urllib.request.urlopen(call, timeout=4)
            except urllib.error.HTTPError as error:
                response = error
            with response:
                data = response.read()
                try:
                    decoded = json.loads(data)
                except (json.JSONDecodeError, UnicodeDecodeError):
                    decoded = None
                return response.status, response.headers, decoded

        until = time.monotonic() + 5
        while True:
            try:
                ready = request("/health/ready")
            except urllib.error.URLError:
                assert time.monotonic() < until, "Gateway did not start accepting connections"
                time.sleep(0.025)
                continue
            if ready[0] == 200:
                break
            assert time.monotonic() < until, ready
            time.sleep(0.025)
        assert request("/health/live")[0] == 200

        numeric = {"type": "TASK_TYPE_NUMERIC", "executionTimeout": "2s",
                   "numeric": {"operation": "OPERATION_MONTE_CARLO_PI", "iterations": "10000", "seed": "42"}}
        transform = {"type": "TASK_TYPE_TRANSFORM", "executionTimeout": "2s",
                     "transform": {"operation": "OPERATION_REVERSE_CODEPOINTS", "data": "a\u00e9\U0001f30d"}}
        network = {"type": "TASK_TYPE_NETWORK_SIMULATION", "executionTimeout": "2s",
                   "networkSimulation": {"packetCount": 20, "packetSizeBytes": 32,
                                         "seed": "5", "lossPartsPerMillion": 0,
                                         "syntheticDelay": "0.001s"}}
        tasks = []
        for index, spec in enumerate([numeric, transform, network]):
            status, headers, task = request("/api/v1/tasks", spec, f"task-{index}")
            assert status == 202, (status, task)
            assert headers["Location"] == "/api/v1/tasks/" + task["id"]
            assert headers["X-Correlation-Id"] == "rest-smoke-correlation"
            tasks.append(task["id"])

        def completed(task_id):
            until = time.monotonic() + 5
            while time.monotonic() < until:
                status, _, task = request("/api/v1/tasks/" + task_id)
                assert status == 200, task
                if task["state"] in ["TASK_STATE_COMPLETED", "TASK_STATE_FAILED"]:
                    assert task["state"] == "TASK_STATE_COMPLETED", task
                    assert task["attemptCount"] == 1, task
                    assert "token" not in json.dumps(task).lower()
                    return task
                time.sleep(0.01)
            raise AssertionError("Task did not complete within test budget")

        outputs = [completed(task_id) for task_id in tasks]
        assert 3.0 < outputs[0]["result"]["numeric"]["value"] < 3.3
        assert outputs[1]["result"]["transform"]["data"] == "\U0001f30d\u00e9a"
        assert outputs[2]["result"]["networkSimulation"]["deliveredBytes"] == "640"
        assert outputs[2]["result"]["networkSimulation"]["simulatedDuration"] == "0.020s"

        status, headers, replay = request("/api/v1/tasks", numeric, "task-0")
        assert status == 202 and replay["id"] == tasks[0]
        assert headers["X-Idempotency-Replayed"] == "true"
        assert request("/api/v1/tasks", transform, "task-0")[0] == 409
        assert request("/api/v1/tasks", numeric)[0] == 400
        assert request("/api/v1/tasks", {**numeric, "typo": 1}, "unknown")[0] == 400
        assert request("/api/v1/tasks", raw=b"{", headers={"Content-Type": "application/json"})[0] == 400
        assert request("/api/v1/tasks", raw=b"{}", headers={"Content-Type": "text/plain"})[0] == 415
        assert request("/api/v1/tasks", raw=b"x" * 65537, headers={"Content-Type": "application/json"})[0] == 413
        assert request("/api/v1/tasks/not-found")[0] == 404
        assert request("/api/v1/tasks?page_size=101")[0] == 400
        assert request("/api/v1/tasks?page_size=-1")[0] == 400
        assert request("/api/v1/tasks?cursor=bogus")[0] == 400
        status, _, page = request("/api/v1/tasks?page_size=1")
        assert status == 200 and len(page["tasks"]) == 1
        cursor = urllib.parse.quote(page["nextCursor"], safe="")
        status, _, second = request("/api/v1/tasks?page_size=1&cursor=" + cursor)
        assert status == 200 and second["tasks"][0]["id"] != page["tasks"][0]["id"]

        # Concurrent same-key submissions must identify a single retained task.
        with ThreadPoolExecutor(max_workers=6) as pool:
            responses = list(pool.map(lambda _: request("/api/v1/tasks", numeric, "parallel"), range(12)))
        assert all(response[0] == 202 for response in responses), responses
        assert len({response[2]["id"] for response in responses}) == 1
        completed(responses[0][2]["id"])

        # Fail closed on storage loss while the HTTP event loop stays live.
        state.stop()
        started = time.monotonic()
        assert request("/api/v1/tasks/" + tasks[0])[0] in [503, 504]
        assert time.monotonic() - started < 2
        assert request("/health/ready")[0] == 503
        assert request("/health/live")[0] == 200
        assert request("/api/v1/tasks", numeric, "storage-down")[0] in [503, 504]
        print("PASS: REST processing, three workloads, replay/conflict, validation, pagination, concurrent deduplication, storage loss, graceful shutdown")
    finally:
        failures = []
        for process in reversed(processes):
            try:
                process.stop()
            except Exception as error:
                failures.append(str(error))
        if failures:
            raise AssertionError("; ".join(failures))


if __name__ == "__main__":
    main()
