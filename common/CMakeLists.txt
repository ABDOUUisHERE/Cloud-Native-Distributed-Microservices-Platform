add_library(taskrelay_common STATIC grpc/health_client.cc grpc/state_client.cc
    task/validation.cc threading/bounded_executor.cc)
target_include_directories(taskrelay_common PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(taskrelay_common PUBLIC taskrelay_proto taskrelay_options OpenSSL::Crypto Threads::Threads)
