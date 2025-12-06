file(GLOB FASTDDS_SRC
    ${CMAKE_SOURCE_DIR}/fastdds-cpp-wrapper/*.cpp
    ${CMAKE_SOURCE_DIR}/fastdds-cpp-wrapper/*.c
    ${CMAKE_SOURCE_DIR}/fastdds-cpp-wrapper/*.h
    ${CMAKE_SOURCE_DIR}/fastdds-cpp-wrapper/*.hpp
    ${CMAKE_SOURCE_DIR}/fastdds-cpp-wrapper/*.cxx
)

add_subdirectory(${CMAKE_SOURCE_DIR}/paho-mqtt-cpp-wrapper ${CMAKE_BINARY_DIR}/paho-mqtt-cpp-wrapper)

add_library(ldm_common
    config/config.cpp
    geometry/geometry_utils.cpp
    locator/locator.cpp
    messaging/fastdds_transport.cpp
    messaging/in_memory_bus.cpp
    messaging/mqtt_transport.cpp
    messaging/transport.cpp
    messaging/zenoh_transport.cpp
    metrics/metrics.cpp
    ldm-model/ldm_model.cpp
    ldm-model/ldm_store.cpp
    ${FASTDDS_SRC}
)

target_include_directories(ldm_common
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include
           ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include
           ${CMAKE_SOURCE_DIR}
           ${CMAKE_SOURCE_DIR}/paho-mqtt-cpp-wrapper/include
)

find_package(Threads REQUIRED)
find_package(zenohc REQUIRED)
find_package(zenohcxx REQUIRED)

target_link_libraries(ldm_common
    PUBLIC Threads::Threads
           paho-mqtt-cpp-wrapper
           paho-mqttpp3
           paho-mqtt3as
           fastcdr
           fastrtps
           zenohcxx::zenohc
)
