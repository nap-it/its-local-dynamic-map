file(GLOB FASTDDS_SRC
    ${CMAKE_SOURCE_DIR}/fastdds-cpp-wrapper/*.cpp
    ${CMAKE_SOURCE_DIR}/fastdds-cpp-wrapper/*.c
    ${CMAKE_SOURCE_DIR}/fastdds-cpp-wrapper/*.h
    ${CMAKE_SOURCE_DIR}/fastdds-cpp-wrapper/*.hpp
    ${CMAKE_SOURCE_DIR}/fastdds-cpp-wrapper/*.cxx
)

add_subdirectory(${CMAKE_SOURCE_DIR}/paho-mqtt-cpp-wrapper ${CMAKE_BINARY_DIR}/paho-mqtt-cpp-wrapper)

add_library(common
    messaging/fastdds_transport.cpp
    messaging/in_memory_bus.cpp
    messaging/mqtt_transport.cpp
    messaging/transport.cpp
    messaging/zenoh_transport.cpp
    ${FASTDDS_SRC}
)

target_include_directories(common
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include
           ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include
           ${CMAKE_SOURCE_DIR}
           ${CMAKE_SOURCE_DIR}/paho-mqtt-cpp-wrapper/include
)

find_package(Threads REQUIRED)
find_package(zenohc REQUIRED)
find_package(zenohcxx REQUIRED)

target_link_libraries(common
    PUBLIC Threads::Threads
           paho-mqtt-cpp-wrapper
           paho-mqttpp3
           paho-mqtt3as
           fastcdr
           fastrtps
           zenohcxx::zenohc
)
