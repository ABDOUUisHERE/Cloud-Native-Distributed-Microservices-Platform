set(TASKRELAY_PROTO_FILES
    taskrelay/v1/task.proto
    taskrelay/v1/router.proto
    taskrelay/v1/worker.proto
    taskrelay/v1/state.proto
    grpc/health/v1/health.proto)
set(TASKRELAY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
find_path(TASKRELAY_PROTOBUF_IMPORT_DIR google/protobuf/timestamp.proto REQUIRED)
file(MAKE_DIRECTORY "${TASKRELAY_GENERATED_DIR}")
set(TASKRELAY_PROTO_SOURCES)
set(TASKRELAY_PROTO_DEPENDENCIES)
foreach(proto IN LISTS TASKRELAY_PROTO_FILES)
    list(APPEND TASKRELAY_PROTO_DEPENDENCIES "${CMAKE_CURRENT_SOURCE_DIR}/${proto}")
endforeach()
foreach(proto IN LISTS TASKRELAY_PROTO_FILES)
    string(REGEX REPLACE "\\.proto$" "" stem "${proto}")
    set(outputs
        "${TASKRELAY_GENERATED_DIR}/${stem}.pb.cc"
        "${TASKRELAY_GENERATED_DIR}/${stem}.pb.h"
        "${TASKRELAY_GENERATED_DIR}/${stem}.grpc.pb.cc"
        "${TASKRELAY_GENERATED_DIR}/${stem}.grpc.pb.h")
    add_custom_command(
        OUTPUT ${outputs}
        COMMAND protobuf::protoc
        ARGS "--proto_path=${CMAKE_CURRENT_SOURCE_DIR}"
             "--proto_path=${TASKRELAY_PROTOBUF_IMPORT_DIR}"
             "--cpp_out=${TASKRELAY_GENERATED_DIR}"
             "--grpc_out=${TASKRELAY_GENERATED_DIR}"
             "--plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
             "${CMAKE_CURRENT_SOURCE_DIR}/${proto}"
        DEPENDS ${TASKRELAY_PROTO_DEPENDENCIES} protobuf::protoc gRPC::grpc_cpp_plugin
        VERBATIM)
    list(APPEND TASKRELAY_PROTO_SOURCES ${outputs})
endforeach()
add_library(taskrelay_proto STATIC ${TASKRELAY_PROTO_SOURCES})
target_include_directories(taskrelay_proto SYSTEM PUBLIC "${TASKRELAY_GENERATED_DIR}")
target_link_libraries(taskrelay_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)
