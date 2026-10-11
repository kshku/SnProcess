enable_testing()

add_executable(sn_process_test)
target_sources(sn_process_test PRIVATE test.c)
target_link_libraries(sn_process_test PRIVATE snprocess)
add_test(NAME sn_process_test COMMAND sn_process_test)

add_executable(sn_process_helper)
target_sources(sn_process_helper PRIVATE helper.c)
target_link_libraries(sn_process_helper PRIVATE snprocess)

add_executable(sn_process_pipe_test)
target_sources(sn_process_pipe_test PRIVATE pipe_test.c)
target_link_libraries(sn_process_pipe_test PRIVATE snprocess)
add_test(NAME sn_process_pipe_test COMMAND sn_process_pipe_test)

add_executable(sn_process_spawn_test)
target_sources(sn_process_spawn_test PRIVATE spawn_test.c)
target_link_libraries(sn_process_spawn_test PRIVATE snprocess)
add_test(NAME sn_process_spawn_test COMMAND sn_process_spawn_test "$<TARGET_FILE:sn_process_helper>")

add_executable(sn_process_command_test)
target_sources(sn_process_command_test PRIVATE command_test.c)
target_link_libraries(sn_process_command_test PRIVATE snprocess)
add_test(NAME sn_process_command_test COMMAND sn_process_command_test "$<TARGET_FILE:sn_process_helper>")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND SN_PROCESS_BUILD_SHARED)
    add_custom_target(copy_dlls ALL
        COMMENT "Copy the dlls"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_RUNTIME_DLLS:sn_process_test>" "$<TARGET_FILE_DIR:sn_process_test>"
        COMMAND_EXPAND_LISTS
    )

    add_dependencies(copy_dlls sn_process_test sn_process_helper)
endif()
