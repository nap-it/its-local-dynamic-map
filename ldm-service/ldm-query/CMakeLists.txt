add_executable(ldm-query
    src/main.cpp
)

target_include_directories(ldm-query PRIVATE ${CMAKE_SOURCE_DIR}/common)

target_link_libraries(ldm-query PRIVATE ldm_common)
