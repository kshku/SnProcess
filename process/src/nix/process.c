/* Feature test macro has to precede every libc header so that <spawn.h>
   exposes posix_spawn_file_actions_addchdir_np and POSIX_SPAWN_SETSID. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <snprocess/process.h>

#if defined(SN_OS_LINUX) || defined(SN_OS_MAC)

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sntime/sntime.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

typedef struct SnProcessNix {
    pid_t pid;
    int exit_code;
    bool reaped;
} SnProcessNix;

typedef struct SnPipeNix {
    int fd;
} SnPipeNix;

SN_STATIC_ASSERT(sizeof(SnProcessNix) <= sizeof(SnProcess), "SnProcess size is not large enough!");

#define PROC_IMPL(p) ((SnProcessNix *)(p))
#define PIPE_FD(p) (((const SnPipeNix *)(p))->fd)

static int normalize_status(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

/* Adds the file actions that wire one of the child's standard streams. FILE
   and NULL are handled with addopen directly in the child, so the parent holds
   no descriptor for them and has nothing to close afterwards. */
static bool add_stdio_action(posix_spawn_file_actions_t *actions, const SnProcessStdio *stdio, int child_fd) {
    switch (stdio->kind) {
        case SN_PROCESS_STDIO_INHERIT:
            return true;

        case SN_PROCESS_STDIO_PIPE: {
            if (!stdio->pipe) return false;
            int fd = PIPE_FD(stdio->pipe);
            if (fd < 0) return false;
            return posix_spawn_file_actions_adddup2(actions, fd, child_fd) == 0;
        }

        case SN_PROCESS_STDIO_FILE: {
            if (!stdio->path) return false;
            if (child_fd == STDIN_FILENO)
                return posix_spawn_file_actions_addopen(actions, child_fd, stdio->path, O_RDONLY, (mode_t)0) == 0;
            return posix_spawn_file_actions_addopen(
                       actions, child_fd, stdio->path, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)0644)
                   == 0;
        }

        case SN_PROCESS_STDIO_NULL: {
            int flags = (child_fd == STDIN_FILENO) ? O_RDONLY : O_WRONLY;
            return posix_spawn_file_actions_addopen(actions, child_fd, "/dev/null", flags, (mode_t)0) == 0;
        }

        default:
            return false;
    }
}

/* Once the child holds a duplicate of its end, the parent's copy is closed so
   the pipe reaches end-of-file when the child closes it. */
static void close_child_end(const SnProcessStdio *stdio) {
    if (stdio->kind == SN_PROCESS_STDIO_PIPE && stdio->pipe) sn_pipe_close(stdio->pipe);
}

bool sn_process_spawn(const SnProcessDesc *desc, SnProcess *out) {
    if (!desc || !desc->argv || !desc->argv[0]) return false;
    if (!desc->detached && !out) return false;

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return false;

    bool ok = add_stdio_action(&actions, &desc->stdin_stdio, STDIN_FILENO)
              && add_stdio_action(&actions, &desc->stdout_stdio, STDOUT_FILENO)
              && add_stdio_action(&actions, &desc->stderr_stdio, STDERR_FILENO);

    if (ok && desc->cwd) ok = posix_spawn_file_actions_addchdir_np(&actions, desc->cwd) == 0;

    posix_spawnattr_t attr;
    if (ok) ok = posix_spawnattr_init(&attr) == 0;

    if (ok) {
        int flag_value = desc->detached ? POSIX_SPAWN_SETSID : POSIX_SPAWN_SETPGROUP;
        ok = posix_spawnattr_setflags(&attr, (short)flag_value) == 0;
        if (ok && !desc->detached) ok = posix_spawnattr_setpgroup(&attr, 0) == 0;
    }

    const char *const *envp = desc->envp ? desc->envp : (const char *const *)environ;

    pid_t pid = -1;
    if (ok) {
        ok = posix_spawnp(&pid, desc->argv[0], &actions, &attr, (char *const *)desc->argv, (char *const *)envp) == 0;
    }

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    if (!ok) return false;

    close_child_end(&desc->stdin_stdio);
    close_child_end(&desc->stdout_stdio);
    close_child_end(&desc->stderr_stdio);

    if (desc->detached) return true;

    SnProcessNix *impl = PROC_IMPL(out);
    impl->pid = pid;
    impl->exit_code = 0;
    impl->reaped = false;
    return true;
}

SnProcessWaitStatus sn_process_wait(SnProcess *proc, uint64_t timeout_ms, int *exit_code) {
    if (!proc) return SN_PROCESS_ERROR;

    SnProcessNix *impl = PROC_IMPL(proc);
    if (impl->reaped) {
        if (exit_code) *exit_code = impl->exit_code;
        return SN_PROCESS_EXITED;
    }
    if (impl->pid <= 0) return SN_PROCESS_ERROR;

    if (timeout_ms == SN_TIMEOUT_INFINITE) {
        int status = 0;
        pid_t ret;
        do {
            ret = waitpid(impl->pid, &status, 0);
        } while (ret < 0 && errno == EINTR);

        if (ret != impl->pid) return SN_PROCESS_ERROR;

        impl->exit_code = normalize_status(status);
        impl->reaped = true;
        if (exit_code) *exit_code = impl->exit_code;
        return SN_PROCESS_EXITED;
    }

    uint64_t delta = timeout_ms > (uint64_t)INT64_MAX ? (uint64_t)INT64_MAX : timeout_ms;
    uint64_t deadline = (uint64_t)sn_time_now_ms() + delta;

    for (;;) {
        int status = 0;
        pid_t ret = waitpid(impl->pid, &status, WNOHANG);
        if (ret == impl->pid) {
            impl->exit_code = normalize_status(status);
            impl->reaped = true;
            if (exit_code) *exit_code = impl->exit_code;
            return SN_PROCESS_EXITED;
        }
        if (ret < 0 && errno != EINTR) return SN_PROCESS_ERROR;

        if ((uint64_t)sn_time_now_ms() >= deadline) return SN_PROCESS_TIMEOUT;
        sn_time_sleep_ms(1);
    }
}

bool sn_process_is_alive(SnProcess *proc) {
    if (!proc) return false;

    SnProcessNix *impl = PROC_IMPL(proc);
    if (impl->reaped || impl->pid <= 0) return false;

    int status = 0;
    pid_t ret = waitpid(impl->pid, &status, WNOHANG);
    if (ret == 0) return true;
    if (ret == impl->pid) {
        impl->exit_code = normalize_status(status);
        impl->reaped = true;
    }
    return false;
}

bool sn_process_kill(SnProcess *proc, bool force) {
    if (!proc) return false;

    SnProcessNix *impl = PROC_IMPL(proc);
    if (impl->reaped || impl->pid <= 0) return false;

    int sig = force ? SIGKILL : SIGTERM;
    if (kill(-impl->pid, sig) == 0) return true;
    return kill(impl->pid, sig) == 0;
}

bool sn_process_signal(SnProcess *proc, SnProcessSignal sig) {
    if (!proc) return false;

    SnProcessNix *impl = PROC_IMPL(proc);
    if (impl->reaped || impl->pid <= 0) return false;

    int native;
    switch (sig) {
        case SN_PROCESS_SIGNAL_TERMINATE:
            native = SIGTERM;
            break;
        case SN_PROCESS_SIGNAL_INTERRUPT:
            native = SIGINT;
            break;
        case SN_PROCESS_SIGNAL_KILL:
            native = SIGKILL;
            break;
        default:
            return false;
    }

    if (kill(-impl->pid, native) == 0) return true;
    return kill(impl->pid, native) == 0;
}

void sn_process_close(SnProcess *proc) {
    if (!proc) return;
    PROC_IMPL(proc)->pid = -1;
}

int64_t sn_process_get_id(const SnProcess *proc) {
    if (!proc) return -1;

    const SnProcessNix *impl = (const SnProcessNix *)proc;
    if (impl->reaped || impl->pid <= 0) return -1;
    return (int64_t)impl->pid;
}

int64_t sn_process_get_current_id(void) {
    return (int64_t)getpid();
}

int64_t sn_process_get_parent_id(void) {
    return (int64_t)getppid();
}

void sn_process_exit(int code) {
    _exit(code);
}

#endif
