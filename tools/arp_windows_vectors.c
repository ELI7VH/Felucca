/* SPDX-License-Identifier: GPL-3.0-only */
/* The ARP's DIR parity vectors from SHAYPE's Windows core (WaveLoop Kit). WYNDO and SHYFT are SHAYPE's (WaveLoop
 * windows/shaype/shaype_core.c SH_WINDOW / SH_SHYFT, ahead of the web toy's sequence.mjs), so their golden notes come
 * from that C, built natively on the Mac: each case (a pattern, its STEPS, a held chord, a direction, a boundary) goes
 * through sh_build_table, one row per step of a whole cycle, and what the table plays is written as C for
 * tests/arp_shape_test.c, which plays the same case through firmware/src/arp_shape.c ash_tick for two cycles and wants
 * exactly the same notes at every tick (tick % the cycle). UP, DOWN and PEND are re-checked the same way (RAND draws
 * differ by design: tools/arp_vectors.mjs holds it to the toy). PEND climbs in octaves whatever BOUND says (WaveLoop
 * a205e3a6), so its WRAP cases are SHAYPE's, not the toy's (tests/arp_shape_test.c retires the toy's PEND WRAP).
 *   W=$WAVELOOP/windows/shaype   (WAVELOOP: ~/repos/waveloop; the vectors in the repo: its commit 4cbf5392, origin/main
 *                                 on 2026-10-25; shaype_core.c last changed at a205e3a6)
 *   cc -O1 -I"$W" -o build/arp_windows_vectors tools/arp_windows_vectors.c "$W/shaype_core.c" -lm
 *   build/arp_windows_vectors "$(shasum -a 256 "$W/shaype_core.c" | cut -c1-64)" > tests/arp_window_vectors.h
 * Chords are given voiced (low to high, each once): ash_voicing / sh_root_voicing are held to the toy elsewhere. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shaype_core.h"

#define NMAX 30                          /* firmware/src/arp_shape.c ARP_NMAX */
static const int DIRS[5] = {SH_UP, SH_DOWN, SH_PENDULUM, SH_WINDOW, SH_SHYFT};
static const int KIT_DIR[SH_DIRECTIONS] = {0, 1, 2, 3, 4, 5};   /* (AD_* = SH_* in their order) */

static unsigned xs = 0x5A1F7u;
static unsigned rnd(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

static ShPattern pat;
static ShRow rows[SH_MAX_STEPS * 16];
static int ncase, nev_all, by_dir[SH_DIRECTIONS], moved[SH_DIRECTIONS];

static void emit(int steps, const int *chord, int n, int dir, int bound)
{
    unsigned seed = 1;
    int len = sh_build_table(&pat, steps, chord, n, dir, bound, rows, &seed), nev = 0, i, j;
    if (pat.count > NMAX || steps > 64) { fprintf(stderr, "case past the Kit's limits\n"); exit(1); }
    printf("static const uint16_t AW_EV%d[] = {", ncase);
    for (i = 0; i < len; i++)
        for (j = 0; j < rows[i].count; j++)
            printf("%s%d, %d, %d", nev++ ? ", " : "", i, rows[i].pitch[j], rows[i].length[j]);
    printf("%s};\n", nev ? "" : "0");
    printf("#define AW_C%d {%d, %d, %d, %d, %d, {", ncase, steps, len, KIT_DIR[dir], bound, n);
    for (i = 0; i < n; i++) printf("%s%d", i ? ", " : "", chord[i]);
    printf("%s}, %d, {", n ? "" : "0", pat.count);
    for (i = 0; i < pat.count; i++) printf("%s%d", i ? ", " : "", pat.n[i].step | pat.n[i].row << 6 | (pat.n[i].length - 1) << 9);
    printf("%s}, %d, AW_EV%d}\n", pat.count ? "" : "0", nev, ncase);
    ncase++; nev_all += nev; by_dir[dir]++;
    moved[dir] += len > steps;
}
static void all_dirs(int steps, const int *chord, int n)
{
    for (int d = 0; d < 5; d++)
        for (int b = 0; b < 2; b++) emit(steps, chord, n, DIRS[d], b);
}
static void set(const int (*n)[3], int k)
{
    pat.count = 0;
    for (int i = 0; i < k; i++) sh_paint(&pat, n[i][1], n[i][0], n[i][2]);   /* {step, row, length} */
}
static int draw(int steps, int lift)                    /* a drawn pattern, rows lift.. (a group off row 0) */
{
    pat.count = 0;
    for (int i = 0, k = 1 + (int)(rnd() % 14); i < k && pat.count < NMAX; i++) {
        const int step = (int)(rnd() % (unsigned)steps), length = 1 + (int)(rnd() % (unsigned)(steps - step));
        sh_paint(&pat, lift + (int)(rnd() % (unsigned)(SH_ROWS - lift)), step, rnd() % 3 ? 1 : length);
    }
    return pat.count <= NMAX;
}

int main(int argc, char **argv)
{
    static const int F[7][16][3] = {
        {{0, 0, 1}, {0, 2, 1}, {1, 1, 1}, {1, 3, 1}, {2, 2, 1}, {2, 3, 1}, {3, 0, 1}, {3, 3, 1}},
        {{0, 0, 1}, {1, 1, 1}, {2, 2, 1}, {3, 3, 1}},
        {{0, 0, 1}, {1, 1, 1}, {2, 2, 1}, {3, 3, 1}, {4, 2, 1}, {5, 1, 1}},
        {{0, 0, 1}, {0, 1, 1}, {0, 2, 1}, {3, 0, 1}, {3, 1, 1}, {3, 2, 1}, {6, 0, 2}, {6, 1, 2}, {6, 2, 2}},
        {{0, 0, 1}, {1, 1, 1}, {2, 2, 1}, {3, 3, 1}, {4, 4, 1}, {5, 5, 1}, {6, 6, 1}, {7, 7, 1}},
        {{0, 0, 1}, {1, 2, 1}, {2, 0, 1}, {3, 3, 1}, {4, 0, 1}, {5, 2, 1}, {6, 0, 1}, {7, 1, 1}},
        {{0, 0, 4}, {1, 2, 1}, {3, 1, 1}}};
    static const int FN[7] = {8, 4, 6, 9, 8, 8, 3}, FS[7] = {4, 4, 6, 8, 8, 8, 4};
    static const int C0[1] = {0}, C1[1] = {60}, C2[2] = {60, 64}, C3[3] = {60, 64, 67}, FACE[4] = {53, 57, 60, 64},
                     C5[5] = {48, 52, 55, 59, 62}, C8[8] = {36, 43, 52, 60, 67, 71, 74, 79}, HI[4] = {115, 118, 122, 125};
    static const int *CH[8] = {C0, C1, C2, C3, FACE, C5, C8, HI};
    static const int CN[8] = {0, 1, 2, 3, 4, 5, 8, 4};
    /* the README's: F A C E under a 3-row group plays F A C, then A C E; on rows 2..4 the same; a 4-row group stays */
    static const int G3[3][3] = {{0, 0, 1}, {1, 1, 1}, {2, 2, 1}}, G3UP[3][3] = {{0, 2, 1}, {1, 3, 1}, {2, 4, 1}},
                     G4[4][3] = {{0, 0, 1}, {1, 1, 1}, {2, 2, 1}, {3, 3, 1}},
                     /* a tall group (rows 1..6), chords over a step, a long note, a row twice on a step's merge */
                     TALL[8][3] = {{0, 1, 2}, {0, 6, 1}, {1, 3, 1}, {2, 2, 1}, {2, 5, 2}, {3, 4, 1}, {4, 1, 1}, {5, 6, 1}},
                     ONE[2][3] = {{0, 5, 1}, {2, 5, 2}};
    if (argc < 2) { fprintf(stderr, "usage: arp_windows_vectors <sha256 of shaype_core.c>\n"); return 2; }
    printf("/* generated by tools/arp_windows_vectors.c from WaveLoop windows/shaype/shaype_core.c sha256 %s - do not edit */\n", argv[1]);
    printf("/* a case: STEPS, the cycle's length in steps (SHAYPE's table), DIR (AD_*), BOUND, the held chord (voiced), the\n"
           " * pattern (ARP_NOTE packed); the notes of one cycle: tick, pitch, length */\n");
    printf("typedef struct { uint8_t steps; uint16_t cycle; uint8_t dir, bound, nchord, chord[8], npat; uint16_t pat[ARP_NMAX];\n"
           "                 uint16_t nev; const uint16_t *ev; } arp_wvec_t;\n");
    for (int f = 0; f < 7; f++) {
        set(F[f], FN[f]);
        for (int c = 1; c < 8; c++) if (c != 2 && c != 6) all_dirs(FS[f], CH[c], CN[c]);   /* (1 3 4 5 notes, high) */
    }
    set(G3, 3); for (int c = 0; c < 8; c++) all_dirs(3, CH[c], CN[c]);
    set(G3UP, 3); for (int c = 0; c < 8; c++) all_dirs(3, CH[c], CN[c]);
    set(F[0], FN[0]); all_dirs(4, C8, 8); all_dirs(4, C2, 2); all_dirs(4, C0, 0);
    set(G4, 4); all_dirs(4, FACE, 4); all_dirs(4, C5, 5);
    set(TALL, 8); for (int c = 0; c < 8; c++) all_dirs(6, CH[c], CN[c]);
    set(ONE, 2); all_dirs(4, C3, 3); all_dirs(4, C1, 1);
    pat.count = 0; all_dirs(4, C3, 3);                 /* (no notes) */
    for (int k = 0; k < 60; k++) {                       /* drawn: STEPS 1..16 (a few 32 and 64), chords 0..8 notes */
        const int steps = k % 10 == 9 ? (k % 20 == 9 ? 64 : 32) : 1 + (int)(rnd() % 16u), lift = (int)(rnd() % 4u);
        int chord[8], n = (int)(rnd() % 9u), lo = 24 + (int)(rnd() % 60u);
        if (!draw(steps, lift)) continue;
        for (int i = 0; i < n; i++) chord[i] = lo += 1 + (int)(rnd() % 7u);
        for (int i = 0; i < n; i++) if (chord[i] > 127) n = i;
        emit(steps, chord, n, DIRS[k % 5], (int)(rnd() & 1u));
        emit(steps, chord, n, DIRS[(k + 3) % 5], (int)(rnd() & 1u));
    }
    printf("static const arp_wvec_t ARP_WVEC[] = {\n");
    for (int i = 0; i < ncase; i++) printf("    AW_C%d,\n", i);
    printf("};\n#define ARP_NWVEC %du   /* %d events; UP %d, DOWN %d, PEND %d, WYNDO %d (%d moving), SHYFT %d (%d moving) */\n",
           ncase, nev_all, by_dir[SH_UP], by_dir[SH_DOWN], by_dir[SH_PENDULUM], by_dir[SH_WINDOW], moved[SH_WINDOW],
           by_dir[SH_SHYFT], moved[SH_SHYFT]);
    return 0;
}
