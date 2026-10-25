/*
 * fuzz_cli.c - Standalone fuzz target for screensaver CLI argument parsing.
 * Dual-mode: LLVM libFuzzer (-DLIBFUZZER) or standalone runner.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_ARGS 64
#define MAX_ARG_LEN 256

/* Mocked option variables matching saver implementations */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static int opt_mute = 0;
static const char *opt_wav = NULL;
static int opt_clock = -1;
static double opt_time = 0.0;
static int opt_w = 640;
static int opt_h = 400;

static void reset_options(void)
{
    opt_window = 0;
    opt_speed = 1.0;
    opt_seed = 0;
    opt_snapshot = NULL;
    opt_mute = 0;
    opt_wav = NULL;
    opt_clock = -1;
    opt_time = 0.0;
    opt_w = 640;
    opt_h = 400;
}

/* Parse CLI options with safe bounds checking */
static int parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!a) continue;
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            /* settings dialog requested - do not open GUI in fuzzing */
            return 0;
        }
        else if (!strcmp(a, "--settings")) return 0;
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) {
            opt_speed = atof(argv[++i]);
            if (opt_speed < 0.01) opt_speed = 0.01;
            if (opt_speed > 100.0) opt_speed = 100.0;
        }
        else if (!strcmp(a, "--seed") && i + 1 < argc) {
            opt_seed = (uint64_t)strtoull(argv[++i], NULL, 10);
        }
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--mute")) opt_mute = 1;
        else if (!strcmp(a, "--wav") && i + 1 < argc) opt_wav = argv[++i];
        else if (!strcmp(a, "--clock") && i + 1 < argc) {
            int hh = 0, mm = 0, ss = 0;
            if (sscanf(argv[++i], "%d:%d:%d", &hh, &mm, &ss) < 2) return 1;
            if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return 1;
            opt_clock = hh * 3600 + mm * 60 + ss;
        }
        else if (!strcmp(a, "--time") && i + 1 < argc) {
            opt_time = atof(argv[++i]);
            if (opt_time < 0.0) opt_time = 0.0;
            if (opt_time > 86400.0) opt_time = 86400.0; /* Clamp extreme time */
        }
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *arg = argv[++i];
            const char *x = strchr(arg, 'x');
            int w = atoi(arg);
            int h = x ? atoi(x + 1) : 0;
            if (w < 64 || h < 64 || w > 8192 || h > 8192) return 1;
            opt_w = w; opt_h = h;
        }
        else {
            return 1; /* Unknown or invalid option */
        }
    }
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size == 0 || Size > 4096) return 0;

    char buf[4096];
    memcpy(buf, Data, Size);
    buf[Size < sizeof(buf) ? Size : sizeof(buf) - 1] = '\0';

    char *argv[MAX_ARGS];
    int argc = 0;
    argv[argc++] = "fuzz_target";

    /* Tokenize by space, tab, or newline */
    char *p = buf;
    while (*p && argc < MAX_ARGS - 1) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    argv[argc] = NULL;

    reset_options();
    parse_args(argc, argv);
    return 0;
}

#ifndef LIBFUZZER
int main(int argc, char **argv)
{
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            FILE *f = fopen(argv[i], "rb");
            if (!f) continue;
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0 && sz < 65536) {
                uint8_t *data = malloc(sz);
                if (data && fread(data, 1, sz, f) == (size_t)sz) {
                    LLVMFuzzerTestOneInput(data, sz);
                }
                free(data);
            }
            fclose(f);
        }
    } else {
        /* Built-in smoke corpus */
        const char *tests[] = {
            "--window --speed 2.5",
            "--size 1920x1080 --time 10.0",
            "--size 64x64 --mute",
            "--size 100000x100000",
            "--clock 12:30:00",
            "--clock 25:99:99",
            "--clock -1:00",
            "--time 999999999",
            "--seed 1234567890",
            "/s",
            "/p:12345",
            "/c",
            "--settings",
            "--wav /tmp/test.wav",
            "--unknown-flag",
            "--size invalid",
            "--speed",
            "--time",
            "--size"
        };
        for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
            LLVMFuzzerTestOneInput((const uint8_t *)tests[i], strlen(tests[i]));
        }
    }
    return 0;
}
#endif
