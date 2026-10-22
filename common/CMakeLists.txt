add_library(taskrelay_common STATIC grpc/health_client.cc)
target_include_directories(taskrelay_common PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(taskrelay_common PUBLIC taskrelay_proto taskrelay_options)
