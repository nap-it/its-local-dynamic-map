add_executable(core
    main.cpp
    cpm_message.cpp
    cpm_store.cpp
    locator.cpp
    mapem_message.cpp
    vam_message.cpp
    geometry_utils.cpp
    ldm_model.cpp
    ldm_store.cpp
    metrics.cpp
)

target_include_directories(core PRIVATE ${CMAKE_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(core PRIVATE common)
