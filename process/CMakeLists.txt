if(SN_PROCESS_BUILD_SHARED)
    add_library(snprocess SHARED)
    target_compile_definitions(snprocess PRIVATE SN_EXPORT)
else()
    add_library(snprocess STATIC)
    target_compile_definitions(snprocess PUBLIC SN_PROCESS_STATIC)
endif()

target_include_directories(snprocess PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(snprocess PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(snprocess PRIVATE sn_process_configs)
target_link_libraries(snprocess PUBLIC sncore sntime)

add_subdirectory(src)
