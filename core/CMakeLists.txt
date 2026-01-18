add_executable(core
    src/main.cpp
)

target_include_directories(core PRIVATE ${CMAKE_SOURCE_DIR}/common)

target_link_libraries(core PRIVATE common)
