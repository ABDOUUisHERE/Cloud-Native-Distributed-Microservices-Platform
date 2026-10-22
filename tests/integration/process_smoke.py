"""Exercise the actual worker and router binaries with bounded process lifetimes."""

import queue
import subprocess
import sys
import threading


def main():
    worker_binary, router_binary = sys.argv[1:]
    worker = subprocess.Popen(
        [worker_binary, "127.0.0.1:0"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    output = queue.Queue()

    def read_startup():
        output.put(worker.stdout.readline())

    reader = threading.Thread(target=read_startup, daemon=True)
    reader.start()
    try:
        line = output.get(timeout=5)
        assert line.startswith("gRPC bound port: "), line
        port = int(line.rsplit(":", 1)[1])
        assert 0 < port <= 65535
        result = subprocess.run(
            [router_binary, "--probe", f"127.0.0.1:{port}"],
            capture_output=True, text=True, timeout=5, check=True,
        )
        assert result.stdout.strip() == "Worker health: SERVING"
    finally:
        worker.terminate()
        try:
            _, errors = worker.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            worker.kill()
            worker.communicate(timeout=2)
            raise AssertionError("Worker did not stop within its shutdown budget") from None
        reader.join(timeout=1)
        assert worker.returncode == 0, errors

    # A stopped worker must fail promptly rather than hanging or reporting success.
    stopped = subprocess.run(
        [router_binary, "--probe", f"127.0.0.1:{port}"],
        capture_output=True, text=True, timeout=3,
    )
    assert stopped.returncode == 1, stopped
    assert "Worker health probe failed" in stopped.stderr
    print("PASS: real worker/router exchange, graceful exit, and bounded unavailable-worker probe")


if __name__ == "__main__":
    main()
