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
)

target_include_directories(ldm_common
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)

# External deps would be located via find_package in a complete environment.
target_link_libraries(ldm_common
    PUBLIC Threads::Threads
)
