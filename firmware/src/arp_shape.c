/* SPDX-License-Identifier: GPL-3.0-only */
/* SHAYPE, WaveLoop's Shape Arp, as the ARP (WaveLoop Kit; ported from WaveLoop's Switch Blade build). A port of the web toy's core, web/src/arp/core/sequence.mjs
 * in the WaveLoop repo, held to it by tests/arp_shape_test.c against vectors the JS core wrote (tools/arp_vectors.mjs,
 * tests/arp_vectors.h). Nothing here touches a track, the clock or a voice: seq.c is the host (arp_fire), ui_arp.c
 * edits the pattern, project.c stores it. Built on the Mac too (the test includes this file alone).
 *
 * A pattern is notes {step, row, length} on STEPS steps (1..64) by ARP_ROWS rows. A row is a place in the held chord
 * (ash_voicing: low to high, each pitch once, a ROOT moving the notes under it up octaves). Every pass (one cycle of
 * STEPS) moves the rows by an offset (ash_pass_offset: UP, DOWN, PEND, RAND; ash_window: WYNDO, SHYFT); a place past
 * the chord wraps round the held pitches (WRAP) or moves by octaves (OCT) (ash_resolve; PEND always by octaves). The
 * notes starting on a step sound together, one pitch once (the longest of its lengths), for length x the step x GATE
 * (ash_events). */
#ifndef ARP_SHAPE_C
#define ARP_SHAPE_C                      /* (project.c includes it on the Mac without seq.c) */
#include <stdint.h>

#define ARP_ROWS 8u
#define ARP_STEPS 64u
#define ARP_NMAX 30u                     /* notes a pattern holds: 4 + 30 x 2 bytes = project.c's 64 per track (FUN9) */
/* a note, packed: step 0..63 (bits 0..5), row 0..7 (6..8), length - 1 0..63 (9..14) */
#define ARP_NOTE(s, r, l) ((uint16_t)((s) | (r) << 6 | ((l) - 1u) << 9))
#define ARP_STEP(n) ((uint32_t)(n) & 63u)
#define ARP_ROW(n) (((uint32_t)(n) >> 6) & 7u)
#define ARP_LEN(n) ((((uint32_t)(n) >> 9) & 63u) + 1u)
typedef struct {
    uint8_t n;                           /* notes in use */
    uint8_t rsv[3];                      /* (the stored block's pat, root, oct: project.c keeps them in seq.c arp_x) */
    uint16_t note[ARP_NMAX];             /* in the order they were written (the JS core's array order) */
} arp_pat_t;

/* P_AOCT: DIR (sequence.mjs passOffset's directions, then SHAYPE's WYNDO and SHYFT: WaveLoop windows/shaype/shaype_core.c
 * SH_WINDOW / SH_SHYFT, ahead of the toy's sequence.mjs; PEND in octaves from a205e3a6; held to that core at 4cbf5392 by
 * tests/arp_window_vectors.h). RAND is
 * off the face (params.c enum_step steps over it); a stored RAND still plays and shows as RAND */
enum { AD_UP, AD_DOWN, AD_PEND, AD_RAND, AD_WYNDO, AD_SHYFT, AD_N };
enum { AB_WRAP, AB_OCT };                    /* P_AORDER: BOUND */
/* RATE (P_ARATE), sequence.mjs RATES slow to fast, in steps per two beats: 1/2 1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32 */
static const uint8_t ARP_RATE2[8] = {1, 2, 3, 4, 6, 8, 12, 16};

/* sequence.mjs DEFAULT_PATTERN, then the factory patterns (P_APAT 1..: ARP_FACTORY[k - 1]) */
typedef struct { const char *name; uint8_t steps, bound, n; uint16_t note[16]; } arp_factory_t;   /* (loading one sets
                                                                          * STEPS and BOUND too) */
#define AN ARP_NOTE
static const arp_factory_t ARP_FACTORY[] = {
    {"SHAYPE", 4, AB_WRAP, 8, {AN(0, 0, 1), AN(0, 2, 1), AN(1, 1, 1), AN(1, 3, 1), AN(2, 2, 1), AN(2, 3, 1), AN(3, 0, 1),
                               AN(3, 3, 1)}},
    {"UP 4", 4, AB_WRAP, 4, {AN(0, 0, 1), AN(1, 1, 1), AN(2, 2, 1), AN(3, 3, 1)}},
    {"UPDOWN", 6, AB_WRAP, 6, {AN(0, 0, 1), AN(1, 1, 1), AN(2, 2, 1), AN(3, 3, 1), AN(4, 2, 1), AN(5, 1, 1)}},
    {"STABS", 8, AB_WRAP, 9, {AN(0, 0, 1), AN(0, 1, 1), AN(0, 2, 1), AN(3, 0, 1), AN(3, 1, 1), AN(3, 2, 1), AN(6, 0, 2),
                              AN(6, 1, 2), AN(6, 2, 2)}},
    {"OCT RUN", 8, AB_OCT, 8, {AN(0, 0, 1), AN(1, 1, 1), AN(2, 2, 1), AN(3, 3, 1), AN(4, 4, 1), AN(5, 5, 1), AN(6, 6, 1),
                               AN(7, 7, 1)}},
    {"PEDAL", 8, AB_WRAP, 8, {AN(0, 0, 1), AN(1, 2, 1), AN(2, 0, 1), AN(3, 3, 1), AN(4, 0, 1), AN(5, 2, 1), AN(6, 0, 1),
                              AN(7, 1, 1)}},
    {"DRONE", 4, AB_WRAP, 3, {AN(0, 0, 4), AN(1, 2, 1), AN(3, 1, 1)}},
};
#undef AN
#define ARP_NFACTORY (sizeof ARP_FACTORY / sizeof ARP_FACTORY[0])

static void ash_load(arp_pat_t *p, uint32_t k)   /* factory pattern k (0: SHAYPE, the toy's DEFAULT_PATTERN) */
{
    const arp_factory_t *f = &ARP_FACTORY[k < ARP_NFACTORY ? k : 0u];
    uint32_t i;
    p->n = f->n;
    p->rsv[0] = p->rsv[1] = p->rsv[2] = 0;
    for (i = 0; i < ARP_NMAX; i++)
        p->note[i] = i < f->n ? f->note[i] : 0u;
}

/* passOffset(pass, size, direction, random): rnd draws a fraction of 2^32 (Math.floor(random() * size)), only where
 * the toy calls random() (RAND over two notes or more) */
static int32_t ash_pass_offset(uint32_t pass, uint32_t size, uint32_t dir, uint32_t (*rnd)(void))
{
    uint32_t p;
    if (size < 2u)
        return 0;
    if (dir == AD_DOWN)
        return -(int32_t)(pass % size);
    if (dir == AD_RAND)
        return (int32_t)(((uint64_t)rnd() * size) >> 32);
    if (dir == AD_PEND) {
        p = pass % (2u * (size - 1u));
        return (int32_t)(p < size ? p : 2u * (size - 1u) - p);
    }
    if (dir >= AD_WYNDO)                         /* (ash_window moves the rows itself) */
        return 0;
    return (int32_t)(pass % size);
}

/* WYNDO and SHYFT (shaype_core.c sh_window_span, sh_build_table): the group is the pattern's rows from its lowest note's
 * (the foot) to its highest's (h rows tall; the notes inside STEPS), over n held notes.
 *   WYNDO: the group slides up through the held notes a note a pass, from the lowest until its top is the highest, then
 *          starts again: pass p plays row r at chord place r - foot + p; n - h + 1 passes (1: a group as tall as the
 *          chord, or taller, stays put). Holding F A C E under a 3-row group: F A C, then A C E.
 *   SHYFT: the chord stays put; a window n rows high stands on the foot and climbs the group a row a pass: pass p plays
 *          only rows foot + p .. foot + p + n - 1, row r at place r - foot - p; h - n + 1 passes (1: a group no taller).
 * -> the pass's offset (a row's place is row + offset; SHYFT: only places 0..n-1 sound, ash_events win) */
static int32_t ash_window(const arp_pat_t *p, uint32_t steps, uint32_t pass, uint32_t n, uint32_t dir)
{
    uint32_t i, lo = ARP_ROWS, top = 0, h, passes;
    for (i = 0; i < p->n; i++) {
        uint32_t r = ARP_ROW(p->note[i]);
        if (ARP_STEP(p->note[i]) >= steps)
            continue;
        lo = r < lo ? r : lo;
        top = r + 1u > top ? r + 1u : top;
    }
    if (lo > top)                                /* (no notes) */
        lo = 0;
    h = top - lo;
    if (dir == AD_WYNDO) {
        passes = n > h ? n - h + 1u : 1u;
        return (int32_t)(pass % passes) - (int32_t)lo;
    }
    passes = n && h > n ? h - n + 1u : 1u;
    return -(int32_t)(lo + pass % passes);
}

/* rootVoicing(notes, root): each note under root up octaves until it is not, then low to high, each pitch once, 0..127.
 * root < 0: none. out: up to n pitches; their count */
static uint32_t ash_voicing(const uint8_t *in, uint32_t n, int32_t root, uint8_t *out)
{
    uint32_t i, j, k = 0;
    for (i = 0; i < n; i++) {
        int32_t v = in[i];
        if (root >= 0 && v < root)
            v += 12 * ((root - v + 11) / 12);
        if (v < 0 || v > 127)
            continue;
        for (j = 0; j < k && out[j] != v; j++)
            ;
        if (j < k)
            continue;
        for (j = k++; j > 0 && out[j - 1] > v; j--)
            out[j] = out[j - 1];
        out[j] = (uint8_t)v;
    }
    return k;
}

/* resolvePosition(held, position, boundary): held low to high, each once (ash_voicing); -1 = nothing */
static int32_t ash_resolve(const uint8_t *held, uint32_t n, int32_t pos, uint32_t bound)
{
    int32_t len = (int32_t)n, idx, pitch, oct;
    if (!n)
        return -1;
    idx = ((pos % len) + len) % len;
    oct = pos >= 0 ? pos / len : -((-pos + len - 1) / len);   /* Math.floor(position / held.length) */
    pitch = held[idx] + (bound == AB_OCT ? oct * 12 : 0);
    return pitch >= 0 && pitch <= 127 ? pitch : -1;
}

/* eventsAt(pattern, step, held, offset, boundary), merged as the hosts merge it (one pitch once, its longest length,
 * in the order its first note comes in the pattern). win (SHYFT): only places 0..n-1 sound. pitch / len: up to
 * ARP_ROWS; their count */
static uint32_t ash_events(const arp_pat_t *p, uint32_t step, const uint8_t *held, uint32_t n, int32_t offset,
                           uint32_t bound, int win, uint8_t *pitch, uint8_t *len)
{
    uint32_t i, j, k = 0;
    for (i = 0; i < p->n; i++) {
        uint32_t x = p->note[i], l;
        int32_t v, at = (int32_t)ARP_ROW(x) + offset;
        if (ARP_STEP(x) != step)
            continue;
        if (win && (at < 0 || at >= (int32_t)n))    /* (SHYFT: outside the window) */
            continue;
        if ((v = ash_resolve(held, n, at, bound)) < 0)
            continue;
        l = ARP_LEN(x);
        for (j = 0; j < k && pitch[j] != v; j++)
            ;
        if (j < k) {
            if (l > len[j])
                len[j] = (uint8_t)l;
        } else if (k < ARP_ROWS) {
            pitch[k] = (uint8_t)v;
            len[k++] = (uint8_t)l;
        }
    }
    return k;
}

/* one step of the hosts' loop (main.js schedule; seq.c arp_fire): step tick % steps of pass tick / steps over the held
 * chord (ash_voicing); the notes it starts (ash_events). The pass's offset is worked out on every step from the chord
 * it is given (SHAYPE's table, rebuilt with the chord, read at its pass and step: shaype_engine.c), RAND's drawn on the
 * pass's first step only. Which chord a step is given (the keys as they are now, or the one its pass began with) is
 * the host's: KEY ATTACH (seq.c arp_fire). PEND climbs to the top and back in octaves whatever BOUND says, never
 * wrapping (WaveLoop a205e3a6: shaype_core.c sh_build_table) */
#ifndef ASH_PASS_ONCE
#define ASH_PASS_ONCE 0                  /* (tests/arp_seq_test.c sets it to hold the host's timing to the toy's shapeChords,
                                          * whose UP DOWN PEND keep the offset their pass began with) */
#endif
static uint32_t ash_tick(const arp_pat_t *p, uint32_t steps, uint32_t tick, const uint8_t *held, uint32_t n, uint32_t dir,
                         uint32_t bound, uint32_t (*rnd)(void), int32_t *offset, uint8_t *pitch, uint8_t *len)
{
    uint32_t s = tick % steps;
    if (dir == AD_WYNDO || dir == AD_SHYFT)
        *offset = ash_window(p, steps, tick / steps, n, dir);
    else if (!s || (dir != AD_RAND && !ASH_PASS_ONCE))
        *offset = ash_pass_offset(tick / steps, n, dir, rnd);
    if (dir == AD_PEND)
        bound = AB_OCT;
    return ash_events(p, s, held, n, *offset, bound, dir == AD_SHYFT, pitch, len);
}

/* the note of row r sounding on step s (it starts there or before and lasts over it): its index, -1 none */
static int32_t ash_find(const arp_pat_t *p, uint32_t row, uint32_t step)
{
    uint32_t i;
    for (i = 0; i < p->n; i++) {
        uint32_t x = p->note[i];
        if (ARP_ROW(x) == row && ARP_STEP(x) <= step && step < ARP_STEP(x) + ARP_LEN(x))
            return (int32_t)i;
    }
    return -1;
}
static void ash_drop(arp_pat_t *p, uint32_t i)   /* note i out, the order of the others kept */
{
    for (; i + 1u < p->n; i++)
        p->note[i] = p->note[i + 1u];
    p->note[--p->n] = 0;                 /* (the slot let go is 0: a stored pattern's tail always is) */
}
/* paintNote(pattern, row, step, length): the notes of the row it meets go, it goes last. 0: the pattern is full */
static int ash_paint(arp_pat_t *p, uint32_t row, uint32_t step, uint32_t len)
{
    uint32_t i = 0, end = step + len;
    while (i < p->n) {
        uint32_t x = p->note[i];
        if (ARP_ROW(x) == row && !(ARP_STEP(x) + ARP_LEN(x) <= step || ARP_STEP(x) >= end))
            ash_drop(p, i);
        else
            i++;
    }
    if (p->n >= ARP_NMAX)
        return 0;
    p->note[p->n++] = ARP_NOTE(step, row, len);
    return 1;
}
/* the steps setting got shorter (the toy's rule): notes past the end go, notes over it are cut */
static void ash_fit(arp_pat_t *p, uint32_t steps)
{
    uint32_t i = 0;
    while (i < p->n) {
        uint32_t x = p->note[i], s = ARP_STEP(x);
        if (s >= steps) {
            ash_drop(p, i);
            continue;
        }
        if (s + ARP_LEN(x) > steps)
            p->note[i] = ARP_NOTE(s, ARP_ROW(x), steps - s);
        i++;
    }
}
/* a stored pattern is one: every note inside 64 steps, no more than ARP_NMAX */
static int ash_valid(const arp_pat_t *p)
{
    uint32_t i;
    if (p->n > ARP_NMAX)
        return 0;
    for (i = 0; i < p->n; i++)
        if ((p->note[i] & 0x8000u) || ARP_STEP(p->note[i]) + ARP_LEN(p->note[i]) > ARP_STEPS)
            return 0;
    return 1;
}
/* subdivide(pattern, steps, rate, factor), the toy's x2 / /2: ZOOM. In (factor 2) every step splits in two at twice
 * the rate; out (0.5) pairs join at half the rate. Every note's step and length go with it, so the pattern sounds the
 * same. rate: P_ARATE (an index of ARP_RATE2). AZ_OK and the new *steps *rate, or why not, the toy's checks in its
 * order, nothing changed: no such rate, past 64 steps, or (joining) STEPS odd or a note starting or ending inside a
 * pair. The toy's RATES go on to 1/64, RATE here stops at 1/32: where the toy would zoom 1/32 in, AZ_FAST */
enum { AZ_OK, AZ_FAST, AZ_SLOW, AZ_NORATE, AZ_MAX, AZ_ODD, AZ_BETWEEN };
static uint32_t ash_subdivide(arp_pat_t *p, uint32_t *steps, uint32_t *rate, int in)
{
    uint32_t r = ARP_RATE2[*rate < 8u ? *rate : 7u], next = in ? r * 2u : r / 2u, j, i;
    for (j = 0; j < 8u && ARP_RATE2[j] != next; j++)
        ;
    if (in) {
        if (j == 8u && next != 2u * ARP_RATE2[7])      /* (the toy's 1/64: its next checks first) */
            return AZ_NORATE;
        if (*steps * 2u > ARP_STEPS)
            return AZ_MAX;
        if (j == 8u)
            return AZ_FAST;
        for (i = 0; i < p->n; i++)               /* (a note past STEPS that would not fit: never, kept out anyway) */
            if ((ARP_STEP(p->note[i]) + ARP_LEN(p->note[i])) * 2u > ARP_STEPS)
                return AZ_MAX;
        for (i = 0; i < p->n; i++)
            p->note[i] = ARP_NOTE(ARP_STEP(p->note[i]) * 2u, ARP_ROW(p->note[i]), ARP_LEN(p->note[i]) * 2u);
        *steps *= 2u;
    } else {
        if (j == 8u || (r & 1u))
            return r <= ARP_RATE2[0] ? AZ_SLOW : AZ_NORATE;
        if (*steps & 1u)
            return AZ_ODD;
        for (i = 0; i < p->n; i++)
            if ((ARP_STEP(p->note[i]) | ARP_LEN(p->note[i])) & 1u)
                return AZ_BETWEEN;
        for (i = 0; i < p->n; i++)
            p->note[i] = ARP_NOTE(ARP_STEP(p->note[i]) / 2u, ARP_ROW(p->note[i]), ARP_LEN(p->note[i]) / 2u);
        *steps /= 2u;
    }
    *rate = j;
    return AZ_OK;
}
/* shift(pattern, steps, by), the toy's rotate one step left (-1) or right (+1): a note over the end carries on from step
 * 0 (two notes, the tail on step 0). First the two halves of a note split at the end are joined (a note ending on STEPS
 * takes the first note of its row on step 0 not taken yet, when both fit in STEPS), so shifting back and forth loses
 * nothing. The order: the notes as they were, each in place (a split one: its head, then its tail). 0: past ARP_NMAX
 * notes (the toy has no limit), nothing changed */
static int ash_shift(arp_pat_t *p, uint32_t steps, int32_t by)
{
    uint16_t out[ARP_NMAX];
    uint8_t len[ARP_NMAX], head[ARP_NMAX];
    uint32_t i, j, k = 0, n = p->n, s, r;
    if (!steps || n > ARP_NMAX)
        return 0;
    for (i = 0; i < n; i++) {
        len[i] = (uint8_t)ARP_LEN(p->note[i]);
        head[i] = 0;
    }
    for (i = 0; i < n; i++) {
        if (ARP_STEP(p->note[i]) + len[i] != steps)
            continue;
        for (j = 0; j < n; j++)                  /* (notes.find: the first, its length checked after) */
            if (j != i && ARP_ROW(p->note[j]) == ARP_ROW(p->note[i]) && ARP_STEP(p->note[j]) == 0u && !head[j])
                break;
        if (j < n && len[i] + len[j] <= steps) {
            head[j] = 1;
            len[i] = (uint8_t)(len[i] + len[j]);
        }
    }
    for (i = 0; i < n; i++) {
        if (head[i])
            continue;
        r = ARP_ROW(p->note[i]);
        s = (uint32_t)((((int32_t)ARP_STEP(p->note[i]) + by) % (int32_t)steps + (int32_t)steps) % (int32_t)steps);
        if (k + (s + len[i] > steps ? 2u : 1u) > ARP_NMAX)
            return 0;
        if (s + len[i] <= steps) {
            out[k++] = ARP_NOTE(s, r, len[i]);
        } else {
            out[k++] = ARP_NOTE(s, r, steps - s);
            out[k++] = ARP_NOTE(0u, r, s + len[i] - steps);
        }
    }
    for (i = 0; i < ARP_NMAX; i++)
        p->note[i] = i < k ? out[i] : 0u;
    p->n = (uint8_t)k;
    return 1;
}
/* MIRROR in time: a note on step s, length l, goes to steps - s - l (the pattern played backwards; its own inverse) */
static void ash_reverse(arp_pat_t *p, uint32_t steps)
{
    uint32_t i, s, l;
    for (i = 0; i < p->n; i++) {
        s = ARP_STEP(p->note[i]);
        l = ARP_LEN(p->note[i]);
        if (s >= steps)                          /* (never: ash_fit keeps every note inside STEPS) */
            continue;
        if (s + l > steps)
            l = steps - s;
        p->note[i] = ARP_NOTE(steps - s - l, ARP_ROW(p->note[i]), l);
    }
}
/* MIRROR upside down: the rows the pattern uses turn over inside their own span (lowest + highest - r), so the shape
 * keeps its place in the chord (a 4-row pattern on rows 1..4 stays on rows 1..4; its own inverse) */
static void ash_flip(arp_pat_t *p)
{
    uint32_t i, lo = ARP_ROWS, hi = 0, r;
    for (i = 0; i < p->n; i++) {
        r = ARP_ROW(p->note[i]);
        lo = r < lo ? r : lo;
        hi = r > hi ? r : hi;
    }
    for (i = 0; i < p->n; i++)
        p->note[i] = ARP_NOTE(ARP_STEP(p->note[i]), lo + hi - ARP_ROW(p->note[i]), ARP_LEN(p->note[i]));
}
/* the empty cells (a row on a step no note of that row lasts over) inside STEPS; with k < that count, the k-th of them
 * (step by step, rows up) gets a note of length 1 (MUTATE +; the caller sees there is room). Returns the count */
static uint32_t ash_empty(arp_pat_t *p, uint32_t steps, int32_t k)
{
    uint64_t occ[ARP_ROWS] = {0};
    uint32_t i, s, r, n = 0;
    for (i = 0; i < p->n; i++)
        for (s = ARP_STEP(p->note[i]); s < ARP_STEP(p->note[i]) + ARP_LEN(p->note[i]) && s < 64u; s++)
            occ[ARP_ROW(p->note[i])] |= (uint64_t)1 << s;
    for (s = 0; s < steps; s++)
        for (r = 0; r < ARP_ROWS; r++) {
            if ((occ[r] >> s) & 1u)
                continue;
            if ((int32_t)n++ == k)
                ash_paint(p, r, s, 1);
        }
    return n;
}
#endif /* ARP_SHAPE_C */
