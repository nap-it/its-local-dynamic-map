add_executable(ingestion
    main.cpp
    metrics.cpp
)

target_include_directories(ingestion PRIVATE ${CMAKE_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ingestion PRIVATE common)
