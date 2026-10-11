enable_testing()

set(SRCS
    test.c
)

add_executable(sn_process_test)
target_sources(sn_process_test PRIVATE ${SRCS})
target_link_libraries(sn_process_test PRIVATE snprocess)

add_test(NAME sn_process_test COMMAND sn_process_test)
