#pragma once

/**
 * @file pipe.h
 * @brief Byte pipe abstraction.
 */

#include "snprocess/api.h"

#include <sncore/defines.h>

/**
 * @def SN_TIMEOUT_INFINITE
 * @brief Timeout value meaning "wait indefinitely".
 *
 * Mirrors the value used by SnNetwork so that a translation unit including
 * both headers sees an identical definition.
 */
#ifndef SN_TIMEOUT_INFINITE
    #define SN_TIMEOUT_INFINITE UINT64_MAX
#endif

/**
 * @struct SnPipe
 * @brief Opaque handle for one end of a pipe.
 *
 * An end created by @ref sn_pipe_create is unidirectional (either the read or
 * the write end); an end created by @ref sn_pipe_create_duplex is both
 * readable and writable.
 */
typedef struct SnPipe {
    alignas(16) uint8_t data[32];
} SnPipe;

/**
 * @enum SnPipeWaitStatus
 * @brief Outcome of @ref sn_pipe_wait.
 */
typedef enum SnPipeWaitStatus {
    SN_PIPE_READABLE, /**< Data (or EOF) is available; a read will not block. */
    SN_PIPE_TIMEOUT, /**< Nothing became available within the timeout. */
    SN_PIPE_CLOSED, /**< The far end is gone; a read returns 0 (EOF). */
    SN_PIPE_ERROR, /**< A fatal error occurred. */
} SnPipeWaitStatus;

/**
 * @brief Create an anonymous, unidirectional byte pipe.
 *
 * Data written to @p write_end is read from @p read_end. Both ends are
 * created non-inheritable (POSIX close-on-exec; Windows non-inheritable
 * HANDLEs). @ref sn_process_spawn is responsible for making the child's ends
 * inheritable.
 *
 * @param read_end  Receives the read end.
 * @param write_end Receives the write end.
 *
 * @return true on success, false otherwise.
 */
SN_PROCESS_API bool sn_pipe_create(SnPipe *read_end, SnPipe *write_end);

/**
 * @brief Create a bidirectional pipe pair.
 *
 * @p a and @p b are connected; each end is both readable and writable and data
 * written to one end is read from the other.
 *
 * @param a Receives the first end.
 * @param b Receives the second end.
 *
 * @return true on success, false otherwise.
 */
SN_PROCESS_API bool sn_pipe_create_duplex(SnPipe *a, SnPipe *b);

/**
 * @brief Blocking read from a pipe end.
 *
 * @param pipe The pipe end to read from.
 * @param buf  The buffer to fill.
 * @param size The maximum number of bytes to read.
 *
 * @return The number of bytes read (> 0), 0 on end-of-file (all write ends
 *         closed), or -1 on error.
 *
 * @note A read may return fewer bytes than requested; callers loop for a full
 *       message.
 */
SN_PROCESS_API int64_t sn_pipe_read(SnPipe *pipe, void *buf, uint64_t size);

/**
 * @brief Blocking write to a pipe end.
 *
 * @param pipe The pipe end to write to.
 * @param buf  The bytes to write.
 * @param size The number of bytes to write.
 *
 * @return The number of bytes written (may be fewer than @p size), or -1 on
 *         error, including a broken pipe.
 */
SN_PROCESS_API int64_t sn_pipe_write(SnPipe *pipe, const void *buf, uint64_t size);

/**
 * @brief Wait until a pipe end is readable, or a deadline elapses.
 *
 * @param pipe       The pipe end to wait on.
 * @param timeout_ms Milliseconds to wait; 0 polls without blocking,
 *                   @ref SN_TIMEOUT_INFINITE waits indefinitely.
 *
 * @return @ref SN_PIPE_READABLE if a read will not block, @ref SN_PIPE_CLOSED
 *         if the far end is gone, @ref SN_PIPE_TIMEOUT if nothing happened
 *         within the timeout, or @ref SN_PIPE_ERROR on failure.
 */
SN_PROCESS_API SnPipeWaitStatus sn_pipe_wait(const SnPipe *pipe, uint64_t timeout_ms);

/**
 * @brief Close a pipe end.
 *
 * @param pipe The pipe end to close.
 *
 * @note Safe to call on an already-closed end.
 */
SN_PROCESS_API void sn_pipe_close(SnPipe *pipe);
