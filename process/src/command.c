#include <signal.h>
#include <snprocess/command.h>
#include <sntime/sntime.h>
#include <string.h>

static uint64_t remaining_ms(uint64_t deadline) {
    uint64_t now = (uint64_t)sn_time_now_ms();
    return now >= deadline ? 0 : deadline - now;
}

bool sn_process_spawn_shell(const char *command, SnProcess *out) {
    if (!command || !out) return false;

#if defined(SN_OS_WINDOWS)
    const char *const argv[] = {"cmd.exe", "/c", command, NULL};
#else
    const char *const argv[] = {"/bin/sh", "-c", command, NULL};
#endif

    SnProcessDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.argv = argv;

    return sn_process_spawn(&desc, out);
}

SnProcessWaitStatus sn_process_run(const SnProcessRunDesc *desc, int *exit_code) {
    if (exit_code) *exit_code = 0;
    if (!desc || !desc->argv || !desc->argv[0]) return SN_PROCESS_ERROR;
    if (desc->stdin_size > 0 && !desc->stdin_data) return SN_PROCESS_ERROR;

    bool have_in = desc->stdin_size > 0;

    SnPipe out_read, out_write, err_read, err_write, in_read, in_write;
    if (!sn_pipe_create(&out_read, &out_write)) return SN_PROCESS_ERROR;
    if (!sn_pipe_create(&err_read, &err_write)) {
        sn_pipe_close(&out_read);
        sn_pipe_close(&out_write);
        return SN_PROCESS_ERROR;
    }
    if (have_in && !sn_pipe_create(&in_read, &in_write)) {
        sn_pipe_close(&out_read);
        sn_pipe_close(&out_write);
        sn_pipe_close(&err_read);
        sn_pipe_close(&err_write);
        return SN_PROCESS_ERROR;
    }

    SnProcessDesc spawn;
    memset(&spawn, 0, sizeof(spawn));
    spawn.argv = desc->argv;
    spawn.cwd = desc->cwd;
    spawn.envp = desc->envp;
    spawn.stdout_stdio.kind = SN_PROCESS_STDIO_PIPE;
    spawn.stdout_stdio.pipe = &out_write;
    spawn.stderr_stdio.kind = SN_PROCESS_STDIO_PIPE;
    spawn.stderr_stdio.pipe = &err_write;
    if (have_in) {
        spawn.stdin_stdio.kind = SN_PROCESS_STDIO_PIPE;
        spawn.stdin_stdio.pipe = &in_read;
    }

    SnProcess proc;
    if (!sn_process_spawn(&spawn, &proc)) {
        sn_pipe_close(&out_read);
        sn_pipe_close(&out_write);
        sn_pipe_close(&err_read);
        sn_pipe_close(&err_write);
        if (have_in) {
            sn_pipe_close(&in_read);
            sn_pipe_close(&in_write);
        }
        return SN_PROCESS_ERROR;
    }

    /* sn_process_spawn closed the child's ends (out_write, err_write, in_read);
       the parent keeps out_read, err_read and in_write. */

    bool infinite = desc->timeout_ms == SN_TIMEOUT_INFINITE;
    uint64_t deadline = 0;
    if (!infinite) {
        uint64_t delta = desc->timeout_ms > (uint64_t)INT64_MAX ? (uint64_t)INT64_MAX : desc->timeout_ms;
        deadline = (uint64_t)sn_time_now_ms() + delta;
    }

    if (have_in) {
        /* Writing to a pipe whose reader is gone raises SIGPIPE; ignore it for
           the duration so a child that exits early cannot kill the caller. */
#if !defined(SN_OS_WINDOWS)
        void (*previous)(int) = signal(SIGPIPE, SIG_IGN);
#endif

        const uint8_t *data = desc->stdin_data;
        uint64_t size = desc->stdin_size;
        while (size > 0) {
            int64_t written = sn_pipe_write(&in_write, data, size);
            if (written <= 0) break;
            data += written;
            size -= (uint64_t)written;
        }

#if !defined(SN_OS_WINDOWS)
        signal(SIGPIPE, previous);
#endif
        sn_pipe_close(&in_write);
    }

    bool out_open = true;
    bool err_open = true;
    bool timed_out = false;
    uint8_t buffer[4096];

    while (out_open || err_open) {
        bool idle = true;

        if (out_open) {
            SnPipeWaitStatus status = sn_pipe_wait(&out_read, 0);
            if (status == SN_PIPE_READABLE) {
                int64_t read = sn_pipe_read(&out_read, buffer, sizeof(buffer));
                if (read > 0) {
                    if (desc->on_stdout) desc->on_stdout(buffer, (uint64_t)read, desc->user);
                } else {
                    out_open = false;
                }
                idle = false;
            } else if (status != SN_PIPE_TIMEOUT) {
                out_open = false;
            }
        }

        if (err_open) {
            SnPipeWaitStatus status = sn_pipe_wait(&err_read, 0);
            if (status == SN_PIPE_READABLE) {
                int64_t read = sn_pipe_read(&err_read, buffer, sizeof(buffer));
                if (read > 0) {
                    if (desc->on_stderr) desc->on_stderr(buffer, (uint64_t)read, desc->user);
                } else {
                    err_open = false;
                }
                idle = false;
            } else if (status != SN_PIPE_TIMEOUT) {
                err_open = false;
            }
        }

        if (!out_open && !err_open) break;

        if (idle) {
            if (!infinite && remaining_ms(deadline) == 0) {
                timed_out = true;
                break;
            }
            if (out_open) sn_pipe_wait(&out_read, 10);
            else if (err_open) sn_pipe_wait(&err_read, 10);
        }
    }

    sn_pipe_close(&out_read);
    sn_pipe_close(&err_read);

    if (timed_out) {
        sn_process_kill(&proc, true);
        sn_process_wait(&proc, 100, NULL);
        sn_process_close(&proc);
        return SN_PROCESS_TIMEOUT;
    }

    SnProcessWaitStatus result
        = sn_process_wait(&proc, infinite ? SN_TIMEOUT_INFINITE : remaining_ms(deadline), exit_code);
    if (result == SN_PROCESS_TIMEOUT) {
        sn_process_kill(&proc, true);
        sn_process_wait(&proc, 100, NULL);
    }

    sn_process_close(&proc);
    return result;
}
