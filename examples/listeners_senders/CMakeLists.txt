cmake_minimum_required(VERSION 3.0.0)
project(ev_sys_test VERSION 0.1.0)

include_directories(../../include)
add_executable(ev_sys_test main.cpp)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(ev_sys_test Threads::Threads)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})