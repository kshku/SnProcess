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

typedef struct Sink {
    char data[256];
    size_t length;
} Sink;

static void collect(const void *data, uint64_t size, void *user) {
    Sink *sink = user;
    if (sink->length + size + 1 > sizeof(sink->data)) return;
    memcpy(sink->data + sink->length, data, (size_t)size);
    sink->length += (size_t)size;
    sink->data[sink->length] = '\0';
}

static void strip_cr(char *text) {
    char *write = text;
    for (char *read = text; *read; ++read) {
        if (*read != '\r') *write++ = *read;
    }
    *write = '\0';
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("FAIL: helper path argument missing\n");
        return 1;
    }
    const char *helper = argv[1];

    {
        Sink sink;
        memset(&sink, 0, sizeof(sink));

        const char *child_argv[] = {helper, "print", "hello", NULL};
        SnProcessRunDesc run;
        memset(&run, 0, sizeof(run));
        run.argv = child_argv;
        run.on_stdout = collect;
        run.user = &sink;
        run.timeout_ms = SN_TIMEOUT_INFINITE;

        int code = -1;
        CHECK(sn_process_run(&run, &code) == SN_PROCESS_EXITED, "run exit");
        CHECK(code == 0, "run exit code");
        strip_cr(sink.data);
        CHECK(strcmp(sink.data, "hello\n") == 0, "run stdout");
    }

    {
        Sink sink;
        memset(&sink, 0, sizeof(sink));

        const char *payload = "piped input";
        const char *child_argv[] = {helper, "echo", NULL};
        SnProcessRunDesc run;
        memset(&run, 0, sizeof(run));
        run.argv = child_argv;
        run.stdin_data = payload;
        run.stdin_size = strlen(payload);
        run.on_stdout = collect;
        run.user = &sink;
        run.timeout_ms = SN_TIMEOUT_INFINITE;

        int code = -1;
        CHECK(sn_process_run(&run, &code) == SN_PROCESS_EXITED, "run stdin exit");
        CHECK(code == 0, "run stdin exit code");
        CHECK(strcmp(sink.data, payload) == 0, "run stdin echo");
    }

    {
        Sink sink;
        memset(&sink, 0, sizeof(sink));

        const char *child_argv[] = {helper, "stderr-exit", "bad", "4", NULL};
        SnProcessRunDesc run;
        memset(&run, 0, sizeof(run));
        run.argv = child_argv;
        run.on_stderr = collect;
        run.user = &sink;
        run.timeout_ms = SN_TIMEOUT_INFINITE;

        int code = -1;
        CHECK(sn_process_run(&run, &code) == SN_PROCESS_EXITED, "run stderr exit");
        CHECK(code == 4, "run stderr exit code");
        CHECK(strcmp(sink.data, "bad") == 0, "run stderr");
    }

    {
        const char *child_argv[] = {helper, "sleep", "5000", NULL};
        SnProcessRunDesc run;
        memset(&run, 0, sizeof(run));
        run.argv = child_argv;
        run.timeout_ms = 100;

        int code = -1;
        CHECK(sn_process_run(&run, &code) == SN_PROCESS_TIMEOUT, "run timeout");
    }

    {
        const char *child_argv[] = {"sn_process_program_that_does_not_exist", NULL};
        SnProcessRunDesc run;
        memset(&run, 0, sizeof(run));
        run.argv = child_argv;
        run.timeout_ms = SN_TIMEOUT_INFINITE;

        int code = -1;
        CHECK(sn_process_run(&run, &code) == SN_PROCESS_ERROR, "run missing program");
    }

    {
        SnProcess proc;
        CHECK(sn_process_spawn_shell("exit 0", &proc), "spawn shell");

        int code = -1;
        CHECK(sn_process_wait(&proc, SN_TIMEOUT_INFINITE, &code) == SN_PROCESS_EXITED, "wait "
                                                                                       "shell");
        CHECK(code == 0, "shell exit code");
        sn_process_close(&proc);
    }

    printf("ALL TESTS PASSED\n");
    return 0;
}
