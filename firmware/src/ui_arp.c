/* SPDX-License-Identifier: GPL-3.0-only */
/* WaveLoop Kit (ported from WaveLoop's Switch Blade build): the ARP's pages (params.c ARP, ARP 2, ARP 3; GR_ARP), SHAYPE's pattern of the selected track (seq.c
 * arp_pat, arp_shape.c) as a grid (ui_graph.c graph_arp): steps across, rows up, the cursor step framed, the step
 * playing lit, 16 steps at a time (the cursor's 16).
 *   PRESETS     the cursor, round the pattern's STEPS
 *   the keys    F3 G3 A3 B3 C4 D4 E4 F4 (the white keys from the left) are rows 1..8 at the cursor step: a tap writes a
 *               note there, or takes away the note that is there; the black keys between them are OP1..OP5 (silent
 *               here, below), the keys above play as everywhere (the chord the arp plays; F#4, printed OP6, too)
 *   a row key held + any knob   that row's note at the cursor: its length (a note the press wrote, or one already there)
 *   EDIT        the notes starting on the cursor step go
 *   ARP 3's PAT a factory pattern, loaded as it is turned to (its STEPS and BOUND too); an edit makes it USER
 *               (PAT ROOT OCT ATTACH are seq.c arp_x, not parameters: stored with the pattern, project.c); ATTACH, KEY
 *               ATTACH: when a chord you change reaches the arp, NOW / NOTE / CYCLE (seq.c arp_fire)
 *   an OP key held + OCT- / OCT+   the OP's shift (ui_layer.c op_shift; arp_op below):
 *               OP1 ZOOM out / in, OP2 SHIFT left / right, OP3 OCT down / up, OP4 MIRROR flip / reverse,
 *               OP5 MUTATE a note out / in
 * Every edit is the main loop's, with the audio ISR held off while the pattern changes (arp_fire reads it). */
static const uint8_t ARP_ROWKEY[ARP_ROWS] = {0, 2, 4, 6, 7, 9, 11, 12};   /* row r's key (from F: F3 G3 A3 B3 C4 D4 E4 F4) */

static int arp_keys_on(void) { return !ui.home && !ui.menu && !ui.confirm && !ui.layer && cur_page()->graph == GR_ARP; }
/* The OP keys: the stock FM-1 prints OP1..OP6 on its black keys F#3 .. F#4. On the ARP pages the five among the row keys
 * (F#3 G#3 A#3 C#4 D#4: keys 1 3 5 8 10, inside seq.c ARP_KEYS, so they never sound or record here) are SHIFT: held,
 * OCT- / OCT+ do the OP's function (op_shift). OP6 (F#4) plays as ever. Off the ARP pages they are notes */
static const uint8_t OP_KEY[5] = {1, 3, 5, 8, 10};
#define OP_KEYS 0x52Au                           /* (the five as key bits) */
static const char *const OP_HINT[5] = {"ZOOM: -OUT +IN", "SHIFT: -LEFT +RIGHT", "OCTAVE: -DOWN +UP",
                                       "MIRROR: -FLIP +REVERSE", "MUTATE: -NOTE +NOTE"};   /* (shown as one is pressed) */
static int op_page(void) { return arp_keys_on() && !ui.uboot; }
static int op_held(uint32_t n)                   /* OP n (1..5) physically down on an ARP page (not a layer's key) */
{
    return n >= 1u && n <= 5u && op_page() && (((fm1_in.notes & ~kb_layer) >> OP_KEY[n - 1u]) & 1u) != 0u;
}
static uint32_t op_cur(void)                     /* the OP held (the lowest of them), 0 none */
{
    uint32_t n;
    for (n = 1; n <= 5u; n++)
        if (op_held(n))
            return n;
    return 0;
}
static uint32_t arp_cur(void) { return ui.arp.cur % arp_steps(TSEL); }   /* (STEPS may have shrunk under it) */
/* the step the selected track's arp plays now, -1 none (no chord, ARP off) */
static int32_t arp_playing(void)
{
    const track_t *t = TSEL;
    if (!t->p[P_AMODE] || !arp_fed(t) || t->arp_idx == 0xFFFFFFFFu)   /* (keys, MIDI or the sequencer's chord) */
        return -1;
    return (int32_t)(t->arp_idx % arp_steps(t));
}
/* the OP keys' LEDs on an ARP page (ui_input.c key_leds): the five breathe (they can be pressed: SHIFT), one held is
 * lit fully. The lit ones; *br: the breathing ones */
static uint32_t op_leds(uint32_t *br)
{
    uint32_t held = fm1_in.notes & ~kb_layer & OP_KEYS;
    if (!op_page())
        return 0;
    if (br)
        *br |= OP_KEYS & ~held;
    return held;
}
/* the key LEDs on an ARP page: the rows with a note sounding over the cursor step (their keys), the held ones */
static uint32_t arp_leds(void)
{
    const arp_pat_t *p = &arp_pat[song.sel];
    uint32_t r, m = 0, c = arp_cur();
    for (r = 0; r < ARP_ROWS; r++)
        if (ash_find(p, r, c) >= 0 || ((ui.arp.down >> r) & 1u))
            m |= 1u << ARP_ROWKEY[r];
    return m;
}
static void arp_edited(void)                     /* the pattern is the user's now: PAT USER */
{
    arp_x[song.sel][ARPX_PAT] = 0;
    ui.force = 1;
}
static void arp_cursor_move(int32_t s)
{
    int32_t n = (int32_t)arp_steps(TSEL);
    ui.arp.cur = (uint8_t)((((int32_t)arp_cur() + s) % n + n) % n);
}
/* PAT turned (edit_param): factory pattern v - 1 with its STEPS and BOUND; USER (0) leaves the pattern as it is */
static void arp_factory(track_t *t, uint32_t v)
{
    const arp_factory_t *f;
    if (!v || v > ARP_NFACTORY)
        return;
    f = &ARP_FACTORY[v - 1u];
    fm1_irq_off();
    ash_load(&arp_pat[trk_index(t)], v - 1u);
    t->p[P_APROB] = f->steps;
    t->p[P_AORDER] = f->bound;
    fm1_irq_on();
    ui.force = 1;
    ui_say("PATTERN ", f->name);
}
/* STEPS turned (edit_param): the toy's rule for a shorter grid, notes past the end go, notes over it are cut */
static void arp_steps_set(track_t *t)
{
    fm1_irq_off();
    ash_fit(&arp_pat[trk_index(t)], arp_steps(t));
    fm1_irq_on();
}
/* EDIT on an ARP page: the notes starting on the cursor step go */
static void arp_clear_step(void)
{
    arp_pat_t *p = &arp_pat[song.sel];
    uint32_t i = 0, c = arp_cur(), n = 0;
    fm1_irq_off();
    while (i < p->n)
        if (ARP_STEP(p->note[i]) == c) {
            ash_drop(p, i);
            n++;
        } else {
            i++;
        }
    fm1_irq_on();
    if (n)
        arp_edited();
    ui_message(n ? "STEP CLEARED" : "NOTHING THERE");
}
/* the row keys (ui_input, song.grid 4): pressed (notes: this pass's new keys) a note is written at the cursor, or the
 * one there is marked to go; let go without a knob turned, it goes */
static void arp_keys(uint32_t notes)
{
    arp_pat_t *p = &arp_pat[song.sel];
    uint32_t r, bit, c = arp_cur();
    int32_t i;
    for (r = 0; r < 5u; r++)                     /* an OP key pressed: what its shift does (nothing else) */
        if ((notes >> OP_KEY[r]) & 1u)
            ui_message(OP_HINT[r]);
    for (r = 0; r < ARP_ROWS; r++) {
        bit = 1u << r;
        if ((notes >> ARP_ROWKEY[r]) & 1u) {
            ui.arp.down |= (uint8_t)bit;
            ui.arp.moved &= (uint8_t)~bit;
            ui.arp.made &= (uint8_t)~bit;
            if (ash_find(p, r, c) < 0) {
                int ok;
                fm1_irq_off();
                ok = ash_paint(p, r, c, 1);
                fm1_irq_on();
                if (!ok) {
                    ui_message("PATTERN FULL");
                    continue;
                }
                ui.arp.made |= (uint8_t)bit;
                arp_edited();
            }
        } else if ((ui.arp.down & bit) && !((fm1_in.notes >> ARP_ROWKEY[r]) & 1u)) {
            ui.arp.down &= (uint8_t)~bit;
            if (!((ui.arp.made | ui.arp.moved) & bit) && (i = ash_find(p, r, c)) >= 0) {
                fm1_irq_off();
                ash_drop(p, (uint32_t)i);
                fm1_irq_on();
                arp_edited();
            }
            ui.arp.made &= (uint8_t)~bit;
            ui.arp.moved &= (uint8_t)~bit;
        }
    }
}
/* a knob turned with row keys held: their notes at the cursor longer or shorter (1 .. to the end of STEPS). 1: taken */
static int arp_len_turn(int32_t s)
{
    arp_pat_t *p = &arp_pat[song.sel];
    uint32_t r, c = arp_cur(), steps = arp_steps(TSEL), len = 0;
    int32_t i;
    if (!arp_keys_on() || !ui.arp.down)
        return 0;
    for (r = 0; r < ARP_ROWS; r++) {
        uint32_t x, st;
        if (!((ui.arp.down >> r) & 1u) || (i = ash_find(p, r, c)) < 0)
            continue;
        x = p->note[i];
        st = ARP_STEP(x);
        len = (uint32_t)clamp((int32_t)ARP_LEN(x) + s, 1, (int32_t)(steps - st));
        fm1_irq_off();
        ash_paint(p, r, st, len);                    /* (the notes of the row it now reaches go: the toy's paint) */
        fm1_irq_on();
    }
    ui.arp.moved |= ui.arp.down;
    if (len) {
        char b[8];
        fmt_int(b, (int32_t)len);
        ui_say("LENGTH ", b);
        arp_edited();
    }
    return 1;
}
/* ZOOM's refusals (arp_shape.c AZ_*): the screen says why. NORATE: only 1/16T in (no 1/32T) and 1/4T out (no 1/2T) */
static const char *const AZ_MSG[] = {"", "1/32 IS THE FASTEST", "1/2 IS THE SLOWEST", "", "64 STEPS MAX", "STEPS ARE ODD",
                                     "NOTE BETWEEN STEPS"};
/* OP1 + OCT+ (in) / OCT- (out) on an ARP page (ui_layer.c op_shift): the toy's subdivide x2 / /2 (ash_subdivide) on the
 * selected track's pattern. STEPS and RATE double (or halve), every note's step and length with them: it sounds the
 * same, zoomed. The cursor stays on its place in the music (its step x2 or /2); an arp playing goes on from where it is
 * (its pass, the step it is in and how far into it). When it can't, nothing changes and the screen says why. Main loop
 * only, the ISR held off while the pattern, STEPS and RATE change together (a few dozen notes at most) */
static void arp_zoom(int in)
{
    track_t *t = TSEL;
    arp_pat_t *p = &arp_pat[song.sel];
    uint32_t was = arp_steps(t), steps = was, rate = (uint32_t)clamp(t->p[P_ARATE], 0, 7), code, s, pass, per;
    char b[24];
    fm1_irq_off();
    code = ash_subdivide(p, &steps, &rate, in);
    if (code == AZ_OK) {
        t->p[P_APROB] = (int16_t)steps;
        t->p[P_ARATE] = (int16_t)rate;
        if (t->arp_idx != 0xFFFFFFFFu && t->arp_pos < 0xFFFFFFFu) {   /* (playing, not a fresh chord's first step) */
            pass = t->arp_idx / was;
            s = t->arp_idx % was;
            per = arp_period(t);                     /* (the new RATE's step) */
            if (in) {
                s *= 2u;
                if (t->arp_pos >= per) {             /* (in the old step's second half: the new step after it) */
                    s++;
                    t->arp_pos -= per;
                }
            } else {
                if (s & 1u)                          /* (the second of a pair: half way into the joined step) */
                    t->arp_pos += per / 2u;
                s /= 2u;
            }
            t->arp_idx = pass * steps + s;
        }
    }
    fm1_irq_on();
    if (code != AZ_OK) {
        ui_message(code == AZ_NORATE ? (in ? "NO 1/32T RATE" : "NO 1/2T RATE") : AZ_MSG[code]);
        return;
    }
    ui.arp.cur = (uint8_t)(in ? (ui.arp.cur % was) * 2u : (ui.arp.cur % was) / 2u);
    fmt_int(b, (int32_t)steps);
    str_cpy(b + str_len(b), " STEPS ", sizeof b - str_len(b));
    str_cpy(b + str_len(b), N_ARATE[rate], sizeof b - str_len(b));
    ui_say("ZOOM ", b);                              /* "ZOOM 32 STEPS 1/32" */
    arp_edited();
}
/* OP2 SHIFT: the pattern one step right (+1) or left (-1), wrapping (ash_shift, the toy's shift) */
static void arp_shift(int32_t by)
{
    arp_pat_t *p = &arp_pat[song.sel];
    int ok;
    if (!p->n) {
        ui_message("NO NOTES");
        return;
    }
    fm1_irq_off();
    ok = ash_shift(p, arp_steps(TSEL), by);
    fm1_irq_on();
    if (!ok) {
        ui_message("PATTERN FULL");                  /* (a note split at the end: one more than ARP_NMAX) */
        return;
    }
    ui_message(by > 0 ? "SHIFT >" : "SHIFT <");
    arp_edited();
}
/* OP3 OCTAVE: the ARP's notes an octave up / down (arp_x OCT, -3..+3: seq.c arp_fire). A setting, not an edit: PAT
 * stays */
static void arp_octave(int up)
{
    char b[8];
    int32_t v = arp_x[song.sel][ARPX_OCT] + (up ? 1 : -1);
    if (v < -3 || v > 3) {
        ui_message(up ? "+3 OCT IS THE HIGHEST" : "-3 OCT IS THE LOWEST");
        return;
    }
    arp_x[song.sel][ARPX_OCT] = (int16_t)v;
    b[0] = v > 0 ? '+' : 0;
    b[1] = 0;
    fmt_int(b + (v > 0), v);
    ui_say("ARP OCT ", b);                           /* "ARP OCT +1" */
    ui.force = 1;
}
/* OP4 MIRROR: OCT+ the pattern backwards in time (ash_reverse), OCT- upside down (ash_flip); each its own inverse */
static void arp_mirror(int up)
{
    arp_pat_t *p = &arp_pat[song.sel];
    if (!p->n) {
        ui_message("NO NOTES");
        return;
    }
    fm1_irq_off();
    if (up)
        ash_reverse(p, arp_steps(TSEL));
    else
        ash_flip(p);
    fm1_irq_on();
    ui_message(up ? "REVERSED" : "FLIPPED");
    arp_edited();
}
/* OP5 MUTATE: OCT+ a note of length 1 on a random empty cell (a row on a step no note lasts over), OCT- a random note
 * goes. libc.c rng(), as SEQ TOOLS' RANDOM; destructive like every edit */
static void arp_mutate(int up)
{
    arp_pat_t *p = &arp_pat[song.sel];
    uint32_t steps = arp_steps(TSEL), n;
    if (!up && !p->n) {
        ui_message("NO NOTES");
        return;
    }
    if (up && p->n >= ARP_NMAX) {
        ui_message("PATTERN FULL");
        return;
    }
    fm1_irq_off();
    if (up) {
        n = ash_empty(p, steps, -1);
        if (n)
            ash_empty(p, steps, (int32_t)(rng() % n));
    } else {
        n = p->n;
        ash_drop(p, rng() % n);
    }
    fm1_irq_on();
    if (!n) {
        ui_message("NO EMPTY CELL");
        return;
    }
    ui_message(up ? "+ NOTE" : "- NOTE");
    arp_edited();
}
/* OP n held + OCT+ (up) / OCT- on an ARP page (ui_layer.c op_shift) */
static void arp_op(uint32_t n, int up)
{
    switch (n) {
    case 1: arp_zoom(up); break;
    case 2: arp_shift(up ? 1 : -1); break;
    case 3: arp_octave(up); break;
    case 4: arp_mirror(up); break;
    case 5: arp_mutate(up); break;
    default: break;
    }
}
/* OP n held: where its OCT- (bit 0) / OCT+ (bit 1) would do something, worked out on a copy (nothing changes): ZOOM
 * ash_subdivide's dry run; SHIFT with notes (and room for a split); OCT inside -3..+3; MIRROR with notes; MUTATE +
 * while there is room and an empty cell, - while there is a note */
static uint32_t arp_op_leds(uint32_t n)
{
    const track_t *t = TSEL;
    arp_pat_t c;
    uint32_t in, steps, rate, out = 0;
    const arp_pat_t *p = &arp_pat[song.sel];
    for (in = 0; in < 2u; in++) {
        c = *p;
        steps = arp_steps(t);
        rate = (uint32_t)clamp(t->p[P_ARATE], 0, 7);
        switch (n) {
        case 1: out |= (uint32_t)(ash_subdivide(&c, &steps, &rate, (int)in) == AZ_OK) << in; break;
        case 2: out |= (uint32_t)(p->n && ash_shift(&c, steps, in ? 1 : -1)) << in; break;
        case 3: out |= (uint32_t)(in ? arp_x[song.sel][ARPX_OCT] < 3 : arp_x[song.sel][ARPX_OCT] > -3) << in; break;
        case 4: out |= (uint32_t)(p->n != 0u) << in; break;
        case 5: out |= (uint32_t)(in ? p->n < ARP_NMAX && ash_empty(&c, steps, -1) : p->n != 0u) << in; break;
        default: break;
        }
    }
    return out;
}
