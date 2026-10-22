# Development plan and GitHub workflow

Status: phases 0 and 1 merged through PRs #1 and #2. Phase 1 checks passed on its branch and on merged main. Phase 2 is implemented and locally verified on `feat/task-processing`. Its publication and merge still require authorization. The project uses English throughout.

## Environment inspection

Inspection performed on 2026-10-21. The original workspace is the independent Core5G repository at `ABDOUUisHERE/5G-Core-Network-Simulator`; its working tree was clean. TaskRelay lives in the sibling `taskrelay` directory with separate Git metadata.

| Tool | Observed status |
| --- | --- |
| Git | 2.44.0.windows.1 |
| Python | 3.14.2 |
| Ubuntu WSL compiler | GCC 13.3.0 |
| Docker CLI | 29.1.2 |
| Docker Compose | 2.40.3-desktop.1 |
| Docker engine | Unverified: version query did not respond and was stopped |
| kubectl | 1.34.1; Kustomize 5.7.1 |
| CMake, Ninja, protoc, clang++, clang-format, clang-tidy, Kind | Unavailable in checked Windows PATH and Ubuntu WSL |
| gh, vcpkg, Conan | Unavailable in checked Windows PATH |

These are the phase 0 observations. Phase 1 installed CMake 3.28.3, Ninja 1.11.1, Clang tooling 18.1.3, and supporting build tools in Ubuntu WSL. The default profile uses locked Ubuntu library packages. A pinned vcpkg checkout was also bootstrapped, but its complete source build was stopped during gRPC compilation; that optional runtime profile remains unverified. Phase 1 does not depend on Docker; engine and Kind availability remain outside the current verification.

## Git and review workflow

1. Bootstrap the independent local `main` branch with `.gitignore` only.
2. Create `docs/architecture` from that commit and commit the phase 0 documents there. This provides a reviewable branch diff before any remote publication.
3. Present the local result, verification, and exact next-phase file list for approval.
4. Before publication, confirm explicit authorization to create the GitHub repository and push the concrete commits. Ask for repository visibility if it has not been specified. Never publish to Core5G's remote.
5. After authorization, publish the bootstrap main and phase branch to the selected new repository, then open a draft PR with scope, validation, risks, and requirement references. Creation of a GitHub repository is not part of local phase 0 approval.
6. For subsequent approved phases, branch from the latest reviewed `main`, keep commits focused, run relevant checks, and open/update the phase PR after publication is authorized.
7. Merge only with user authorization and required checks satisfied. Phase completion alone does not authorize a merge or publication.

Use English for all documentation, source code, comments, user-facing content, commit messages, and PR descriptions. Use conventional commit subjects such as `docs(architecture): define task lifecycle`, `build(cmake): add reproducible C++ toolchain`, or `feat(state): enforce lease fencing`. Commit titles describe changes actually present. PR descriptions explain resulting behavior, verification, and material limitations. Link issue numbers only after real issues exist.

The public repository is [ABDOUUisHERE/Cloud-Native-Distributed-Microservices-Platform](https://github.com/ABDOUUisHERE/Cloud-Native-Distributed-Microservices-Platform). Phase 0 was merged through [PR #1](https://github.com/ABDOUUisHERE/Cloud-Native-Distributed-Microservices-Platform/pull/1). Issues and milestones can track later phases once authorized. A local phase 1 build is not evidence of a successful GitHub Actions run, deployment, or benchmark.

## Phases and review gates

Paths below are the planned scope; before each phase, present the actual file list, including amendments required by implementation findings. Documentation and CI evolve alongside code rather than waiting until phase 9.

| Phase / branch | Files and directories planned | Exit evidence |
| --- | --- | --- |
| 0 — `docs/architecture` | `README.md`, five design documents, `.github/pull_request_template.md`, `.gitignore` | Consistent contracts, state model, requirements, and reviewed local diff |
| 1 — `build/cpp-grpc-foundation` | Root/service/common CMake files, `CMakePresets.json`, `vcpkg.json`, versioned `proto/`, first router/worker health exchange, foundation tests, `.clang-format`, `.clang-tidy`, `scripts/`, initial `.github/workflows/ci.yml`, README updates | Pinned dependencies resolve; clean Linux build and real deadline-bounded gRPC exchange pass |
| 2 — `feat/task-processing` | `services/`, `common/`, `tests/unit/`, REST/gRPC integration tests, contract and requirements updates | Three deterministic bounded workloads complete through REST with in-memory State Service adapter |
| 3 — `feat/redis-coordination` | Redis-backed State Service, `redis/scripts/`, bounded executor integration, Redis tests, architecture updates | Atomic submission/leases/transitions, concurrent deduplication, stale-write rejection, and restart recovery pass |
| 4 — `feat/resilience` | Resilience primitives, service lifecycle integration, `tests/resilience/`, resilience/testing docs | Bounded retries, circuit breakers, timeout ambiguity, shutdown, and recovery scenarios pass |
| 5 — `build/docker-compose` | `deploy/docker/`, `docker-compose.yml`, `.dockerignore`, integration scripts, README/testing docs | Nonroot multi-stage images and real Compose run with two workers pass |
| 6 — `infra/kubernetes-kind` | `deploy/kubernetes/`, Kind/deploy scripts, `docs/kubernetes.md`, manifest validation CI | Real Kind rollout, probes, DNS membership refresh, manual scaling, and restoration pass |
| 7 — `feat/observability` | Telemetry helpers, service instrumentation, `monitoring/`, Compose/Kind telemetry configuration, `docs/observability.md` | Prometheus scrapes, useful Grafana panels, and cross-service Jaeger traces verified |
| 8 — `test/load-and-failures` | `tests/performance/`, expanded resilience scenarios, evidence artifacts, `docs/benchmark.md`, HPA only with validated metrics | Raw throughput/quantile/resource measurements at different replica counts and failure evidence |
| 9 — `ci/release-validation` | Complete workflows, dependency checks, documentation updates, `docs/demo.md` | Reproducible clean deployment and all requirement gates backed by real checks |

Compose phase 5 starts the observability containers required by the specification. Their complete instrumentation, dashboard, and cross-service trace acceptance gate belongs to phase 7. Phase 2's memory adapter is explicitly volatile; it does not satisfy the Redis or crash-durability criteria.

## Phase 1 scope

The approved initial file set is:

```text
CMakeLists.txt
CMakePresets.json
vcpkg.json
.clang-format
.clang-tidy
proto/CMakeLists.txt
proto/taskrelay/v1/task.proto
proto/taskrelay/v1/router.proto
proto/taskrelay/v1/worker.proto
proto/taskrelay/v1/state.proto
common/CMakeLists.txt
common/grpc/deadline.h
services/task-router/CMakeLists.txt
services/task-router/main.cc
services/processing-worker/CMakeLists.txt
services/processing-worker/main.cc
tests/CMakeLists.txt
tests/integration/grpc_foundation_test.cc
scripts/bootstrap-wsl.sh
.github/workflows/ci.yml
README.md                         (update)
docs/development-plan.md         (update)
docs/grpc-contracts.md           (update with actual field definitions)
```

The implementation also supplies `common/grpc/health_client.h` and `.cc` to share the real probe between the router and tests, `tests/integration/process_smoke.py` for executable-level validation, `scripts/check-format.py`, `scripts/bootstrap-ubuntu.sh` and `scripts/dependencies-ubuntu2404.lock` for the default package profile, a release-only optional vcpkg triplet under `cmake/triplets/`, `.gitattributes` to preserve LF on Windows, and the unmodified standard gRPC health schema with upstream license attribution. These support the approved foundation scope.

The first worker exchange is a bounded foundation smoke test; complete scheduling, Redis, REST, tracing, and task execution remain later work. The router is a one-shot health client in this phase. The worker advertises foundation health, while business readiness stays negative and ExecuteTask returns UNIMPLEMENTED.

Default and CI dependencies are locked Ubuntu 24.04 packages: gRPC 1.51.1, Protobuf 3.21.12, and GoogleTest/GoogleMock 1.14.0. The complete vcpkg source build proved too costly for routine foundation checks, so the package profile became the validation target. Its lock fixes the application library package revisions, not the entire OS image.

Optional source baseline and vcpkg checkout: `67972e92edd181d433927b42d8ab4bfa52ac164a`. Its direct ports are gRPC 1.81.1, Protobuf 7.36.2, and GoogleTest 1.18.0. All transitive port versions come from the same baseline. Protobuf and its imports compiled, but the full gRPC build and runtime checks did not complete. The upstream standard health schema is vendored from gRPC 1.81.1 and is wire-compatible with the default profile's Check/Watch RPCs. GitHub Actions references use full commit SHAs.

## Validation strategy

Phase 0 verification covered Markdown links and formatting, the eight-file scope, Git whitespace, and semantic consistency between contracts and state transitions. That documentation-only phase had no runtime implementation to test.

For implementation phases, use GoogleTest and GoogleMock for logic and injected dependency failures; use real Redis/gRPC/REST integration tests for transactional boundaries. Include tests for lost acknowledgements, lease expiry, duplicate submissions, stale results, overload, and concurrency. Use sanitizers and thread checking where supported by the selected Linux toolchain.

CI grows to include formatting, clang-tidy/static analysis, CMake build, unit tests, integration tests, Docker builds, Kubernetes schema validation, dependency checks, and retained evidence. Integration tests use bounded waits and clean up only their own resources.

Benchmark reports record machine/runner details, compiler/build mode, image/dependency revisions, configuration, worker counts, warmup, sample count, and observability settings. Store raw observations; compute p50/p95/p99 and throughput from real measurements. Current benchmark and deployment status is **not measured / not executed**.

## Phase 1 local verification — 2026-10-22

The locked Ubuntu 24.04 amd64 profile passed a clean configure and build with GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, and clang-tidy/clang-format 18.1.3. The verified runtime/generator packages are gRPC 1.51.1, Protobuf 3.21.12, and GoogleTest/GoogleMock 1.14.0, with exact Debian revisions in the package lock.

Commands executed from a Linux shell, using a Linux filesystem build directory for the Windows-hosted source:

```sh
bash scripts/bootstrap-ubuntu.sh --install-tools
cmake --preset linux-analysis -B "$HOME/.cache/taskrelay/build/verified-analysis"
cmake --build "$HOME/.cache/taskrelay/build/verified-analysis" --parallel 2
ctest --test-dir "$HOME/.cache/taskrelay/build/verified-analysis" --output-on-failure \
  --output-junit "$HOME/.cache/taskrelay/build/verified-analysis/ctest.xml"
python3 scripts/check-format.py
```

Observed results: all three C++ executables and their generated/shared libraries built; project sources passed clang-tidy and compiler warnings treated as errors; all nine CTest cases passed; formatting passed for ten project C++/Protobuf files. Cases cover real gRPC health success/failure, unknown services, explicit unimplemented task execution, an in-flight parent deadline, expired budgets, round-robin selection across two real gRPC servers, invalid budget rejection, and actual worker/router process startup and shutdown.

Both bootstrap scripts passed Bash syntax checks; the Ubuntu bootstrap verified all locked package revisions and its installation path completed successfully. Local Markdown links, preset JSON, Python tooling syntax, Git whitespace, action SHA pinning, and the unchanged vendored health-schema hash were also checked.

The optional vcpkg build resolved its pinned dependencies and built Protobuf, but was stopped during gRPC compilation. Its full build/runtime profile is unverified. At this local verification point GitHub Actions had not run. The subsequent PR #2 and merged-main checks both succeeded. REST, task processing, Redis, container/cluster deployment, telemetry, and benchmarks remain outside this phase's verified behavior.

## Phase 2 scope and implementation

Approved files cover gateway routes and entry point; State Service memory store, RPC adapter, and entry point; router submission and dispatcher; worker service and processors; shared validation, state client, and bounded executor; CMake/dependency/CI updates; unit, gRPC, and real-process REST tests; and affected documentation. The shared state client also hosts signal and gRPC server lifecycle helpers to avoid copying them into three services. Supporting additions are `tests/unit/bounded_executor_test.cc` and this phase's `docs/testing.md`.

The implementation uses Ubuntu Drogon 1.8.7, Trantor 1.5.12, and JsonCpp 1.9.5. Ubuntu's Drogon CMake export requires development packages for its optional compiled-in database/Redis/YAML integrations; these are locked even though application processing uses none of those backends. A scoped CMP0153 compatibility policy handles its old FindJsoncpp module. OpenSSL supplies UUID randomness; the base security stack remains under Ubuntu updates.

The README now uses the user's requested full project title and canonical renamed public repository URL; the internal `taskrelay` package and executable names remain consistent with the checked-in contracts. The complete optional vcpkg source profile remains unverified.

The single-dispatch loop and volatile memory adapter deliberately establish a working task path first. Redis coordination, durable recovery, retry state transitions, circuit breaking, distribution metrics, and deployment verification remain their own phases. See [the phase 2 boundary](grpc-contracts.md#phase-2-implementation-boundary).

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
