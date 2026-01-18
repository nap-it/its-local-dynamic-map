cmake_minimum_required(VERSION 3.14)
project(ldm-service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_TESTING "Enable tests" ON)

# Set include directories globally before processing subdirectories
include_directories(${CMAKE_SOURCE_DIR}/common/spdlog/include)
include_directories(${CMAKE_SOURCE_DIR}/common/rapidjson/include)
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/paho-mqtt-cpp-wrapper/include)
include_directories(${CMAKE_SOURCE_DIR}/common)

add_subdirectory(common)
add_subdirectory(core)
add_subdirectory(ingestion)
add_subdirectory(query)
