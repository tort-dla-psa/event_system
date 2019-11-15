cmake_minimum_required(VERSION 3.0.0)
project(ev_sys VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(examples/kinda_system)
add_subdirectory(examples/listeners_senders)
add_subdirectory(examples/highload_memory)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})