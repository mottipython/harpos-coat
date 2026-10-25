/*
 * fuzz_sound_setting.c - Standalone fuzz target for settings.txt parser.
 * Dual-mode: LLVM libFuzzer (-DLIBFUZZER) or standalone runner.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Safe line-by-line sound setting parser */
static int parse_sound_setting_memory(const char *buf, size_t len)
{
    int on = 1; /* Default is sound ON */
    size_t i = 0;

    while (i < len) {
        char line[128];
        size_t line_len = 0;

        /* Extract one line up to newline or buffer limit */
        while (i < len && buf[i] != '\n' && buf[i] != '\r') {
            if (line_len < sizeof(line) - 1) {
                line[line_len++] = buf[i];
            }
            i++;
        }
        line[line_len] = '\0';

        /* Skip trailing newline characters */
        while (i < len && (buf[i] == '\n' || buf[i] == '\r')) {
            i++;
        }

        /* Check for "sound=" prefix */
        if (!strncmp(line, "sound=", 6)) {
            if (line[6] == '0') {
                on = 0;
            } else if (line[6] == '1') {
                on = 1;
            } else if (!strcmp(line + 6, "off") || !strcmp(line + 6, "false") || !strcmp(line + 6, "no")) {
                on = 0;
            } else if (!strcmp(line + 6, "on") || !strcmp(line + 6, "true") || !strcmp(line + 6, "yes")) {
                on = 1;
            }
        }
    }

    return on;
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size == 0 || Size > 65536) return 0;

    parse_sound_setting_memory((const char *)Data, Size);
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
        /* Built-in test corpus */
        const char *tests[] = {
            "sound=0\n",
            "sound=1\n",
            "sound=off\n",
            "sound=on\n",
            "sound=\n",
            "sound=00000000\n",
            "# Comment line\nsound=0\nother=value\n",
            "sound=false\n",
            "sound=true\n",
            "\n\n\nsound=1\n\n",
            "sound=random_garbage_1234567890\n"
        };
        for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
            LLVMFuzzerTestOneInput((const uint8_t *)tests[i], strlen(tests[i]));
        }
    }
    return 0;
}
#endif
