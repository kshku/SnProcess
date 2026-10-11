#pragma once

/**
 * @file process.h
 * @brief Process creation and supervision.
 */

#include "snprocess/api.h"
#include "snprocess/pipe.h"

#include <sncore/defines.h>

/**
 * @struct SnProcess
 * @brief Opaque handle for a spawned process.
 */
typedef struct SnProcess {
    alignas(16) uint8_t data[64];
} SnProcess;

/**
 * @enum SnProcessStdioKind
 * @brief How one of a child's standard streams is wired.
 */
typedef enum SnProcessStdioKind {
    SN_PROCESS_STDIO_INHERIT, /**< Inherit the parent's stream. */
    SN_PROCESS_STDIO_PIPE, /**< Use the pipe named in @ref SnProcessStdio::pipe. */
    SN_PROCESS_STDIO_FILE, /**< Use the path named in @ref SnProcessStdio::path. */
    SN_PROCESS_STDIO_NULL, /**< Discard on write; empty on read. */
} SnProcessStdioKind;

/**
 * @struct SnProcessStdio
 * @brief Description of one standard stream of a child process.
 */
typedef struct SnProcessStdio {
    SnProcessStdioKind kind; /**< Which wiring to use. */
    SnPipe *pipe; /**< The pipe, when @ref kind is @ref SN_PROCESS_STDIO_PIPE. */
    const char *path; /**< The path, when @ref kind is @ref SN_PROCESS_STDIO_FILE. */
} SnProcessStdio;

/**
 * @struct SnProcessDesc
 * @brief Description of a process to spawn.
 */
typedef struct SnProcessDesc {
    const char *const *argv; /**< argv[0] is the program, PATH-searched. Required. */
    const char *cwd; /**< Working directory, or NULL to inherit. */
    const char *const *envp; /**< Environment, or NULL to inherit. */
    SnProcessStdio stdin_stdio; /**< Child's standard input. */
    SnProcessStdio stdout_stdio; /**< Child's standard output. */
    SnProcessStdio stderr_stdio; /**< Child's standard error. */
    bool detached; /**< If true, the child is detached from the parent's session. */
} SnProcessDesc;

/**
 * @brief Spawn a child process.
 *
 * The child is placed in its own process group (POSIX) or job object
 * (Windows) so that @ref sn_process_kill can terminate its whole tree.
 *
 * On POSIX the child is created with posix_spawn; on Windows with
 * CreateProcess. Only the standard streams named in @p desc are inherited by
 * the child; every other handle of the parent is closed on exec in the child.
 *
 * If @p desc->detached is true the child is placed in a new session (POSIX) or
 * process group (Windows) so it is not tied to the parent's session, and @p out
 * may be NULL to fire and forget. A child for which no handle is retained is
 * not reaped by the library; on POSIX it becomes a zombie when it exits unless
 * the process ignores @c SIGCHLD.
 *
 * For a @ref SN_PROCESS_STDIO_PIPE stream the pipe end named in the description
 * is the child's end; it is closed on a successful spawn and must not be used
 * by the caller afterwards. The end returned to the caller (the other half of
 * the pipe) stays open.
 *
 * @param desc The description of the process to spawn.
 * @param out  Receives the handle, or NULL for a fire-and-forget spawn.
 *
 * @return true on success, false otherwise.
 */
SN_PROCESS_API bool sn_process_spawn(const SnProcessDesc *desc, SnProcess *out);

/**
 * @enum SnProcessWaitStatus
 * @brief Outcome of @ref sn_process_wait.
 */
typedef enum SnProcessWaitStatus {
    SN_PROCESS_EXITED, /**< Exited and was reaped; the exit code is valid. */
    SN_PROCESS_TIMEOUT, /**< Still running after the timeout. */
    SN_PROCESS_ERROR, /**< The wait failed. */
} SnProcessWaitStatus;

/**
 * @brief Wait for a child to exit, reaping it if it has.
 *
 * @param proc      The process handle.
 * @param timeout_ms Milliseconds to wait; 0 polls without blocking and
 *                  @ref SN_TIMEOUT_INFINITE waits indefinitely.
 * @param exit_code Receives the exit code, or NULL. Set only on
 *                  @ref SN_PROCESS_EXITED.
 *
 * @return @ref SN_PROCESS_EXITED once the child has exited (its exit code is
 *         then cached, so later waits return immediately with the same code),
 *         @ref SN_PROCESS_TIMEOUT if it is still running, or
 *         @ref SN_PROCESS_ERROR on failure.
 *
 * @note Exits the whole group/job when the child was killed.
 */
SN_PROCESS_API SnProcessWaitStatus sn_process_wait(SnProcess *proc, uint64_t timeout_ms, int *exit_code);

/**
 * @brief Query whether a child is still running.
 *
 * @param proc The process handle.
 *
 * @return true while the child is running, false once it has exited or on
 *         error.
 *
 * @note If the child has already exited this reaps it and caches its exit code,
 *       which a later @ref sn_process_wait then reports.
 */
SN_PROCESS_API bool sn_process_is_alive(SnProcess *proc);

/**
 * @enum SnProcessSignal
 * @brief Portable signal subset for @ref sn_process_signal.
 */
typedef enum SnProcessSignal {
    SN_PROCESS_SIGNAL_TERMINATE, /**< Request a clean termination (POSIX SIGTERM). */
    SN_PROCESS_SIGNAL_INTERRUPT, /**< Request interruption (POSIX SIGINT). */
    SN_PROCESS_SIGNAL_KILL, /**< Force termination (POSIX SIGKILL). */
} SnProcessSignal;

/**
 * @brief Terminate the child and its whole tree.
 *
 * @param proc  The process handle.
 * @param force false requests a clean termination, true forces it.
 *
 * @return true on success, false otherwise.
 *
 * @note This does not reap the child; call @ref sn_process_wait afterwards.
 * @note On Windows the clean and forced variants both use TerminateJobObject,
 *       which has no graceful stage.
 */
SN_PROCESS_API bool sn_process_kill(SnProcess *proc, bool force);

/**
 * @brief Send a portable signal to the child and its whole tree.
 *
 * @param proc The process handle.
 * @param sig  The signal to send.
 *
 * @return true on success, false otherwise.
 *
 * @note On Windows only @ref SN_PROCESS_SIGNAL_KILL (and, for a console child,
 *       @ref SN_PROCESS_SIGNAL_INTERRUPT) can be delivered faithfully.
 */
SN_PROCESS_API bool sn_process_signal(SnProcess *proc, SnProcessSignal sig);

/**
 * @brief Release a process handle.
 *
 * @param proc The process handle.
 *
 * @note This does not kill the child. On POSIX a still-running, never-reaped
 *       child becomes a zombie once this handle is released; kill and wait
 *       first.
 */
SN_PROCESS_API void sn_process_close(SnProcess *proc);

/**
 * @brief Get the operating-system identifier of a child.
 *
 * @param proc The process handle.
 *
 * @return The process id, or -1 if the handle is not running.
 */
SN_PROCESS_API int64_t sn_process_get_id(const SnProcess *proc);

/**
 * @brief Get the operating-system identifier of the current process.
 *
 * @return The process id.
 */
SN_PROCESS_API int64_t sn_process_get_current_id(void);

/**
 * @brief Get the operating-system identifier of the parent process.
 *
 * @return The parent process id, or -1 if unavailable.
 */
SN_PROCESS_API int64_t sn_process_get_parent_id(void);

/**
 * @brief Terminate the current process immediately.
 *
 * @param code The exit code to report.
 *
 * @note This does not return.
 */
SN_PROCESS_API void sn_process_exit(int code);
