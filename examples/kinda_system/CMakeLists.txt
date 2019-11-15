cmake_minimum_required(VERSION 3.0.0)
project(kinda_system VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)

include_directories(../../include)
add_executable(kinda_system main.cpp)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(kinda_system Threads::Threads)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)