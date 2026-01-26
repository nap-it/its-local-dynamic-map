add_executable(query
    main.cpp
    metrics.cpp
)

target_include_directories(query PRIVATE ${CMAKE_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(query PRIVATE common)
