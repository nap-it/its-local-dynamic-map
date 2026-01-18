add_executable(ingestion
    src/main.cpp
)

target_include_directories(ingestion PRIVATE ${CMAKE_SOURCE_DIR}/common)

target_link_libraries(ingestion PRIVATE common)
