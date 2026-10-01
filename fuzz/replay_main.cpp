/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/*
 * replay_main.cpp — runs a harness over files instead of fuzzing them, so the
 * corpora are a regression test anywhere g++ is (Windows included, no clang).
 * Linked in place of libFuzzer's main; every argument is one input file:
 *
 *   g++ -std=c++14 -Icryptnox-sdk-esp32/cryptnox-sdk-cpp \
 *       fuzz/fuzz_eth_rlp.cpp fuzz/replay_main.cpp -o r && ./r fuzz/corpus/eth_rlp/seed_*.bin  (or any file list)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (f == NULL) {
            fprintf(stderr, "cannot open %s\n", argv[i]);
            return 1;
        }
        (void)fseek(f, 0, SEEK_END);
        long len = ftell(f);
        (void)fseek(f, 0, SEEK_SET);
        uint8_t *buf = static_cast<uint8_t *>(malloc((len > 0) ? (size_t)len : 1U));
        size_t n = fread(buf, 1U, (size_t)len, f);
        (void)fclose(f);
        (void)LLVMFuzzerTestOneInput(buf, n);
        free(buf);
    }
    printf("replayed %d input(s) OK\n", argc - 1);
    return 0;
}
