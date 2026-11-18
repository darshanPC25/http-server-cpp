
cmake_minimum_required(VERSION 3.20)

project(http_server_cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(http_server_cpp main.cpp)

# Link Windows Socket library
target_link_libraries(http_server_cpp PRIVATE ws2_32)
