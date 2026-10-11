#pragma once

/**
 * @file command.h
 * @brief Convenience helpers built on top of the public SnProcess API.
 *
 * Everything here is implemented in terms of the public pipe and process API;
 * no private platform hooks are used.
 */

#include "snprocess/api.h"
#include "snprocess/pipe.h"
#include "snprocess/process.h"

#include <sncore/defines.h>

/**
 * @typedef SnProcessOutputFn
 * @brief Receives one chunk of a child's output.
 *
 * @param data The bytes produced.
 * @param size The number of bytes.
 * @param user The user pointer from @ref SnProcessRunDesc::user.
 */
typedef void (*SnProcessOutputFn)(const void *data, uint64_t size, void *user);

/**
 * @struct SnProcessRunDesc
 * @brief Description for @ref sn_process_run.
 */
typedef struct SnProcessRunDesc {
    const char *const *argv; /**< argv[0] is the program, PATH-searched. Required. */
    const char *cwd; /**< Working directory, or NULL to inherit. */
    const char *const *envp; /**< Environment, or NULL to inherit. */
    const void *stdin_data; /**< Bytes written to the child's stdin, or NULL. */
    uint64_t stdin_size; /**< Number of bytes in @ref stdin_data. */
    SnProcessOutputFn on_stdout; /**< Called with stdout chunks, or NULL to discard. */
    SnProcessOutputFn on_stderr; /**< Called with stderr chunks, or NULL to discard. */
    void *user; /**< Passed through to the callbacks. */
    uint64_t timeout_ms; /**< Overall budget; @ref SN_TIMEOUT_INFINITE to wait forever. */
} SnProcessRunDesc;

/**
 * @brief Spawn a command through the platform shell.
 *
 * @param command The command line, passed to the child shell verbatim.
 * @param out     Receives the handle.
 *
 * @return true on success, false otherwise.
 *
 * @note Uses `/bin/sh -c` on POSIX and `cmd.exe /c` on Windows.
 */
SN_PROCESS_API bool sn_process_spawn_shell(const char *command, SnProcess *out);

/**
 * @brief Run a command to completion, streaming and capturing its output.
 *
 * Spawns the process with pipes on its standard streams, writes
 * @ref SnProcessRunDesc::stdin_data to its input, streams its output to
 * @ref SnProcessRunDesc::on_stdout and @ref SnProcessRunDesc::on_stderr, and
 * waits for it up to @ref SnProcessRunDesc::timeout_ms.
 *
 * @param desc      The description of the command to run.
 * @param exit_code Receives the exit code, or NULL.
 *
 * @return The result of @ref sn_process_wait: @ref SN_PROCESS_EXITED on
 *         completion, @ref SN_PROCESS_TIMEOUT if the budget elapsed, or
 *         @ref SN_PROCESS_ERROR on failure.
 */
SN_PROCESS_API SnProcessWaitStatus sn_process_run(const SnProcessRunDesc *desc, int *exit_code);
