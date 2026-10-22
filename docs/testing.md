# Testing

## Supported profile

Use Ubuntu 24.04 amd64 with the exact library package revisions in `scripts/dependencies-ubuntu2404.lock`. The default path uses system packages; the optional vcpkg source profile is not validated. The phase 1 GitHub Actions run and merged-main run succeeded. Phase 2 local validation does not imply a published phase 2 workflow run.

```sh
bash scripts/bootstrap-ubuntu.sh --install-tools
cmake --preset linux-analysis
cmake --build --preset linux-analysis
ctest --preset linux-analysis --output-junit build/analysis/ctest.xml
python3 scripts/check-format.py
```

For source on a WSL Windows mount, use a Linux build directory:

```sh
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cmake --preset linux-analysis -B "$HOME/.cache/taskrelay/build/phase2-debug"
cmake --build "$HOME/.cache/taskrelay/build/phase2-debug" --parallel 2
ctest --test-dir "$HOME/.cache/taskrelay/build/phase2-debug" --output-on-failure \
  --output-junit "$HOME/.cache/taskrelay/build/phase2-debug/ctest.xml"
```

The directory name does not select build options: the `linux-analysis` preset enables Debug compilation, warnings as errors, and clang-tidy 18. Build-time Protobuf generation uses the matching system compiler/runtime. Generated bindings are excluded from project lint rules.

## Coverage

| Suite | Boundary exercised |
| --- | --- |
| Foundation gRPC | Real health RPCs, NOT_SERVING and unknown service, generated base service UNIMPLEMENTED, warmed in-flight parent deadline, expired budgets, two-server round-robin, invalid budgets |
| Foundation process smoke | Actual worker listener and one-shot router probe; stopped worker and bounded graceful exit |
| Workloads and validation | Bounds and type mismatch, invalid UTF-8, seeded numeric output, scalar-codepoint reversal, ASCII uppercase, local packet accounting/model delay, cooperative cancellation, typed fingerprints |
| Memory store | Key replay/conflict, ownership and completion replay, expired/stale leases, renewal, filter-bound pagination, capacity/retention, concurrent same-key submissions |
| Bounded executor | Overflow rejection, draining accepted work, stopped admission, invalid limits |
| Task pipeline | Real State/Worker/Router gRPC servers and background dispatcher; all three workloads, forged leases, rejection of an already completed execution token |
| REST process smoke | Four actual processes, all three task results, correlation/Location headers, same-key replay/conflict, input/media/body limits, pagination, concurrent deduplication, storage outage, live/ready, graceful shutdown |

Integration tests bind ephemeral loopback ports and use bounded startup, RPC, polling, and shutdown budgets. The REST test uses Python's standard library and cleans up only the processes it starts. It stops State Service deliberately; queries/submissions then fail within their budgets while `/health/live` remains responsive and `/health/ready` returns 503.

A passing test of the generated base service's UNIMPLEMENTED default does not mean the phase 2 worker is unimplemented. Actual business execution is covered separately by the pipeline and REST suites.

## Current limits

The build/test CI job uses a digest-pinned Ubuntu 24.04 job container. This keeps the runner's preinstalled third-party PostgreSQL packages outside the locked library profile: a newer PGDG `libpq5` otherwise conflicts with Ubuntu's exact `libpq-dev` dependency. The container uses the hosted runner's Azure Ubuntu mirror, retaining Ubuntu signature/hash verification, with bounded initial package-download retries and a five-minute provisioning timeout. Git and CA certificates are installed before checkout; the existing bootstrap installs and verifies the application dependencies. This container is a CI environment, not an application image or a Compose deployment. See [GitHub's job-container documentation](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/run-jobs-in-a-container).

No Redis, Docker, Kind, recovery/retry, telemetry, distribution-metric, or performance acceptance test has run for phase 2. Memory expiry tests inject time; they do not establish Redis atomicity or crash durability. The router dispatches one task at a time, so worker pool concurrency/capacity is tested at the primitive boundary rather than claimed as measured platform throughput. Internal gRPC health currently means local registration, not dependency readiness.

No benchmark numbers are reported. Record real environment details, raw samples, and configurations before publishing any throughput or latency comparison in phase 8.

## Phase 2 local verification - 2026-10-22

Verified the locked Ubuntu 24.04 amd64 profile in WSL with GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, and Clang tooling 18.1.3. The bootstrap installation/verification path succeeded with all locked package revisions, including Drogon 1.8.7, Trantor 1.5.12, and JsonCpp 1.9.5.

```sh
bash scripts/bootstrap-ubuntu.sh --install-tools
cmake --preset linux-analysis -B "$HOME/.cache/taskrelay/build/phase2-debug"
cmake --build "$HOME/.cache/taskrelay/build/phase2-debug" --parallel 4
ctest --test-dir "$HOME/.cache/taskrelay/build/phase2-debug" --output-on-failure \
  --output-junit "$HOME/.cache/taskrelay/build/phase2-debug/ctest.xml"
python3 scripts/check-format.py
```

Observed: all four services compiled with warnings as errors and project clang-tidy checks; all 29 CTest cases passed (27 GoogleTest cases and two real-process Python tests). Formatting passed for 36 project C++/Protobuf files. The final suite includes an expired owned lease rejecting a late completion. REST validation covers successful results for all three workloads, correlation/Location headers, retained-key replay/conflict, request/media limits, pagination, concurrent same-key submissions, State Service loss, ready/live behavior, and graceful shutdown of the actual processes.

Python syntax, JSON manifests, local documentation links, package revisions, and Git whitespace were also checked. The independent Core5G working tree remained clean. GitHub Actions for phase 2 has not run: the workflow and its JUnit artifact are prepared locally for publication after authorization. No optional vcpkg, Redis, deployment, telemetry, sanitizer, or benchmark result is claimed by this verification.
