#include <snprocess/pipe.h>

#if defined(SN_OS_WINDOWS)

    #include <sntime/sntime.h>
    #include <windows.h>

typedef struct SnPipeWin32 {
    HANDLE read;
    HANDLE write;
} SnPipeWin32;

SN_STATIC_ASSERT(sizeof(SnPipeWin32) <= sizeof(SnPipe), "SnPipe size is not large enough!");

    #define PIPE_IMPL(p) ((SnPipeWin32 *)(p))

/* A duplex end is two anonymous pipes crossed: each end reads from one and
   writes to the other. An end with a single direction leaves the unused side
   as INVALID_HANDLE_VALUE. */
static void set_end(SnPipe *pipe, HANDLE read, HANDLE write) {
    PIPE_IMPL(pipe)->read = read;
    PIPE_IMPL(pipe)->write = write;
}

bool sn_pipe_create(SnPipe *read_end, SnPipe *write_end) {
    if (!read_end || !write_end) return false;

    HANDLE read, write;
    if (!CreatePipe(&read, &write, NULL, 0)) return false;

    set_end(read_end, read, INVALID_HANDLE_VALUE);
    set_end(write_end, INVALID_HANDLE_VALUE, write);
    return true;
}

bool sn_pipe_create_duplex(SnPipe *a, SnPipe *b) {
    if (!a || !b) return false;

    HANDLE a_read, a_write, b_read, b_write;
    if (!CreatePipe(&a_read, &a_write, NULL, 0)) return false;
    if (!CreatePipe(&b_read, &b_write, NULL, 0)) {
        CloseHandle(a_read);
        CloseHandle(a_write);
        return false;
    }

    set_end(a, b_read, a_write);
    set_end(b, a_read, b_write);
    return true;
}

int64_t sn_pipe_read(SnPipe *pipe, void *buf, uint64_t size) {
    if (!pipe || !buf) return -1;

    HANDLE handle = PIPE_IMPL(pipe)->read;
    if (handle == INVALID_HANDLE_VALUE) return -1;
    if (size == 0) return 0;

    DWORD want = size > (uint64_t)MAXDWORD ? MAXDWORD : (DWORD)size;
    DWORD got = 0;
    if (!ReadFile(handle, buf, want, &got, NULL)) {
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    }
    return (int64_t)got;
}

int64_t sn_pipe_write(SnPipe *pipe, const void *buf, uint64_t size) {
    if (!pipe || !buf) return -1;

    HANDLE handle = PIPE_IMPL(pipe)->write;
    if (handle == INVALID_HANDLE_VALUE) return -1;
    if (size == 0) return 0;

    DWORD want = size > (uint64_t)MAXDWORD ? MAXDWORD : (DWORD)size;
    DWORD wrote = 0;
    if (!WriteFile(handle, buf, want, &wrote, NULL)) return -1;
    return (int64_t)wrote;
}

SnPipeWaitStatus sn_pipe_wait(const SnPipe *pipe, uint64_t timeout_ms) {
    if (!pipe) return SN_PIPE_ERROR;

    HANDLE handle = ((const SnPipeWin32 *)pipe)->read;
    if (handle == INVALID_HANDLE_VALUE) return SN_PIPE_ERROR;

    bool infinite = timeout_ms == SN_TIMEOUT_INFINITE;
    uint64_t deadline = 0;
    if (!infinite) {
        uint64_t delta = timeout_ms > (uint64_t)INT64_MAX ? (uint64_t)INT64_MAX : timeout_ms;
        deadline = (uint64_t)sn_time_now_ms() + delta;
    }

    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL)) {
            if (available > 0) return SN_PIPE_READABLE;
        } else {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
                return SN_PIPE_CLOSED;
            return SN_PIPE_ERROR;
        }

        if (!infinite && (uint64_t)sn_time_now_ms() >= deadline) return SN_PIPE_TIMEOUT;
        sn_time_sleep_ms(1);
    }
}

void sn_pipe_close(SnPipe *pipe) {
    if (!pipe) return;

    SnPipeWin32 *impl = PIPE_IMPL(pipe);
    if (impl->read != INVALID_HANDLE_VALUE) {
        CloseHandle(impl->read);
        impl->read = INVALID_HANDLE_VALUE;
    }
    if (impl->write != INVALID_HANDLE_VALUE) {
        CloseHandle(impl->write);
        impl->write = INVALID_HANDLE_VALUE;
    }
}

#endif
