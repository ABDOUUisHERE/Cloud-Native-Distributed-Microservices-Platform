# REST and gRPC contracts

Status: phase 0 interface design. This document specifies intended semantics; `.proto` files and generated bindings will be introduced in phase 1.

## Versioning and representation

Use package `taskrelay.v1` and separate `task.proto`, `router.proto`, `worker.proto`, and `state.proto`. Use the standard `grpc.health.v1` health contract rather than an application-specific replacement.

| Message | Proposed fields and constraints |
| --- | --- |
| `TaskSpec` | `TaskType type`, typed `oneof input`, bounded requested execution timeout |
| `NumericInput` | Numeric operation enum, positive bounded iteration count, deterministic seed |
| `TransformInput` | Transformation enum and bounded UTF-8 data |
| `NetworkSimulationInput` | Bounded synthetic packet count/size, seed, local delay/loss parameters; no host, URL, or external target |
| `Task` | Opaque UUID, specification, lifecycle state, creation/update timestamps, global deadline, attempt count, optional result/error, correlation ID |
| `TaskResult` | Typed numeric, transformation, or synthetic-network result |
| `TaskError` | Stable error code, sanitized detail, retryable indicator |
| `TaskLease` | Task, opaque attempt token, attempt number, lease expiry |
| `Page` | Bounded results and opaque next-page cursor |

Enum value zero is UNSPECIFIED and is rejected where a meaningful value is required. State enums include PENDING, RUNNING, COMPLETED, FAILED, and RETRY_PENDING. Tokens, raw idempotency keys, and internal error details are excluded from public task representations.

Initial limits are configurable but globally bounded: 64 KiB HTTP request body, 32 KiB input data, 64 KiB result, 1 MiB gRPC message, page size 1–100, and at most 30 seconds per execution. CPU workload parameters receive operation-specific limits in phase 2. The gateway and State Service both validate semantic limits, preventing bypass through internal RPCs.

JSON numbers must fit their declared Protobuf ranges. Fingerprinting uses the validated typed input's canonical representation, not raw JSON bytes or noncanonical Protobuf serialization. Unknown JSON fields are rejected to catch misspelled inputs. Preserve operation and unit semantics across versions.

## Services

| Service / RPC | Request → response | Semantics |
| --- | --- | --- |
| `TaskRouterService.SubmitTask` | `SubmitTaskRequest(spec, idempotency_key)` → `SubmitTaskResponse(task, replayed)` | Persist before acknowledgement; replay returns the same task |
| `WorkerService.ExecuteTask` | `ExecuteTaskRequest(lease)` → `ExecuteTaskResponse(task_id, attempt, outcome)` | Unary, deadline bounded; returns after State Service publication, or signals an uncertain transport outcome |
| `StateService.CreateTask` | `CreateTaskRequest(spec, idempotency_key)` → `CreateTaskResponse(task, replayed)` | Atomically create task, dedup mapping, indexes, and queue entry |
| `StateService.GetTask` | `GetTaskRequest(task_id)` → `Task` | Read retained task or NOT_FOUND |
| `StateService.ListTasks` | `ListTasksRequest(state_filter, page_size, cursor)` → `Page` | Bounded pagination in creation order; no full key scan |
| `StateService.AcquireTask` | `AcquireTaskRequest(router_id)` → `AcquireTaskResponse(optional lease)` | Recover a bounded expired batch, then lease eligible work; no work is a successful empty response |
| `StateService.StartTask` | `StartTaskRequest(task_id, token, worker_id)` → `Task` | Validate unexpired lease and bind worker; same token/owner replay is idempotent |
| `StateService.RenewLease` | `RenewLeaseRequest(task_id, token, worker_id)` → `LeaseExpiry` | Extend only current unexpired owned lease, capped by task deadline |
| `StateService.CompleteTask` | `CompleteTaskRequest(task_id, token, worker_id, result)` → `Task` | Atomically accept result and remove lease; matching replay returns stored outcome |
| `StateService.FailAttempt` | `FailAttemptRequest(task_id, token, actor_id, error)` → `Task` | Schedule retry or finalize failure; stale or conflicting writes are rejected |
| `grpc.health.v1.Health.Check` | Standard health request → response | Report per-service readiness |
| `grpc.health.v1.Health.Watch` | Standard health request → response stream | Stream health transitions; finite client session deadline with reconnect |

The router may report a pre-start dispatch rejection; once a worker owns a lease, only that worker may report failure for it. On transport ambiguity the router queries state and leaves an owned lease intact until completion or expiry. Atomic operations record enough outcome metadata to distinguish an idempotent replay from a conflicting replay.

Duplicate ExecuteTask calls must not repeat local computation for the same token. Worker admission tracks active tokens; State Service ownership checks prevent another worker accepting the same lease. A restarted worker has a new instance ID and waits for lease recovery rather than impersonating the previous owner.

Pagination cursors contain a version, normalized filter, and stable `(created_at, task_id)` continuation position. Concurrent additions and retention mean pagination is not a snapshot. Bounded index traversal may return a short page with a continuation cursor; scanning does not grow without limit for rare filters.

## REST mapping

| Endpoint | Successful response |
| --- | --- |
| `POST /api/v1/tasks` | HTTP 202, task representation and `Location: /api/v1/tasks/{id}` |
| `GET /api/v1/tasks/{id}` | HTTP 200 with task representation |
| `GET /api/v1/tasks?state=...&page_size=...&cursor=...` | HTTP 200 with tasks and optional next cursor |
| `GET /health/live` | HTTP 200 when process event/control loops progress |
| `GET /health/ready` | HTTP 200 when ready, otherwise HTTP 503 |
| `GET /metrics` | Prometheus exposition on the private administration listener |

POST requires a 1–128 character printable ASCII `Idempotency-Key`. Scope is the single laboratory application; multi-tenant identity scoping is outside v1. A same-key/same-input replay returns HTTP 202 and the existing task, including its current terminal state if already complete. A same-key/different-input request returns HTTP 409. After terminal retention and key expiry, the same key may create a new task; clients must not assume indefinite deduplication.

Return JSON errors containing a stable application `code`, safe `message`, and `correlation_id`. Never return tokens, secrets, payload dumps, or raw Redis errors.

| gRPC / validation outcome | HTTP mapping |
| --- | --- |
| INVALID_ARGUMENT | 400 |
| NOT_FOUND | 404 |
| ALREADY_EXISTS, FAILED_PRECONDITION, ABORTED | 409 |
| RESOURCE_EXHAUSTED | 429, with bounded Retry-After where applicable |
| UNAVAILABLE | 503 |
| DEADLINE_EXCEEDED | 504 |
| INTERNAL, DATA_LOSS, unexpected errors | 500 |
| Oversized HTTP body | 413 before an RPC |
| Unsupported content type | 415 before an RPC |

FAILED_PRECONDITION includes invalid lifecycle ownership or expired/stale tokens. A matching lifecycle replay succeeds; incompatible replays return a conflict. A task's business failure is represented as FAILED in a successful GET response, rather than HTTP 500.

## Deadlines, retries, and propagation

| Operation | Initial budget |
| --- | --- |
| HTTP submit | 3 seconds total |
| HTTP query | 1 second total |
| Individual State Service RPC | At most 500 ms and no longer than parent time remaining |
| ExecuteTask | Requested task execution budget, at most 30 seconds, also bounded by global deadline |
| Lease | 15 seconds, renewed every 5 seconds while valid |
| Health Check | 500 ms |
| Health Watch | 60-second session, then reconnect |

All values are initial design defaults, not measured SLOs. Explicitly propagate remaining deadline budgets and cancellation to downstream work. The lease renewal budget is its own bounded control operation, independent of a blocked processing thread. For an execution nearing its deadline, stop admitting further work and cancel cooperatively.

Each logical dependency call permits at most two transport attempts, including the first, within the original deadline. Retry only safely replayable operations for transient UNAVAILABLE errors. Do not automatically replay ExecuteTask on an ambiguous timeout; inspect state and rely on lease recovery. No hidden unbounded gRPC retry configuration is enabled. Business execution has its separate three-attempt limit.

Validate or generate `x-correlation-id` (at most 64 safe ASCII characters), propagate W3C `traceparent`/`tracestate`, and return the correlation ID in the HTTP response. Every task attempt creates a new span linked to the submission context, so queue delay and retries remain explainable. Bound metadata sizes; malformed trace context starts a new trace rather than rejecting valid business input.

Reference: [gRPC deadlines](https://grpc.io/docs/guides/deadlines/).

## Compatibility rules

Assign explicit stable field numbers in phase 1. Never reuse deleted numbers or enum values; reserve their numbers and names. Add optional fields without changing existing meanings. Preserve unknown fields where supported and avoid changing wire types. Breaking API changes require a new package such as `taskrelay.v2` and a documented migration. Pin compatible protoc and gRPC generation tooling with the runtime dependencies.
