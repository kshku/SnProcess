#include <snprocess/pipe.h>

#if defined(SN_OS_LINUX) || defined(SN_OS_MAC)

    #include <errno.h>
    #include <fcntl.h>
    #include <limits.h>
    #include <poll.h>
    #include <sntime/sntime.h>
    #include <string.h>
    #include <sys/ioctl.h>
    #include <sys/socket.h>
    #include <unistd.h>

    #if defined(SN_OS_MAC)
        #include <sys/filio.h>
    #endif

typedef struct SnPipeNix {
    int fd;
} SnPipeNix;

SN_STATIC_ASSERT(sizeof(SnPipeNix) <= sizeof(SnPipe), "SnPipe size is not large enough!");

    #define PIPE_IMPL(p) ((SnPipeNix *)(p))

/* Both pipe() and socketpair() file descriptors are made close-on-exec so a
   spawned child never inherits a pipe end it was not explicitly handed. */
static void set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

bool sn_pipe_create(SnPipe *read_end, SnPipe *write_end) {
    if (!read_end || !write_end) return false;

    int fds[2];
    if (pipe(fds) != 0) return false;

    set_cloexec(fds[0]);
    set_cloexec(fds[1]);

    PIPE_IMPL(read_end)->fd = fds[0];
    PIPE_IMPL(write_end)->fd = fds[1];
    return true;
}

bool sn_pipe_create_duplex(SnPipe *a, SnPipe *b) {
    if (!a || !b) return false;

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return false;

    set_cloexec(fds[0]);
    set_cloexec(fds[1]);

    PIPE_IMPL(a)->fd = fds[0];
    PIPE_IMPL(b)->fd = fds[1];
    return true;
}

int64_t sn_pipe_read(SnPipe *pipe, void *buf, uint64_t size) {
    if (!pipe || !buf) return -1;

    int fd = PIPE_IMPL(pipe)->fd;
    if (fd < 0) return -1;
    if (size == 0) return 0;
    if (size > (uint64_t)INT64_MAX) size = (uint64_t)INT64_MAX;

    ssize_t ret;
    do {
        ret = read(fd, buf, (size_t)size);
    } while (ret < 0 && errno == EINTR);

    return ret < 0 ? -1 : (int64_t)ret;
}

int64_t sn_pipe_write(SnPipe *pipe, const void *buf, uint64_t size) {
    if (!pipe || !buf) return -1;

    int fd = PIPE_IMPL(pipe)->fd;
    if (fd < 0) return -1;
    if (size == 0) return 0;
    if (size > (uint64_t)INT64_MAX) size = (uint64_t)INT64_MAX;

    ssize_t ret;
    do {
        ret = write(fd, buf, (size_t)size);
    } while (ret < 0 && errno == EINTR);

    return ret < 0 ? -1 : (int64_t)ret;
}

SnPipeWaitStatus sn_pipe_wait(const SnPipe *pipe, uint64_t timeout_ms) {
    if (!pipe) return SN_PIPE_ERROR;

    int fd = ((const SnPipeNix *)pipe)->fd;
    if (fd < 0) return SN_PIPE_ERROR;

    bool infinite = timeout_ms == SN_TIMEOUT_INFINITE;
    uint64_t deadline = 0;
    if (!infinite) {
        uint64_t delta = timeout_ms > (uint64_t)INT64_MAX ? (uint64_t)INT64_MAX : timeout_ms;
        deadline = (uint64_t)sn_time_now_ms() + delta;
    }

    for (;;) {
        int wait;
        if (infinite) {
            wait = -1;
        } else {
            uint64_t now = (uint64_t)sn_time_now_ms();
            if (now >= deadline) {
                wait = 0;
            } else {
                uint64_t remaining = deadline - now;
                wait = remaining > (uint64_t)INT_MAX ? INT_MAX : (int)remaining;
            }
        }

        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret;
        do {
            ret = poll(&pfd, 1, wait);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) return SN_PIPE_ERROR;
        if (ret == 0) return SN_PIPE_TIMEOUT;

        if (pfd.revents & (POLLERR | POLLNVAL)) return SN_PIPE_ERROR;

        /* A peer close is reported for a socketpair end as readability, so a
           readable socket is probed with MSG_PEEK: a zero-length result means
           the far end sent FIN. A real pipe cannot be peeked, so it uses
           FIONREAD: readable with nothing buffered means end of file. POLLHUP
           without POLLIN is a drained pipe whose writer is gone. */
        if (pfd.revents & POLLIN) {
            int sock_type = 0;
            socklen_t type_size = (socklen_t)sizeof(sock_type);
            if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &sock_type, &type_size) == 0) {
                char probe;
                if (recv(fd, &probe, 1, MSG_PEEK) == 0) return SN_PIPE_CLOSED;
            } else {
                int available = 0;
                if (ioctl(fd, (unsigned long)FIONREAD, &available) == 0 && available == 0)
                    return SN_PIPE_CLOSED;
            }
            return SN_PIPE_READABLE;
        }
        if (pfd.revents & POLLHUP) return SN_PIPE_CLOSED;

        if (!infinite && (uint64_t)sn_time_now_ms() >= deadline) return SN_PIPE_TIMEOUT;
    }
}

void sn_pipe_close(SnPipe *pipe) {
    if (!pipe) return;

    SnPipeNix *impl = PIPE_IMPL(pipe);
    if (impl->fd >= 0) {
        close(impl->fd);
        impl->fd = -1;
    }
}

#endif
