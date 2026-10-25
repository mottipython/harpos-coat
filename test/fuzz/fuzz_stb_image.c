/*
 * fuzz_stb_image.c - Standalone fuzz target for in-memory PNG/JPEG decoding.
 * Dual-mode: LLVM libFuzzer (-DLIBFUZZER) or standalone runner.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size == 0 || Size > 10 * 1024 * 1024) return 0;

    int w = 0, h = 0, comp = 0;
    stbi_uc *px = stbi_load_from_memory((const stbi_uc *)Data, (int)Size, &w, &h, &comp, 4);
    if (px) {
        stbi_image_free(px);
    }
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
            if (sz > 0 && sz < 10 * 1024 * 1024) {
                uint8_t *data = malloc(sz);
                if (data && fread(data, 1, sz, f) == (size_t)sz) {
                    LLVMFuzzerTestOneInput(data, sz);
                }
                free(data);
            }
            fclose(f);
        }
    } else {
        /* Built-in minimal 1x1 PNG header smoke test */
        static const uint8_t minimal_png[] = {
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
            0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
            0x89, 0x00, 0x00, 0x00, 0x0a, 0x49, 0x44, 0x41,
            0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00,
            0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00,
            0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
            0x42, 0x60, 0x82
        };
        LLVMFuzzerTestOneInput(minimal_png, sizeof(minimal_png));
    }
    return 0;
}
#endif
