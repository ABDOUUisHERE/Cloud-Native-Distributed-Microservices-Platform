#include "services/api-gateway/routes.h"

#include <google/protobuf/util/json_util.h>

#include <charconv>

#include "common/grpc/health_client.h"
#include "common/task/validation.h"
namespace taskrelay {
namespace {
int HttpCode(grpc::StatusCode code) {
  switch (code) {
    case grpc::StatusCode::INVALID_ARGUMENT:
      return 400;
    case grpc::StatusCode::NOT_FOUND:
      return 404;
    case grpc::StatusCode::ALREADY_EXISTS:
    case grpc::StatusCode::FAILED_PRECONDITION:
    case grpc::StatusCode::ABORTED:
      return 409;
    case grpc::StatusCode::RESOURCE_EXHAUSTED:
      return 429;
    case grpc::StatusCode::UNAVAILABLE:
      return 503;
    case grpc::StatusCode::DEADLINE_EXCEEDED:
      return 504;
    default:
      return 500;
  }
}
drogon::HttpResponsePtr Error(int status, const std::string& correlation) {
  Json::Value error;
  switch (status) {
    case 400:
      error["code"] = "INVALID_ARGUMENT";
      break;
    case 404:
      error["code"] = "NOT_FOUND";
      break;
    case 409:
      error["code"] = "CONFLICT";
      break;
    case 413:
      error["code"] = "PAYLOAD_TOO_LARGE";
      break;
    case 415:
      error["code"] = "UNSUPPORTED_MEDIA_TYPE";
      break;
    case 429:
      error["code"] = "RESOURCE_EXHAUSTED";
      break;
    case 503:
      error["code"] = "UNAVAILABLE";
      break;
    case 504:
      error["code"] = "DEADLINE_EXCEEDED";
      break;
    default:
      error["code"] = "INTERNAL";
  }
  error["message"] = "Request could not be completed";
  error["correlation_id"] = correlation;
  auto response = drogon::HttpResponse::newHttpJsonResponse(error);
  response->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
  response->addHeader("X-Correlation-Id", correlation);
  if (status == 429) response->addHeader("Retry-After", "1");
  return response;
}
drogon::HttpResponsePtr JsonResponse(const google::protobuf::Message& message, int code,
                                     const std::string& correlation) {
  std::string json;
  if (!google::protobuf::util::MessageToJsonString(message, &json).ok())
    return Error(500, correlation);
  auto response = drogon::HttpResponse::newHttpResponse();
  response->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
  response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
  response->setBody(json);
  response->addHeader("X-Correlation-Id", correlation);
  return response;
}
}  // namespace
Routes::Routes(const Targets& targets)
    : router_channel_(MakeWorkerChannel(targets.router)),
      router_(v1::TaskRouterService::NewStub(router_channel_)),
      state_(targets.state) {}
void Routes::Register() {
  drogon::app().registerHandler(
      "/api/v1/tasks",
      [this](const drogon::HttpRequestPtr& request, Reply&& reply) { Enqueue(request, reply); },
      {drogon::Get, drogon::Post});
  drogon::app().registerHandler("/api/v1/tasks/{1}",
                                [this](const drogon::HttpRequestPtr& request, Reply&& reply,
                                       const std::string& id) { Enqueue(request, reply, id); },
                                {drogon::Get});
  drogon::app().registerHandler(
      "/health/ready",
      [this](const drogon::HttpRequestPtr& request, Reply&& reply) { Enqueue(request, reply); },
      {drogon::Get});
  drogon::app().registerHandler("/health/live",
                                [](const drogon::HttpRequestPtr&, Reply&& reply) {
                                  Json::Value body;
                                  body["status"] = "live";
                                  reply(drogon::HttpResponse::newHttpJsonResponse(body));
                                },
                                {drogon::Get});
}
void Routes::Enqueue(const drogon::HttpRequestPtr& request, const Reply& reply, std::string id) {
  auto correlation = request->getHeader("x-correlation-id");
  if (!SafeAscii(correlation, 64)) correlation = NewId();
  const auto deadline =
      std::chrono::system_clock::now() +
      (request->method() == drogon::Post ? std::chrono::seconds(3) : std::chrono::seconds(1));
  if (!executor_.Submit(
          [this, request, reply,
           call = RequestContext{
               .task_id = std::move(id), .correlation = correlation, .deadline = deadline}] {
            try {
              Handle(request, reply, call);
            } catch (const std::exception&) {
              reply(Error(500, call.correlation));
            }
          }))
    reply(Error(429, correlation));
}
void Routes::Handle(const drogon::HttpRequestPtr& request, const Reply& reply,
                    const RequestContext& call) {
  const auto& task_id = call.task_id;
  const auto& correlation = call.correlation;
  const auto deadline = call.deadline;
  if (std::chrono::system_clock::now() >= deadline) {
    reply(Error(504, correlation));
    return;
  }
  if (request->path() == "/health/ready") {
    v1::ListTasksRequest list;
    list.set_page_size(1);
    v1::Page page;
    const auto status = state_.ListTasks(list, &page, deadline, correlation);
    const auto router_status =
        ProbeWorker(router_channel_, v1::TaskRouterService::service_full_name(),
                    std::chrono::milliseconds(500), deadline);
    Json::Value body;
    body["status"] = status.ok() && router_status.ok() ? "ready" : "unavailable";
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(status.ok() && router_status.ok() ? drogon::k200OK
                                                              : drogon::k503ServiceUnavailable);
    response->addHeader("X-Correlation-Id", correlation);
    reply(response);
    return;
  }
  if (request->method() == drogon::Post) {
    if (request->body().size() > 65536) {
      reply(Error(413, correlation));
      return;
    }
    auto content_type = request->getHeader("content-type");
    const auto separator = content_type.find(';');
    content_type.resize(separator == std::string::npos ? content_type.size() : separator);
    if (content_type != "application/json") {
      reply(Error(415, correlation));
      return;
    }
    v1::SubmitTaskRequest submit;
    google::protobuf::util::JsonParseOptions options;
    options.ignore_unknown_fields = false;
    if (!google::protobuf::util::JsonStringToMessage(std::string(request->body()),
                                                     submit.mutable_spec(), options)
             .ok() ||
        !ValidateSpec(submit.spec()).ok()) {
      reply(Error(400, correlation));
      return;
    }
    submit.set_idempotency_key(request->getHeader("idempotency-key"));
    grpc::ClientContext context;
    SetDeadline(context, std::chrono::milliseconds(3000), deadline);
    AddCorrelation(context, correlation);
    v1::SubmitTaskResponse stored;
    const auto status = router_->SubmitTask(&context, submit, &stored);
    if (!status.ok()) {
      reply(Error(HttpCode(status.error_code()), correlation));
      return;
    }
    auto response = JsonResponse(stored.task(), 202, correlation);
    response->addHeader("Location", "/api/v1/tasks/" + stored.task().id());
    response->addHeader("X-Idempotency-Replayed", stored.replayed() ? "true" : "false");
    reply(response);
    return;
  }
  if (!task_id.empty()) {
    if (task_id.size() > 64) {
      reply(Error(400, correlation));
      return;
    }
    v1::GetTaskRequest get;
    get.set_task_id(task_id);
    v1::Task task;
    const auto status = state_.GetTask(get, &task, deadline, correlation);
    reply(status.ok() ? JsonResponse(task, 200, correlation)
                      : Error(HttpCode(status.error_code()), correlation));
    return;
  }
  v1::ListTasksRequest list;
  list.set_page_size(20);
  for (const auto& [name, value] : request->getParameters()) {
    if (name == "page_size") {
      unsigned int size = 0;
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), size);
      if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() || size == 0 ||
          size > 100) {
        reply(Error(400, correlation));
        return;
      }
      list.set_page_size(size);
    } else if (name == "cursor") {
      if (value.size() > 64) {
        reply(Error(400, correlation));
        return;
      }
      list.set_cursor(value);
    } else if (name == "state") {
      v1::TaskState state;
      if (!v1::TaskState_Parse(value, &state) || state == v1::TASK_STATE_UNSPECIFIED) {
        reply(Error(400, correlation));
        return;
      }
      list.add_state_filter(state);
    } else {
      reply(Error(400, correlation));
      return;
    }
  }
  v1::Page page;
  const auto status = state_.ListTasks(list, &page, deadline, correlation);
  reply(status.ok() ? JsonResponse(page, 200, correlation)
                    : Error(HttpCode(status.error_code()), correlation));
}
}  // namespace taskrelay
