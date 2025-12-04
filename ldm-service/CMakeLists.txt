cmake_minimum_required(VERSION 3.14)
project(ldm-service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_TESTING "Enable tests" ON)

add_subdirectory(common)
add_subdirectory(ldm-core)
add_subdirectory(ldm-ingestion)
add_subdirectory(ldm-query)
