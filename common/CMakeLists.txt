add_library(ldm_common
    config/Config.cpp
    geometry/GeometryUtils.cpp
    locator/Locator.cpp
    metrics/Metrics.cpp
    ldm-model/LdmModel.cpp
    ldm-model/LdmStore.cpp
)

target_include_directories(ldm_common
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)

# External deps would be located via find_package in a complete environment.
target_link_libraries(ldm_common
    PUBLIC Threads::Threads
)
