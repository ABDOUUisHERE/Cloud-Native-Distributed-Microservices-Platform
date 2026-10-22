add_executable(taskrelay-gateway main.cc routes.cc)
target_link_libraries(taskrelay-gateway PRIVATE taskrelay_common Drogon::Drogon)
