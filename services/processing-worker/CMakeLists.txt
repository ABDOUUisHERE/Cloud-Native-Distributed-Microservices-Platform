add_executable(taskrelay-worker main.cc)
target_link_libraries(taskrelay-worker PRIVATE taskrelay_common Threads::Threads)
