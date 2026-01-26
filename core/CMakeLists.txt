add_executable(core
    main.cpp
    locator.cpp
    metrics.cpp
)

target_include_directories(core PRIVATE ${CMAKE_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(core PRIVATE common)
