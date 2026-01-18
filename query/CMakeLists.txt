add_executable(query
    src/main.cpp
)

target_include_directories(query PRIVATE ${CMAKE_SOURCE_DIR}/common)

target_link_libraries(query PRIVATE common)
