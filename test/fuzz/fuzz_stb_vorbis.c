/*
 * fuzz_stb_vorbis.c - Standalone fuzz target for Vorbis audio decoding and resampling.
 * Dual-mode: LLVM libFuzzer (-DLIBFUZZER) or standalone runner.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

#define RATE 22050

typedef struct {
    float *s;
    int n;
} Snd;

static Snd snd_new(double secs)
{
    Snd s = {NULL, 0};
    if (secs <= 0.0 || secs > 600.0) secs = 0.01;
    s.n = (int)(secs * RATE);
    if (s.n <= 0) s.n = 1;
    s.s = (float *)calloc((size_t)s.n, sizeof *s.s);
    return s;
}

static Snd test_decode_pipeline(const unsigned char *data, size_t len)
{
    Snd s = {NULL, 0};
    int ch = 0, rate = 0;
    short *pcm = NULL;
    int n = stb_vorbis_decode_memory(data, (int)len, &ch, &rate, &pcm);
    if (n <= 0 || rate <= 0 || ch < 1) {
        if (pcm) free(pcm);
        return snd_new(0.01);
    }

    s = snd_new((double)n / rate);
    if (!s.s) {
        free(pcm);
        return s;
    }

    for (int i = 0; i < s.n; i++) {
        double src = (double)i * rate / RATE;
        int j = (int)src;
        if (j + 1 >= n) break;
        double fr = src - j;
        s.s[i] = (pcm[j * ch] * (1 - fr) + pcm[(j + 1) * ch] * fr) / 32768.0f;
    }
    free(pcm);
    return s;
}

static void test_channel_downmix(const unsigned char *data, size_t len)
{
    int ch = 0, rate = 0;
    short *pcm = NULL;
    int n = stb_vorbis_decode_memory(data, (int)len, &ch, &rate, &pcm);
    if (n <= 0 || ch < 1) {
        if (pcm) free(pcm);
        return;
    }

    short *mono = (short *)malloc(sizeof(short) * (size_t)n);
    if (!mono) {
        free(pcm);
        return;
    }

    for (int i = 0; i < n; i++) {
        mono[i] = pcm[i * ch];
    }
    free(mono);
    free(pcm);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size == 0 || Size > 16 * 1024 * 1024) return 0;

    /* Test both resampling pipeline and channel downmix */
    Snd s = test_decode_pipeline(Data, Size);
    if (s.s) free(s.s);

    test_channel_downmix(Data, Size);

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
            if (sz > 0 && sz < 16 * 1024 * 1024) {
                uint8_t *data = malloc(sz);
                if (data && fread(data, 1, sz, f) == (size_t)sz) {
                    LLVMFuzzerTestOneInput(data, sz);
                }
                free(data);
            }
            fclose(f);
        }
    } else {
        /* Minimal Ogg/Vorbis header test */
        static const uint8_t ogg_hdr[] = { 'O', 'g', 'g', 'S', 0, 2, 0, 0, 0, 0, 0, 0, 0, 0 };
        LLVMFuzzerTestOneInput(ogg_hdr, sizeof(ogg_hdr));
    }
    return 0;
}
#endif
