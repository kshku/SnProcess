#include <snprocess/snprocess.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, name)               \
    do {                                \
        if (!(cond)) {                  \
            printf("FAIL: %s\n", name); \
            return 1;                   \
        }                               \
    } while (0)

int main(void) {
    SnPipe read_end, write_end;
    CHECK(sn_pipe_create(&read_end, &write_end), "sn_pipe_create");

    const char *message = "hello pipe";
    size_t length = strlen(message);
    CHECK(sn_pipe_write(&write_end, message, length) == (int64_t)length, "sn_pipe_write");
    CHECK(sn_pipe_wait(&read_end, SN_TIMEOUT_INFINITE) == SN_PIPE_READABLE,
          "sn_pipe_wait "
          "readable");

    char buffer[64] = {0};
    CHECK(sn_pipe_read(&read_end, buffer, sizeof(buffer)) == (int64_t)length, "sn_pipe_read");
    CHECK(strcmp(buffer, message) == 0, "sn_pipe_read content");

    CHECK(sn_pipe_wait(&read_end, 10) == SN_PIPE_TIMEOUT, "sn_pipe_wait timeout");

    sn_pipe_close(&write_end);
    CHECK(sn_pipe_wait(&read_end, SN_TIMEOUT_INFINITE) == SN_PIPE_CLOSED, "sn_pipe_wait closed");
    CHECK(sn_pipe_read(&read_end, buffer, sizeof(buffer)) == 0, "sn_pipe_read eof");
    sn_pipe_close(&read_end);

    SnPipe a, b;
    CHECK(sn_pipe_create_duplex(&a, &b), "sn_pipe_create_duplex");

    CHECK(sn_pipe_write(&a, "ping", 4) == 4, "duplex write a");
    CHECK(sn_pipe_wait(&b, SN_TIMEOUT_INFINITE) == SN_PIPE_READABLE, "duplex wait b");
    memset(buffer, 0, sizeof(buffer));
    CHECK(sn_pipe_read(&b, buffer, sizeof(buffer)) == 4, "duplex read b");
    CHECK(strcmp(buffer, "ping") == 0, "duplex read b content");

    CHECK(sn_pipe_write(&b, "pong", 4) == 4, "duplex write b");
    CHECK(sn_pipe_wait(&a, SN_TIMEOUT_INFINITE) == SN_PIPE_READABLE, "duplex wait a");
    memset(buffer, 0, sizeof(buffer));
    CHECK(sn_pipe_read(&a, buffer, sizeof(buffer)) == 4, "duplex read a");
    CHECK(strcmp(buffer, "pong") == 0, "duplex read a content");

    sn_pipe_close(&a);
    CHECK(sn_pipe_wait(&b, SN_TIMEOUT_INFINITE) == SN_PIPE_CLOSED, "duplex wait closed");
    sn_pipe_close(&b);

    printf("ALL TESTS PASSED\n");
    return 0;
}
