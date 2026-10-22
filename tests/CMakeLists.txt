find_package(GTest CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(GoogleTest)

add_executable(grpc_foundation_test integration/grpc_foundation_test.cc)
target_link_libraries(grpc_foundation_test PRIVATE taskrelay_common GTest::gtest_main)
gtest_discover_tests(grpc_foundation_test PROPERTIES TIMEOUT 10)

add_test(NAME foundation_process_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/process_smoke.py"
            "$<TARGET_FILE:taskrelay-worker>" "$<TARGET_FILE:taskrelay-router>")
set_tests_properties(foundation_process_smoke PROPERTIES TIMEOUT 15)

add_executable(task_processing_test unit/workloads_test.cc unit/memory_store_test.cc
    unit/bounded_executor_test.cc integration/task_pipeline_test.cc)
target_link_libraries(task_processing_test PRIVATE taskrelay_state taskrelay_processing
    taskrelay_routing GTest::gtest_main)
gtest_discover_tests(task_processing_test PROPERTIES TIMEOUT 15)
add_test(NAME rest_process_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/rest_smoke.py"
    "$<TARGET_FILE:taskrelay-state>" "$<TARGET_FILE:taskrelay-worker>"
    "$<TARGET_FILE:taskrelay-router>" "$<TARGET_FILE:taskrelay-gateway>")
set_tests_properties(rest_process_smoke PROPERTIES TIMEOUT 35)
