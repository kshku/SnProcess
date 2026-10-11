# SnProcess

Process and pipe abstraction library written in C17.

Provides anonymous and bidirectional byte pipes, and process creation,
supervision (wait/reap, liveness, tree kill), stdio redirection, and signals
across platforms.

## API

### Pipe (`<snprocess/pipe.h>`)

| Function | Description |
|----------|-------------|
| `bool sn_pipe_create(SnPipe *read_end, SnPipe *write_end)` | Create an anonymous unidirectional pipe |
| `bool sn_pipe_create_duplex(SnPipe *a, SnPipe *b)` | Create a connected bidirectional pair |
| `int64_t sn_pipe_read(SnPipe *pipe, void *buf, uint64_t size)` | Read up to `size` bytes; 0 on EOF, -1 on error |
| `int64_t sn_pipe_write(SnPipe *pipe, const void *buf, uint64_t size)` | Write bytes; may write fewer; -1 on error |
| `SnPipeWaitStatus sn_pipe_wait(const SnPipe *pipe, uint64_t timeout_ms)` | Wait until readable, closed, or the deadline elapses |
| `void sn_pipe_close(SnPipe *pipe)` | Close an end (safe on an already-closed end) |

### Process (`<snprocess/process.h>`)

| Function | Description |
|----------|-------------|
| `bool sn_process_spawn(const SnProcessDesc *desc, SnProcess *out)` | Spawn a child; pass `NULL` for `out` to fire and forget |
| `SnProcessWaitStatus sn_process_wait(SnProcess *proc, uint64_t timeout_ms, int *exit_code)` | Wait for the child to exit and reap it |
| `bool sn_process_is_alive(SnProcess *proc)` | Query whether the child is still running |
| `bool sn_process_kill(SnProcess *proc, bool force)` | Terminate the child and its whole tree |
| `bool sn_process_signal(SnProcess *proc, SnProcessSignal sig)` | Send a portable signal to the child and its tree |
| `void sn_process_close(SnProcess *proc)` | Release the handle (does not kill the child) |
| `int64_t sn_process_get_id(const SnProcess *proc)` | Get a child's process id, or -1 |
| `int64_t sn_process_get_current_id(void)` | Get the current process id |
| `int64_t sn_process_get_parent_id(void)` | Get the parent process id, or -1 |
| `void sn_process_exit(int code)` | Terminate the current process immediately |

### Command (`<snprocess/command.h>`)

| Function | Description |
|----------|-------------|
| `bool sn_process_spawn_shell(const char *command, SnProcess *out)` | Spawn a command through the platform shell |
| `SnProcessWaitStatus sn_process_run(const SnProcessRunDesc *desc, int *exit_code)` | Run to completion, writing stdin and streaming stdout/stderr to callbacks |

### Types

```c
typedef struct SnProcessStdio {
    SnProcessStdioKind kind;  // inherit, pipe, file, or null
    SnPipe *pipe;             // used when kind is SN_PROCESS_STDIO_PIPE
    const char *path;         // used when kind is SN_PROCESS_STDIO_FILE
} SnProcessStdio;

typedef struct SnProcessDesc {
    const char *const *argv;   // argv[0] is the program, PATH-searched
    const char *cwd;           // working directory, or NULL to inherit
    const char *const *envp;   // environment, or NULL to inherit
    SnProcessStdio stdin_stdio;
    SnProcessStdio stdout_stdio;
    SnProcessStdio stderr_stdio;
    bool detached;
} SnProcessDesc;
```

## Usage

Create a bidirectional pipe and send a message through it:

```c
#include <snprocess/snprocess.h>
#include <stdio.h>

int main(void) {
    SnPipe a, b;
    if (!sn_pipe_create_duplex(&a, &b)) return 1;

    sn_pipe_write(&a, "ping", 4);

    char buf[16];
    int64_t n = sn_pipe_read(&b, buf, sizeof(buf));
    printf("received %d bytes: %.*s\n", (int)n, (int)n, buf);

    sn_pipe_close(&a);
    sn_pipe_close(&b);
    return 0;
}
```

Spawn a command through the shell, wait for it, and read its exit code:

```c
SnProcess proc;
if (sn_process_spawn_shell("echo hello", &proc)) {
    int code = -1;
    sn_process_wait(&proc, SN_TIMEOUT_INFINITE, &code);
    printf("exit code: %d\n", code);
    sn_process_close(&proc);
}
```

For the common "run a command and collect its output" case, `sn_process_run`
writes `stdin_data` and streams stdout/stderr to callbacks in one call:

```c
static void on_stdout(const void *data, uint64_t size, void *user) {
    fwrite(data, 1, (size_t)size, (FILE *)user);
}

/* ... */
const char *git_argv[] = {"git", "status", "--short", NULL};
SnProcessRunDesc run = {0};
run.argv = git_argv;
run.on_stdout = on_stdout;
run.user = stdout;
run.timeout_ms = SN_TIMEOUT_INFINITE;

int code;
if (sn_process_run(&run, &code) == SN_PROCESS_EXITED) {
    printf("git exited with %d\n", code);
}
```

## Adding to your project

```cmake
include(FetchContent)
FetchContent_Declare(snprocess
    GIT_REPOSITORY https://github.com/kshku/SnProcess.git
    GIT_TAG <tag>  # e.g., v0.1.0
)
FetchContent_MakeAvailable(snprocess)

target_link_libraries(myapp PRIVATE snprocess)
```

## Build

```sh
cmake -B build
cmake --build build
```

| Option | Default | Description |
|--------|---------|-------------|
| `SN_PROCESS_BUILD_SHARED` | `OFF` | Build as shared library |
| `SN_PROCESS_BUILD_TEST` | `OFF` | Build tests |

## Platform Support

| Platform | Backend |
|----------|---------|
| Linux | `posix_spawn`, `pipe`, `poll`, `ioctl(FIONREAD)` |
| macOS | `posix_spawn`, `pipe`, `poll`, `ioctl(FIONREAD)` |
| Windows | `CreateProcess`, `CreatePipe`, job objects for tree kill |

## Dependencies

- **SnCore** — fetched automatically via FetchContent
- **SnTime** — fetched automatically via FetchContent (deadline timing)
