/* A tiny child program driven by the SnProcess tests. It is intentionally
   dependency free and uses only the C standard library plus the platform's
   directory and sleep calls, so it builds and runs on every CI platform. */
#if !defined(_WIN32)
    #define _GNU_SOURCE
#endif

#include <sntime/sntime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #include <direct.h>

    #define helper_getcwd _getcwd
#else
    #include <unistd.h>

    #define helper_getcwd getcwd
#endif

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: helper <mode> [args]\n");
        return 2;
    }

    const char *mode = argv[1];

    if (strcmp(mode, "echo") == 0) {
        char buffer[4096];
        size_t count;
        while ((count = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
            if (fwrite(buffer, 1, count, stdout) != count) return 1;
        }
        return 0;
    }

    if (strcmp(mode, "exit") == 0) {
        if (argc < 3) return 2;
        return atoi(argv[2]);
    }

    if (strcmp(mode, "sleep") == 0) {
        if (argc < 3) return 2;
        SnTimeMs ms = (SnTimeMs)strtoul(argv[2], NULL, 10);
        sn_time_sleep_ms(ms);
        return 0;
    }

    if (strcmp(mode, "print") == 0) {
        for (int i = 2; i < argc; ++i) {
            if (i > 2) fputc(' ', stdout);
            fputs(argv[i], stdout);
        }
        fputc('\n', stdout);
        return 0;
    }

    if (strcmp(mode, "env") == 0) {
        if (argc < 3) return 2;
        const char *value = getenv(argv[2]);
        if (value) fputs(value, stdout);
        return 0;
    }

    if (strcmp(mode, "cwd") == 0) {
        char buffer[4096];
        if (helper_getcwd(buffer, sizeof(buffer))) fputs(buffer, stdout);
        return 0;
    }

    if (strcmp(mode, "stderr-exit") == 0) {
        if (argc < 4) return 2;
        fputs(argv[2], stderr);
        return atoi(argv[3]);
    }

    fprintf(stderr, "unknown mode: %s\n", mode);
    return 2;
}
