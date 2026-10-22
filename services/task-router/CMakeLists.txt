add_library(taskrelay_routing STATIC router_service.cc dispatcher.cc)
target_link_libraries(taskrelay_routing PUBLIC taskrelay_common)
add_executable(taskrelay-router main.cc)
target_link_libraries(taskrelay-router PRIVATE taskrelay_routing)
