#if !defined(_WIN32)
    #define _GNU_SOURCE
#endif

#include <snprocess/snprocess.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #include <direct.h>

    #define test_getcwd _getcwd
#else
    #include <unistd.h>

    #define test_getcwd getcwd
#endif

#define CHECK(cond, name)               \
    do {                                \
        if (!(cond)) {                  \
            printf("FAIL: %s\n", name); \
            return 1;                   \
        }                               \
    } while (0)

static void strip_cr(char *text) {
    char *write = text;
    for (char *read = text; *read; ++read) {
        if (*read != '\r') *write++ = *read;
    }
    *write = '\0';
}

static int64_t read_all(SnPipe *pipe, char *out, size_t capacity) {
    size_t total = 0;
    while (total + 1 < capacity) {
        SnPipeWaitStatus status = sn_pipe_wait(pipe, SN_TIMEOUT_INFINITE);
        if (status == SN_PIPE_CLOSED) break;
        if (status != SN_PIPE_READABLE) return -1;

        int64_t read = sn_pipe_read(pipe, out + total, capacity - total - 1);
        if (read < 0) return -1;
        if (read == 0) break;
        total += (size_t)read;
    }
    out[total] = '\0';
    return (int64_t)total;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("FAIL: helper path argument missing\n");
        return 1;
    }
    const char *helper = argv[1];

    {
        const char *child_argv[] = {helper, "exit", "7", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;

        SnProcess proc;
        CHECK(sn_process_spawn(&desc, &proc), "spawn exit");

        int code = -1;
        CHECK(sn_process_wait(&proc, SN_TIMEOUT_INFINITE, &code) == SN_PROCESS_EXITED, "wait exit");
        CHECK(code == 7, "exit code");
        CHECK(!sn_process_is_alive(&proc), "is_alive after exit");
        sn_process_close(&proc);
    }

    {
        SnPipe out_read, out_write;
        CHECK(sn_pipe_create(&out_read, &out_write), "create stdout pipe");

        const char *child_argv[] = {helper, "print", "hello", "world", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;
        desc.stdout_stdio.kind = SN_PROCESS_STDIO_PIPE;
        desc.stdout_stdio.pipe = &out_write;

        SnProcess proc;
        CHECK(sn_process_spawn(&desc, &proc), "spawn print");

        char buffer[128];
        CHECK(read_all(&out_read, buffer, sizeof(buffer)) >= 0, "read stdout");
        strip_cr(buffer);
        CHECK(strcmp(buffer, "hello world\n") == 0, "stdout content");

        int code = -1;
        CHECK(sn_process_wait(&proc, SN_TIMEOUT_INFINITE, &code) == SN_PROCESS_EXITED, "wait "
                                                                                       "print");
        CHECK(code == 0, "print exit code");
        sn_process_close(&proc);
        sn_pipe_close(&out_read);
    }

    {
        SnPipe in_read, in_write, out_read, out_write;
        CHECK(sn_pipe_create(&in_read, &in_write), "create stdin pipe");
        CHECK(sn_pipe_create(&out_read, &out_write), "create echo stdout pipe");

        const char *child_argv[] = {helper, "echo", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;
        desc.stdin_stdio.kind = SN_PROCESS_STDIO_PIPE;
        desc.stdin_stdio.pipe = &in_read;
        desc.stdout_stdio.kind = SN_PROCESS_STDIO_PIPE;
        desc.stdout_stdio.pipe = &out_write;

        SnProcess proc;
        CHECK(sn_process_spawn(&desc, &proc), "spawn echo");

        const char *payload = "roundtrip payload";
        size_t length = strlen(payload);
        CHECK(sn_pipe_write(&in_write, payload, (uint64_t)length) == (int64_t)length, "write "
                                                                                      "stdin");
        sn_pipe_close(&in_write);

        char buffer[128];
        CHECK(read_all(&out_read, buffer, sizeof(buffer)) >= 0, "read echo");
        CHECK(strcmp(buffer, payload) == 0, "echo content");

        int code = -1;
        CHECK(sn_process_wait(&proc, SN_TIMEOUT_INFINITE, &code) == SN_PROCESS_EXITED, "wait echo");
        CHECK(code == 0, "echo exit code");
        sn_process_close(&proc);
        sn_pipe_close(&out_read);
    }

    {
        SnPipe err_read, err_write;
        CHECK(sn_pipe_create(&err_read, &err_write), "create stderr pipe");

        const char *child_argv[] = {helper, "stderr-exit", "oops", "5", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;
        desc.stderr_stdio.kind = SN_PROCESS_STDIO_PIPE;
        desc.stderr_stdio.pipe = &err_write;

        SnProcess proc;
        CHECK(sn_process_spawn(&desc, &proc), "spawn stderr");

        char buffer[64];
        CHECK(read_all(&err_read, buffer, sizeof(buffer)) >= 0, "read stderr");
        CHECK(strcmp(buffer, "oops") == 0, "stderr content");

        int code = -1;
        CHECK(sn_process_wait(&proc, SN_TIMEOUT_INFINITE, &code) == SN_PROCESS_EXITED, "wait "
                                                                                       "stderr");
        CHECK(code == 5, "stderr exit code");
        sn_process_close(&proc);
        sn_pipe_close(&err_read);
    }

    {
        SnPipe out_read, out_write;
        CHECK(sn_pipe_create(&out_read, &out_write), "create env stdout pipe");

        const char *child_argv[] = {helper, "env", "SN_PROCESS_TEST_VALUE", NULL};
        const char *envp[] = {"SN_PROCESS_TEST_VALUE=hello-env", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;
        desc.envp = envp;
        desc.stdout_stdio.kind = SN_PROCESS_STDIO_PIPE;
        desc.stdout_stdio.pipe = &out_write;

        SnProcess proc;
        CHECK(sn_process_spawn(&desc, &proc), "spawn env");

        char buffer[64];
        CHECK(read_all(&out_read, buffer, sizeof(buffer)) >= 0, "read env");
        CHECK(strcmp(buffer, "hello-env") == 0, "env content");

        CHECK(sn_process_wait(&proc, SN_TIMEOUT_INFINITE, NULL) == SN_PROCESS_EXITED, "wait env");
        sn_process_close(&proc);
        sn_pipe_close(&out_read);
    }

    {
        char cwd[4096];
        CHECK(test_getcwd(cwd, sizeof(cwd)) != NULL, "getcwd");

        SnPipe out_read, out_write;
        CHECK(sn_pipe_create(&out_read, &out_write), "create cwd stdout pipe");

        const char *child_argv[] = {helper, "cwd", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;
        desc.cwd = cwd;
        desc.stdout_stdio.kind = SN_PROCESS_STDIO_PIPE;
        desc.stdout_stdio.pipe = &out_write;

        SnProcess proc;
        CHECK(sn_process_spawn(&desc, &proc), "spawn cwd");

        char buffer[4096];
        CHECK(read_all(&out_read, buffer, sizeof(buffer)) >= 0, "read cwd");
        CHECK(strcmp(buffer, cwd) == 0, "cwd content");

        CHECK(sn_process_wait(&proc, SN_TIMEOUT_INFINITE, NULL) == SN_PROCESS_EXITED, "wait cwd");
        sn_process_close(&proc);
        sn_pipe_close(&out_read);
    }

    {
        const char *path = "sn_process_test_out.txt";

        const char *child_argv[] = {helper, "print", "file", "output", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;
        desc.stdout_stdio.kind = SN_PROCESS_STDIO_FILE;
        desc.stdout_stdio.path = path;

        SnProcess proc;
        CHECK(sn_process_spawn(&desc, &proc), "spawn file");
        CHECK(sn_process_wait(&proc, SN_TIMEOUT_INFINITE, NULL) == SN_PROCESS_EXITED, "wait file");
        sn_process_close(&proc);

        FILE *file = fopen(path, "rb");
        CHECK(file != NULL, "open file output");
        char buffer[64] = {0};
        size_t count = fread(buffer, 1, sizeof(buffer) - 1, file);
        buffer[count] = '\0';
        fclose(file);
        remove(path);

        strip_cr(buffer);
        CHECK(strcmp(buffer, "file output\n") == 0, "file content");
    }

    {
        const char *child_argv[] = {helper, "exit", "0", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;

        SnProcess proc;
        CHECK(sn_process_spawn(&desc, &proc), "spawn valid");
        CHECK(sn_process_wait(&proc, SN_TIMEOUT_INFINITE, NULL) == SN_PROCESS_EXITED, "wait valid");
        sn_process_close(&proc);
    }

    {
        const char *child_argv[] = {"sn_process_program_that_does_not_exist", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;

        SnProcess proc;
        CHECK(!sn_process_spawn(&desc, &proc), "spawn missing program fails");
    }

    {
        const char *child_argv[] = {helper, "exit", "0", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;
        desc.cwd = "sn_process_directory_that_does_not_exist";

        SnProcess proc;
        CHECK(!sn_process_spawn(&desc, &proc), "spawn bad cwd fails");
    }

    {
        const char *child_argv[] = {helper, "sleep", "5000", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;

        SnProcess proc;
        CHECK(sn_process_spawn(&desc, &proc), "spawn sleep");
        CHECK(sn_process_get_id(&proc) > 0, "process id");
        CHECK(sn_process_is_alive(&proc), "is_alive running");

        int code = -1;
        CHECK(sn_process_wait(&proc, 50, &code) == SN_PROCESS_TIMEOUT, "wait timeout");
        CHECK(sn_process_is_alive(&proc), "is_alive after timeout");

        CHECK(sn_process_kill(&proc, true), "kill");
        CHECK(sn_process_wait(&proc, SN_TIMEOUT_INFINITE, &code) == SN_PROCESS_EXITED, "wait after "
                                                                                       "kill");
        CHECK(!sn_process_is_alive(&proc), "is_alive after kill");
        sn_process_close(&proc);
    }

    {
        const char *child_argv[] = {helper, "exit", "0", NULL};
        SnProcessDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.argv = child_argv;
        desc.detached = true;

        CHECK(sn_process_spawn(&desc, NULL), "spawn detached");
    }

    CHECK(sn_process_get_current_id() > 0, "current id");
    CHECK(sn_process_get_parent_id() > 0, "parent id");

    printf("ALL TESTS PASSED\n");
    return 0;
}
