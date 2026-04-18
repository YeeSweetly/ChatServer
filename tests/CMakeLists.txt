cmake_minimum_required(VERSION 3.10)
project(StressClient)

set(CMAKE_CXX_STANDARD 11)

add_executable(StressClient StressClient.cpp)

target_link_libraries(StressClient pthread)

set_target_properties(StressClient PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
