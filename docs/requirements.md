# Requirements and acceptance criteria

Status: approved direction recorded during phase 0; phase 1 adds wire contracts and a gRPC foundation. The full acceptance criteria below remain pending until their corresponding service, deployment, and test phases are implemented.

## Purpose and scope

Build TaskRelay as an independent C++20 project targeting Linux, Docker Compose, and local Kubernetes with Kind. Demonstrate a reproducible distributed-systems laboratory suitable for a software engineering R&D interview. Keep synthetic workloads local and bounded. Prefer explicit guarantees and measured evidence over architectural complexity.

The source is the user's original technical specification, version 1.0. The user subsequently selected a public repository, delegated the project name choice, and required the entire project to be in English. The resulting project name is TaskRelay; use English for documentation, code, comments, user-facing content, commits, and pull requests. This document maps its requirements to planned verification; it does not replace the specification's approval rules.

## Functional requirements

| ID | Requirement | Acceptance evidence |
| --- | --- | --- |
| F01 | Gateway exposes `POST /api/v1/tasks`, `GET /api/v1/tasks/{id}`, and paginated `GET /api/v1/tasks` | REST integration tests for valid requests, malformed inputs, limits, and error mappings |
| F02 | Gateway validates inputs and propagates correlation and trace context | Unit tests plus a trace spanning HTTP and gRPC |
| F03 | Router persists tasks before acknowledging submission and dispatches through gRPC | Crash-window tests and end-to-end task completion |
| F04 | Workers perform numeric computation, data transformation, and synthetic local network simulation | Deterministic processor tests and bounded resource tests |
| F05 | Workers use a bounded C++ thread pool and support graceful shutdown | Concurrency tests, overload tests, and shutdown under load |
| F06 | State Service owns Redis access and rejects invalid lifecycle updates | Atomic transition, stale-token, and concurrent-update tests |
| F07 | States include PENDING, RUNNING, COMPLETED, FAILED, and RETRY_PENDING | State-machine tests covering each allowed and rejected transition |
| F08 | At least two workers operate concurrently with observable distribution | Per-worker metrics and execution records; no assertion of equal CPU load |
| F09 | Protobuf defines submission, queries, execution, lifecycle, and health RPCs | Generated C++ builds and gRPC integration tests |

## Reliability and security requirements

| ID | Requirement | Acceptance evidence |
| --- | --- | --- |
| R01 | Every outbound RPC has a finite deadline | Client configuration review and injected timeout tests |
| R02 | Retries have exponential backoff, jitter, and finite budgets | Policy tests with controlled clocks and exhaustion tests |
| R03 | Idempotent submission rejects conflicting payloads for the same key | Concurrent duplicate submission tests and conflict responses |
| R04 | Leases, recovery, and fencing reject stale attempt results | Worker termination, delayed response, and lease-expiry tests |
| R05 | Circuit breakers isolate unavailable dependencies | Closed/open/half-open transition tests |
| R06 | Health, readiness, liveness, and graceful shutdown are exposed | Dependency outage and termination tests |
| R07 | Input, queue, thread, memory, and CPU usage are bounded | Boundary tests and deployment resource inspection |
| R08 | Secrets are excluded from Git, logs, traces, and responses | Configuration and output inspection; dependency/security checks |
| R09 | Redis is private to the deployment network | Compose port and Kubernetes Service/NetworkPolicy inspection |
| R10 | Processing semantics and durability limits are explicit | Documentation and recovery evidence; no exactly-once claim |

## Deployment, telemetry, and quality requirements

| ID | Requirement | Acceptance evidence |
| --- | --- | --- |
| Q01 | Multi-stage C++ images run with nonprivileged users | Image build and container runtime inspection |
| Q02 | Compose runs four application services, at least two workers, Redis, Prometheus, Grafana, and Jaeger | Clean-start integration run with health checks |
| Q03 | Kind deployment includes Deployments, Services, ConfigMaps, secret references, probes, and resource bounds | Manifest validation and a real cluster smoke test |
| Q04 | Prometheus captures request counts, latency distributions, successes, failures, retries, durations, backlog, and availability | Scrape verification and dashboard queries |
| Q05 | Grafana provides an actionable dashboard and Jaeger shows propagated traces | Dashboard review and trace evidence |
| Q06 | GoogleTest/GoogleMock cover validation, state, retry, breaker, router, concurrency, and errors | Unit test reports; sanitizers where supported |
| Q07 | Integration tests exercise REST, gRPC, and Redis | CI and local integration reports |
| Q08 | Resilience tests interrupt a worker, router, Redis, a gRPC call, and a Kubernetes pod | Reproducible scenarios with observed state outcomes |
| Q09 | Benchmarks record throughput, p50/p95/p99, CPU, memory, and replica scaling | Raw real measurements with environment and methodology |
| Q10 | CI checks formatting, static analysis, build, unit/integration tests, images, manifests, and dependencies | Successful workflow runs with retained artifacts |
| Q11 | Dependencies and tooling are reproducible | Locked Ubuntu library versions or pinned vcpkg baseline, plus image digests and action revisions; record base OS/toolchain versions |
| Q12 | Kubernetes HPA is added only after a usable metrics source exists | Metrics-server or validated custom metrics setup and scaling evidence |

## REST surface

All application processes will expose `/health/live`, `/health/ready`, and `/metrics` on their HTTP administration listener. The gateway also exposes the three public task routes. Internal administration ports remain private except explicit localhost mappings for the laboratory.

## Required final documentation

The final repository must contain `README.md` and the following documents under `docs/`: `architecture.md`, `requirements.md`, `grpc-contracts.md`, `kubernetes.md`, `resilience.md`, `observability.md`, `testing.md`, `benchmark.md`, and `demo.md`. Phase 0 additionally introduces `development-plan.md`. Future documents will be created with the phase that supplies verified content.

## Approval and completion rules

Before each phase, present files to create or modify and obtain the user's agreement. Do not publish commits without explicit authorization. Phase 0 was published and merged with approval. Phase 1 local implementation is authorized; its publication, merge, and later phase starts require their own approval.

The project is complete only when all required services compile and operate, both deployment paths work, workers distribute tasks, recovery scenarios are demonstrated, telemetry is visible, automated checks pass, and the documentation reproduces those demonstrations. No benchmark target or CI success is inferred from a design document.
