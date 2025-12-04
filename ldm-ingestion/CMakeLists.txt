add_executable(ldm-ingestion
    src/main.cpp
)

target_include_directories(ldm-ingestion PRIVATE ${CMAKE_SOURCE_DIR}/common)

target_link_libraries(ldm-ingestion PRIVATE ldm_common)
