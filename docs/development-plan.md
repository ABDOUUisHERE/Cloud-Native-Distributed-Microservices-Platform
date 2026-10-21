# Development plan and GitHub workflow

Status: phase 0 documentation. Only this phase's local files and commits are authorized. Later phase starts and remote publication require the user's explicit agreement.

## Environment inspection

Inspection performed on 2026-10-21. The original workspace is the independent Core5G repository at `ABDOUUisHERE/5G-Core-Network-Simulator`; its working tree was clean. NexusMesh lives in the sibling `nexusmesh` directory with separate Git metadata.

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

Phase 1 must provision or select the missing Linux toolchain and confirm Docker readiness before relying on containers. Native WSL builds remain an alternative if Docker is unavailable. No installations have been performed during phase 0.

## Git and review workflow

1. Bootstrap the independent local `main` branch with `.gitignore` only.
2. Create `docs/architecture` from that commit and commit the phase 0 documents there. This provides a reviewable branch diff before any remote publication.
3. Present the local result, verification, and exact next-phase file list for approval.
4. Before publication, confirm explicit authorization to create the GitHub repository and push the concrete commits. Ask for repository visibility if it has not been specified. Never publish to Core5G's remote.
5. After authorization, publish the bootstrap main and phase branch to the selected new repository, then open a draft PR with scope, validation, risks, and requirement references. Creation of a GitHub repository is not part of local phase 0 approval.
6. For subsequent approved phases, branch from the latest reviewed `main`, keep commits focused, run relevant checks, and open/update the phase PR after publication is authorized.
7. Merge only with user authorization and required checks satisfied. Phase completion alone does not authorize a merge or publication.

Use conventional commit subjects such as `docs(architecture): define task lifecycle`, `build(cmake): add reproducible C++ toolchain`, or `feat(state): enforce lease fencing`. Commit titles describe changes actually present. PR descriptions explain resulting behavior, verification, and material limitations. Link issue numbers only after real issues exist.

Issues and milestones can track the phases after the remote repository exists and their creation is authorized. No GitHub PR, issue, successful workflow, or hosting URL exists as part of this local design phase.

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

## Phase 1 file proposal for the next approval

The intended initial file set is:

```text
CMakeLists.txt
CMakePresets.json
vcpkg.json
.clang-format
.clang-tidy
proto/CMakeLists.txt
proto/nexusmesh/v1/task.proto
proto/nexusmesh/v1/router.proto
proto/nexusmesh/v1/worker.proto
proto/nexusmesh/v1/state.proto
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

The first worker exchange is a bounded foundation smoke test; it must not claim that complete scheduling, Redis, or task lifecycle behavior exists. A dependency compatibility check may justify a revised file list before implementation. Do not start these changes without phase 1 approval.

## Validation strategy

For phase 0, inspect Markdown links and formatting, confirm the eight-file scope, check Git whitespace, and review semantic consistency between service contracts and state transitions. No runtime tests apply because no implementation exists.

For implementation phases, use GoogleTest and GoogleMock for logic and injected dependency failures; use real Redis/gRPC/REST integration tests for transactional boundaries. Include tests for lost acknowledgements, lease expiry, duplicate submissions, stale results, overload, and concurrency. Use sanitizers and thread checking where supported by the selected Linux toolchain.

CI grows to include formatting, clang-tidy/static analysis, CMake build, unit tests, integration tests, Docker builds, Kubernetes schema validation, dependency checks, and retained evidence. Integration tests use bounded waits and clean up only their own resources.

Benchmark reports record machine/runner details, compiler/build mode, image/dependency revisions, configuration, worker counts, warmup, sample count, and observability settings. Store raw observations; compute p50/p95/p99 and throughput from real measurements. Current benchmark and deployment status is **not measured / not executed**.
