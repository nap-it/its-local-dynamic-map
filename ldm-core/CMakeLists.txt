add_executable(ldm-core
    src/main.cpp
)

target_include_directories(ldm-core PRIVATE ${CMAKE_SOURCE_DIR}/common)

target_link_libraries(ldm-core PRIVATE ldm_common)
