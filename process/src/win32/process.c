#include <snprocess/process.h>

#if defined(SN_OS_WINDOWS)

// clang-format off
    #include <windows.h>
    #include <tlhelp32.h>
    // clang-format on
    #include <sncore/utf.h>
    #include <stdlib.h>
    #include <string.h>
    #include <wchar.h>

typedef struct SnProcessWin32 {
    HANDLE process;
    HANDLE job;
    DWORD pid;
    int exit_code;
    bool reaped;
} SnProcessWin32;

typedef struct SnPipeWin32 {
    HANDLE read;
    HANDLE write;
} SnPipeWin32;

SN_STATIC_ASSERT(sizeof(SnProcessWin32) <= sizeof(SnProcess), "SnProcess size is not large "
                                                              "enough!");

    #define PROC_IMPL(p) ((SnProcessWin32 *)(p))
    #define PIPE_IMPL(p) ((const SnPipeWin32 *)(p))

/* A wide string builder, used for the command line, the environment block and
   every UTF-8 to UTF-16 conversion. */
typedef struct WideBuf {
    wchar_t *data;
    size_t len;
    size_t cap;
} WideBuf;

static bool wb_reserve(WideBuf *wb, size_t extra) {
    if (wb->len + extra + 1 <= wb->cap) return true;

    size_t cap = wb->cap ? wb->cap : 64;
    while (cap < wb->len + extra + 1) cap *= 2;

    wchar_t *data = realloc(wb->data, cap * sizeof(wchar_t));
    if (!data) return false;

    wb->data = data;
    wb->cap = cap;
    return true;
}

static bool wb_append(WideBuf *wb, const wchar_t *str, size_t count) {
    if (!wb_reserve(wb, count)) return false;

    memcpy(wb->data + wb->len, str, count * sizeof(wchar_t));
    wb->len += count;
    wb->data[wb->len] = L'\0';
    return true;
}

static bool wb_put(WideBuf *wb, wchar_t ch) {
    return wb_append(wb, &ch, 1);
}

/* Appends one argument, quoted the way CommandLineToArgvW parses it back. */
static bool wb_append_arg(WideBuf *wb, const wchar_t *arg) {
    bool quote = *arg == L'\0';
    for (const wchar_t *p = arg; *p && !quote; ++p) {
        if (*p == L' ' || *p == L'\t' || *p == L'"') quote = true;
    }

    if (!quote) return wb_append(wb, arg, wcslen(arg));

    if (!wb_put(wb, L'"')) return false;

    size_t backslashes = 0;
    for (const wchar_t *p = arg;; ++p) {
        wchar_t ch = *p;

        if (ch == L'\\') {
            backslashes++;
            continue;
        }

        /* A run of backslashes before a quote or the end is doubled, so the
           literal backslashes survive; a quote before the end is escaped. */
        if (ch == L'"' || ch == L'\0') {
            size_t count = backslashes * 2 + (ch == L'"' ? 1 : 0);
            for (size_t i = 0; i < count; ++i) {
                if (!wb_put(wb, L'\\')) return false;
            }
            backslashes = 0;

            if (ch == L'"') {
                if (!wb_put(wb, L'"')) return false;
            } else {
                break;
            }
        } else {
            for (size_t i = 0; i < backslashes; ++i) {
                if (!wb_put(wb, L'\\')) return false;
            }
            backslashes = 0;
            if (!wb_put(wb, ch)) return false;
        }
    }

    return wb_put(wb, L'"');
}

static wchar_t *utf8_to_wide(const char *utf8) {
    size_t count = sn_utf8_to_utf16(utf8, NULL, 0);
    if (count == (size_t)-1) return NULL;

    wchar_t *wide = malloc((count + 1) * sizeof(wchar_t));
    if (!wide) return NULL;
    if (sn_utf8_to_utf16(utf8, wide, count + 1) == (size_t)-1) {
        free(wide);
        return NULL;
    }
    return wide;
}

static wchar_t *build_command_line(const char *const *argv) {
    WideBuf wb = {0};

    for (size_t i = 0; argv[i]; ++i) {
        wchar_t *arg = utf8_to_wide(argv[i]);
        if (!arg) {
            free(wb.data);
            return NULL;
        }

        bool ok = (i == 0 || wb_put(&wb, L' ')) && wb_append_arg(&wb, arg);
        free(arg);
        if (!ok) {
            free(wb.data);
            return NULL;
        }
    }

    if (!wb.data && !wb_put(&wb, L'\0')) return NULL;
    return wb.data;
}

static wchar_t *build_environment(const char *const *envp) {
    WideBuf wb = {0};

    for (size_t i = 0; envp[i]; ++i) {
        wchar_t *entry = utf8_to_wide(envp[i]);
        if (!entry) {
            free(wb.data);
            return NULL;
        }

        size_t count = wcslen(entry);
        bool ok = wb_append(&wb, entry, count + 1);
        free(entry);
        if (!ok) {
            free(wb.data);
            return NULL;
        }
    }

    if (!wb_put(&wb, L'\0')) {
        free(wb.data);
        return NULL;
    }
    return wb.data;
}

static HANDLE open_temp(const wchar_t *path, DWORD access, DWORD creation) {
    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    return CreateFileW(
        path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, creation, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* Resolves one standard stream to a handle, flagging handles the caller has to
   close once the child owns them. */
static HANDLE resolve_stdio(const SnProcessStdio *stdio, DWORD kind, bool *temp) {
    bool input = kind == STD_INPUT_HANDLE;

    switch (stdio->kind) {
        case SN_PROCESS_STDIO_INHERIT:
            return GetStdHandle(kind);

        case SN_PROCESS_STDIO_PIPE:
            if (!stdio->pipe) return NULL;
            return input ? PIPE_IMPL(stdio->pipe)->read : PIPE_IMPL(stdio->pipe)->write;

        case SN_PROCESS_STDIO_FILE: {
            if (!stdio->path) return NULL;
            wchar_t *path = utf8_to_wide(stdio->path);
            if (!path) return NULL;
            HANDLE handle = open_temp(
                path, input ? GENERIC_READ : GENERIC_WRITE, input ? OPEN_EXISTING : CREATE_ALWAYS);
            free(path);
            if (handle == INVALID_HANDLE_VALUE) return NULL;
            *temp = true;
            return handle;
        }

        case SN_PROCESS_STDIO_NULL: {
            HANDLE handle = open_temp(L"NUL", input ? GENERIC_READ : GENERIC_WRITE, OPEN_EXISTING);
            if (handle == INVALID_HANDLE_VALUE) return NULL;
            *temp = true;
            return handle;
        }

        default:
            return NULL;
    }
}

static void close_child_end(const SnProcessStdio *stdio) {
    if (stdio->kind == SN_PROCESS_STDIO_PIPE && stdio->pipe) sn_pipe_close(stdio->pipe);
}

bool sn_process_spawn(const SnProcessDesc *desc, SnProcess *out) {
    if (!desc || !desc->argv || !desc->argv[0]) return false;
    if (!desc->detached && !out) return false;

    wchar_t *command = build_command_line(desc->argv);
    if (!command) return false;

    wchar_t *cwd = NULL;
    if (desc->cwd) {
        cwd = utf8_to_wide(desc->cwd);
        if (!cwd) {
            free(command);
            return false;
        }
    }

    wchar_t *env = NULL;
    if (desc->envp) {
        env = build_environment(desc->envp);
        if (!env) {
            free(cwd);
            free(command);
            return false;
        }
    }

    HANDLE streams[3];
    bool temp[3] = {false, false, false};
    streams[0] = resolve_stdio(&desc->stdin_stdio, STD_INPUT_HANDLE, &temp[0]);
    streams[1] = resolve_stdio(&desc->stdout_stdio, STD_OUTPUT_HANDLE, &temp[1]);
    streams[2] = resolve_stdio(&desc->stderr_stdio, STD_ERROR_HANDLE, &temp[2]);

    bool any_handle = streams[0] || streams[1] || streams[2];

    HANDLE inherit[3];
    SIZE_T inherit_count = 0;
    for (int i = 0; i < 3; ++i) {
        if (streams[i]) {
            SetHandleInformation(streams[i], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            inherit[inherit_count++] = streams[i];
        }
    }

    HANDLE job = NULL;
    if (!desc->detached) {
        job = CreateJobObjectW(NULL, NULL);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
            ZeroMemory(&info, sizeof(info));
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
                CloseHandle(job);
                job = NULL;
            }
        }
    }

    bool suspended = job != NULL;

    STARTUPINFOEXW startup;
    ZeroMemory(&startup, sizeof(startup));
    startup.StartupInfo.cb = sizeof(startup);

    LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
    if (any_handle) {
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = streams[0];
        startup.StartupInfo.hStdOutput = streams[1];
        startup.StartupInfo.hStdError = streams[2];

        SIZE_T size = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &size);
        attributes = malloc(size);
        if (!attributes || !InitializeProcThreadAttributeList(attributes, 1, 0, &size)
            || !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
                                          inherit_count * sizeof(HANDLE), NULL, NULL)) {
            if (attributes) {
                DeleteProcThreadAttributeList(attributes);
                free(attributes);
                attributes = NULL;
            }
            for (int i = 0; i < 3; ++i) {
                if (temp[i]) CloseHandle(streams[i]);
            }
            if (job) CloseHandle(job);
            free(env);
            free(cwd);
            free(command);
            return false;
        }

        startup.lpAttributeList = attributes;
    }

    DWORD creation = suspended ? CREATE_SUSPENDED : 0;
    if (desc->detached) creation |= DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    if (env) creation |= CREATE_UNICODE_ENVIRONMENT;

    PROCESS_INFORMATION proc;
    ZeroMemory(&proc, sizeof(proc));

    BOOL created
        = CreateProcessW(NULL, command, NULL, NULL, TRUE, creation, env, cwd, &startup.StartupInfo, &proc);

    if (attributes) {
        DeleteProcThreadAttributeList(attributes);
        free(attributes);
    }

    for (int i = 0; i < 3; ++i) {
        if (temp[i]) CloseHandle(streams[i]);
    }

    free(env);
    free(cwd);
    free(command);

    if (!created) {
        if (job) CloseHandle(job);
        return false;
    }

    if (suspended) {
        if (!AssignProcessToJobObject(job, proc.hProcess)) {
            CloseHandle(job);
            job = NULL;
        }
        ResumeThread(proc.hThread);
    }

    CloseHandle(proc.hThread);

    close_child_end(&desc->stdin_stdio);
    close_child_end(&desc->stdout_stdio);
    close_child_end(&desc->stderr_stdio);

    if (!out) {
        CloseHandle(proc.hProcess);
        return true;
    }

    SnProcessWin32 *impl = PROC_IMPL(out);
    impl->process = proc.hProcess;
    impl->job = job;
    impl->pid = proc.dwProcessId;
    impl->exit_code = 0;
    impl->reaped = false;
    return true;
}

SnProcessWaitStatus sn_process_wait(SnProcess *proc, uint64_t timeout_ms, int *exit_code) {
    if (!proc) return SN_PROCESS_ERROR;

    SnProcessWin32 *impl = PROC_IMPL(proc);
    if (impl->reaped) {
        if (exit_code) *exit_code = impl->exit_code;
        return SN_PROCESS_EXITED;
    }
    if (impl->process == INVALID_HANDLE_VALUE) return SN_PROCESS_ERROR;

    DWORD wait = (timeout_ms == SN_TIMEOUT_INFINITE || timeout_ms > (uint64_t)INFINITE) ? INFINITE : (DWORD)timeout_ms;
    DWORD result = WaitForSingleObject(impl->process, wait);
    if (result == WAIT_TIMEOUT) return SN_PROCESS_TIMEOUT;
    if (result != WAIT_OBJECT_0) return SN_PROCESS_ERROR;

    DWORD code = 0;
    if (!GetExitCodeProcess(impl->process, &code)) return SN_PROCESS_ERROR;

    impl->exit_code = (int)code;
    impl->reaped = true;
    if (exit_code) *exit_code = impl->exit_code;
    return SN_PROCESS_EXITED;
}

bool sn_process_is_alive(SnProcess *proc) {
    if (!proc) return false;

    SnProcessWin32 *impl = PROC_IMPL(proc);
    if (impl->reaped || impl->process == INVALID_HANDLE_VALUE) return false;

    DWORD result = WaitForSingleObject(impl->process, 0);
    if (result == WAIT_TIMEOUT) return true;

    if (result == WAIT_OBJECT_0) {
        DWORD code = 0;
        if (GetExitCodeProcess(impl->process, &code)) {
            impl->exit_code = (int)code;
            impl->reaped = true;
        }
    }
    return false;
}

bool sn_process_kill(SnProcess *proc, bool force) {
    SN_UNUSED(force);
    if (!proc) return false;

    SnProcessWin32 *impl = PROC_IMPL(proc);
    if (impl->reaped) return false;

    if (impl->job) return TerminateJobObject(impl->job, 1) != 0;
    if (impl->process == INVALID_HANDLE_VALUE) return false;
    return TerminateProcess(impl->process, 1) != 0;
}

bool sn_process_signal(SnProcess *proc, SnProcessSignal sig) {
    if (!proc) return false;

    SnProcessWin32 *impl = PROC_IMPL(proc);
    if (impl->reaped) return false;

    if (sig == SN_PROCESS_SIGNAL_INTERRUPT)
        return GenerateConsoleCtrlEvent(CTRL_C_EVENT, impl->pid) != 0;
    if (sig != SN_PROCESS_SIGNAL_TERMINATE && sig != SN_PROCESS_SIGNAL_KILL) return false;

    if (impl->job) return TerminateJobObject(impl->job, 1) != 0;
    if (impl->process == INVALID_HANDLE_VALUE) return false;
    return TerminateProcess(impl->process, 1) != 0;
}

void sn_process_close(SnProcess *proc) {
    if (!proc) return;

    SnProcessWin32 *impl = PROC_IMPL(proc);
    if (impl->process != INVALID_HANDLE_VALUE) CloseHandle(impl->process);
    if (impl->job) CloseHandle(impl->job);

    impl->process = INVALID_HANDLE_VALUE;
    impl->job = NULL;
    impl->pid = 0;
    impl->reaped = true;
}

int64_t sn_process_get_id(const SnProcess *proc) {
    if (!proc) return -1;

    const SnProcessWin32 *impl = (const SnProcessWin32 *)proc;
    if (impl->reaped || impl->pid == 0) return -1;
    return (int64_t)impl->pid;
}

int64_t sn_process_get_current_id(void) {
    return (int64_t)GetCurrentProcessId();
}

int64_t sn_process_get_parent_id(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;

    DWORD self = GetCurrentProcessId();
    PROCESSENTRY32W entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);

    DWORD parent = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == self) {
                parent = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return parent ? (int64_t)parent : -1;
}

void sn_process_exit(int code) {
    ExitProcess((UINT)code);
}

#endif
