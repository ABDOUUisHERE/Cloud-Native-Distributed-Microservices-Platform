add_library(taskrelay_state STATIC memory_store.cc state_service.cc)
target_link_libraries(taskrelay_state PUBLIC taskrelay_common)
add_executable(taskrelay-state main.cc)
target_link_libraries(taskrelay-state PRIVATE taskrelay_state)
