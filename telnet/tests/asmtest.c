/* Runs the real tr_render machine code from symterm.exe in a Z80 emulator
 * and compares the output against a C reference implementation.
 * usage: asmtest symterm.exe symterm.exe.map */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "z80.h"

static uint8_t mem[65536];
static uint8_t rd(void* u, uint16_t a) { (void)u; return mem[a]; }
static void wr(void* u, uint16_t a, uint8_t v) { (void)u; mem[a] = v; }

static unsigned sym(const char* map, const char* name) {
    FILE* f = fopen(map, "r"); char line[256]; unsigned a; char t[8], n[128];
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%x %7s %127s", &a, t, n) == 3 && !strcmp(n, name)) { fclose(f); return a; }
    fprintf(stderr, "missing %s\n", name); exit(1);
}

int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "rb");
    size_t len = fread(mem, 1, 65536, f); fclose(f);
    unsigned render = sym(argv[2], "_tr_render");
    unsigned pfont = sym(argv[2], "_tr_font3"), pctab = sym(argv[2], "_tr_ctab");
    unsigned ppage = sym(argv[2], "_tr_curpage");
    unsigned font = sym(argv[2], "_tr_fontdata"), font3 = sym(argv[2], "_font3"), ctab = sym(argv[2], "_ctab");
    unsigned curbuf = sym(argv[2], "_curbuf");
    unsigned page = ((curbuf + 255) & 0xFF00) >> 8;
    int fails = 0, trial;
    printf("loaded %zu bytes, tr_render at %04X\n", len, render);

    srand(1234);
    /* colour table exactly as build_ctab() makes it (identity palette for test) */
    for (unsigned a = 0; a < 256; ++a) {
        uint8_t fg = a & 15, bg = a >> 4;
        mem[ctab + a*4 + 0] = bg << 4 | bg;
        mem[ctab + a*4 + 1] = bg << 4 | fg;
        mem[ctab + a*4 + 2] = fg << 4 | bg;
        mem[ctab + a*4 + 3] = fg << 4 | fg;
    }
    for (unsigned ch = 0; ch < 256; ++ch) for (int l = 0; l < 8; ++l) {
        uint8_t v = mem[font + ch*8 + l], *d = mem + font3 + ch*24 + l*3;
        if (l & 1) { d[0] = v & 3; d[1] = (v >> 2) & 3; d[2] = (v >> 4) & 3; }
        else { d[0] = (v >> 4) & 3; d[1] = (v >> 2) & 3; d[2] = v & 3; }
    }
    mem[pfont] = font3 & 255; mem[pfont+1] = font3 >> 8;
    mem[pctab] = ctab & 255; mem[pctab+1] = ctab >> 8;
    mem[ppage] = page;

    for (trial = 0; trial < 200; ++trial) {
        unsigned text = 0xA000, attr = 0xA100, dst = 0xB000, n = 1 + rand() % 80, c0 = rand() % (81 - n);
        uint8_t ref[2048];
        unsigned long cyc0;
        z80 z;
        for (unsigned i = 0; i < 80; ++i) {
            mem[text + i] = rand();
            mem[attr + i] = (rand() % 4 == 0) ? mem[attr + (i ? i-1 : 0)] : rand();
        }
        memset(mem + dst, 0xEE, 2048);
        memset(ref, 0xEE, 2048);
        /* reference */
        for (unsigned i = 0; i < n; ++i) {
            uint8_t ch = mem[text + c0 + i], at = mem[attr + c0 + i];
            for (int l = 0; l < 8; ++l) {
                uint8_t v = mem[font + ch*8 + l];
                uint8_t* d = ref + l*256 + (c0 + i)*3;
                d[0] = mem[ctab + at*4 + ((v >> 4) & 3)];
                d[1] = mem[ctab + at*4 + ((v >> 2) & 3)];
                d[2] = mem[ctab + at*4 + (v & 3)];
            }
        }
        /* call tr_render(dst + c0*3, text + c0, attr + c0, n) */
        z80_init(&z);
        z.read_byte = rd; z.write_byte = wr;
        z.sp = 0xF000;
        uint16_t args[4] = { dst + c0*3, text + c0, attr + c0, n };
        for (int k = 3; k >= 0; --k) { z.sp -= 2; mem[z.sp] = args[k] & 255; mem[z.sp+1] = args[k] >> 8; }
        z.sp -= 2; mem[z.sp] = 0x00; mem[z.sp+1] = 0xE0; mem[0xE000] = 0x76; /* ret -> HALT */
        z.b = 0x12; z.c = 0x34; z.ix = 0x5678; z.iy = 0x9ABC;
        z.pc = render;
        cyc0 = z.cyc;
        while (!z.halted && z.cyc - cyc0 < 5000000) z80_step(&z);
        if (!z.halted) { printf("trial %d: did not return\n", trial); return 1; }
        if (z.b != 0x12 || z.c != 0x34 || z.ix != 0x5678 || z.iy != 0x9ABC || z.sp != 0xF000 - 8) {
            printf("trial %d: register/stack not preserved (sp=%04X)\n", trial, z.sp); ++fails;
        }
        if (memcmp(ref, mem + dst, 2048)) { printf("trial %d (c0=%u n=%u): output mismatch\n", trial, c0, n); ++fails; }
        if (trial == 0 || n == 80)
            printf("trial %d: %u chars in %lu T-states (%.0f per char, %.1f ms @3.58MHz)\n",
                   trial, n, z.cyc - cyc0, (double)(z.cyc - cyc0) / n, (z.cyc - cyc0) / 3579.545);
    }
    printf(fails ? "FAILED (%d)\n" : "all %d trials OK\n", fails ? fails : trial);
    return fails != 0;
}
