/* SPDX-License-Identifier: GPL-3.0-only
 * Yama-bruh port safety, controls, drum integration, and export for the independent
 * original-JS comparison in yama_reference.mjs. No baseline generated from this port.
 * cc -O1 -w -Ibuild/gen -Ifirmware/src -o build/host/yama_test tests/yama_test.c -lm
 * build/host/yama_test build/yama_demo
 * node tests/yama_reference.mjs build/yama_reference build/yama_demo */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define YN (FS * 5u)
static int failures;
static int32_t A[YN], B[YN];
static const char *const YNAMES[9] = {
    "kick", "snare", "clap", "hihat_c", "hihat_o", "tom", "rimshot", "cowbell", "cymbal"
};
static void check(int yes, const char *what, uint32_t bank, uint32_t drum)
{
    if (!yes) {
        printf("yama_test: FAIL bank %u %s: %s\n", bank, YNAMES[drum], what);
        failures++;
    }
}

/* A direct voice render includes exactly the same dv_run path used by eng_drum.
 * n is arbitrary; optional retrigger/choke positions must land on CTL boundaries. */
static uint32_t yrender(const dv_param_t *p, int32_t *out, uint32_t n, uint32_t retrigger, uint32_t choke)
{
    dv_coef_t c;
    dv_voice_t v;
    uint32_t stop = 0;
    dv_setup(&c, p);
    dv_init(&v, c.type);
    dv_trigger(&v);
    for (uint32_t i = 0; i < n; i += CTL) {
        uint32_t count = n - i < CTL ? n - i : CTL;
        if (retrigger && i == retrigger) dv_trigger(&v);
        if (choke && i == choke) dv_choke(&v);
        dv_run(&c, &v, 0, out + i, count);
        if (!v.live && !stop) stop = i + count;
    }
    return stop;
}
static double energy(const int32_t *y, uint32_t n)
{
    double e = 0;
    for (uint32_t i = 0; i < n; i++) e += (double)y[i] * y[i];
    return e;
}
static double mean_time(const int32_t *y, uint32_t n)
{
    double e = 0, weighted = 0;
    for (uint32_t i = 0; i < n; i++) {
        double x = (double)y[i] * y[i];
        e += x; weighted += x * i / FS;
    }
    return weighted / (e + 1e-20);
}
static double change(const int32_t *a, const int32_t *b, uint32_t n)
{
    double e = 0;
    for (uint32_t i = 0; i < n; i++) e += ((double)a[i] - b[i]) * ((double)a[i] - b[i]);
    return e / (energy(a, n) + 1e-20);
}
static void bounded(const int32_t *y, uint32_t n, uint32_t bank, uint32_t drum)
{
    int32_t peak = 0;
    double sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t a = y[i] < 0 ? -y[i] : y[i];
        if (a > peak) peak = a;
        sum += y[i];
    }
    check(peak > 0 && peak < 32767, "audible and below full scale", bank, drum);
    check(fabs(sum / n) < peak * .01, "DC under 1% of peak", bank, drum);
}
static void save_wav(const char *dir, const char *name, const int32_t *y, uint32_t n)
{
    if (!dir) return;
    char filename[1024];
    snprintf(filename, sizeof filename, "%s/%s.wav", dir, name);
    FILE *f = fopen(filename, "wb");
    if (!f) { perror(filename); exit(2); }
    wav_hdr(f, n);
    for (uint32_t i = 0; i < n; i++) wav_put(f, y[i], y[i]);
    fclose(f);
}

static void voices(const char *dir)
{
    for (uint32_t bank = 0; bank < 8; bank++) for (uint32_t drum = 0; drum < 9; drum++) {
        dv_param_t p;
        dv_default(&p, DV_YTYPE(bank, drum));
        uint32_t stop = yrender(&p, A, YN, 0, 0);
        bounded(A, YN, bank, drum);
        check(stop && stop < YN, "default voice ends within five seconds", bank, drum);
        check(energy(A + YN - FS / 10u, FS / 10u) == 0, "silent final 100 ms", bank, drum);
        char name[100];
        snprintf(name, sizeof name, "bank%u-%s", bank, YNAMES[drum]);
        save_wav(dir, name, A, YN);

        uint32_t at = CTL * 689u;
        yrender(&p, B, at * 2u, at, 0);
        check(!memcmp(A, B + at, sizeof *A * at), "retrigger resets phase/noise/envelopes", bank, drum);

        p.decay = 32; yrender(&p, A, FS * 2u, 0, 0);
        p.decay = 96; yrender(&p, B, FS * 2u, 0, 0);
        check(mean_time(B, FS * 2u) > mean_time(A, FS * 2u) * 1.3,
              "decay moves audible energy later", bank, drum);
        for (uint32_t control = 0; control < 3; control++) {
            dv_default(&p, DV_YTYPE(bank, drum));
            if (control == 0) p.tune = -48;
            if (control == 1) p.tone = 0;
            if (control == 2) p.extra = 0;
            yrender(&p, A, FS / 2u, 0, 0);
            if (control == 0) p.tune = 48;
            if (control == 1) p.tone = 127;
            if (control == 2) p.extra = 127;
            yrender(&p, B, FS / 2u, 0, 0);
            check(change(A, B, FS / 2u) > 0.001,
                  control == 0 ? "TUNE changes sound" : control == 1 ? "TONE changes sound" : "SNAP changes sound",
                  bank, drum);
        }
        /* Four opposing stressful settings per voice: sign extremes and maximum
         * output gain. UBSan catches intermediate overflow before clipping hides it. */
        for (uint32_t corner = 0; corner < 4; corner++) {
            dv_default(&p, DV_YTYPE(bank, drum));
            p.tune = corner & 1 ? 400 : -400;
            p.decay = corner & 2 ? 127 : 0;
            p.tone = corner & 1 ? 127 : 0;
            p.extra = corner & 2 ? 0 : 127;
            p.level = p.accent = 127;
            yrender(&p, A, YN, 0, 0);
            bounded(A, YN, bank, drum);
        }
    }
}

static void mapping_and_choke(void)
{
    /* Independent expected GM voice routing. Cymbals share the bell sequencer lane
     * but must retain their cymbal timbre when a GM cymbal note is struck. */
    static const uint8_t gm[47] = {
        0,0,6,1,2,1,5,3,5,3,5,4,5,5,8,5,8,8,7,3,8,7,8,6,8,5,5,5,5,5,5,5,7,7,3,3,6,6,3,3,6,6,6,5,5,7,7
    };
    static const int8_t semis[47] = {
        -2,0,0,0,0,2,-7,0,-4,-2,0,0,3,5,0,8,-3,2,5,5,4,0,1,-12,-2,5,2,0,0,-5,10,7,7,3,3,7,7,5,-4,-6,0,-4,-7,7,3,12,12
    };
    for (uint32_t bank = 0; bank < 8; bank++) {
        host_tracks_init();
        host_preset(&trk[0], ENGI_DRUM, 3 + bank);
        trk[0].p[P_SUS] = 127;
        for (uint32_t note = 0; note < 128; note++) {
            int32_t st;
            uint32_t n = note >= 35 && note <= 81 ? note : 36 + (note + 120 - 36) % 12;
            uint32_t type = drum_gm(trk[0].p, note, &st);
            check(type == DV_YTYPE(bank, gm[n - 35]), "GM maps to bank and correct timbre", bank, gm[n - 35]);
            check(st == semis[n - 35], "GM pitch offsets preserved", bank, gm[n - 35]);
        }
        trk_note_on(&trk[0], 46, 100);
        int32_t out[2 * CTL];
        for (uint32_t i = 0; i < 20; i++) mix_block(out, CTL);
        trk_note_on(&trk[0], 42, 100);
        for (uint32_t i = 0; i < 20; i++) mix_block(out, CTL);
        check(!drum_kit[0][DV_HATO].v.live && drum_kit[0][DV_HATC].v.live,
              "closed hat chokes open-hat lane within 15 ms", bank, DV_HATO);

        /* Custom mix and sound edits survive the payload used in songs/user sounds,
         * and editing one selected lane does not alter another lane or track. */
        uint8_t stored[128];
        drum_focus[0] = DV_SNARE;
        *drum_param_ref(&trk[0], P_E1, 1) = 93;
        *drum_param_ref(&trk[0], P_LEVEL, 1) = 76;
        *drum_param_ref(&trk[0], P_PAN, 1) = -25;
        *drum_param_ref(&trk[0], P_DLY, 1) = 42;
        drum_controls_pack(&trk[0], stored);
        drum_controls_reset(&trk[0]);
        check(drum_controls_unpack(&trk[0], stored) && drum_focus[0] == DV_SNARE,
              "per-drum payload restores selected lane", bank, DV_SNARE);
        check(drum_value(&trk[0], DV_SNARE, P_E1) == 93 && drum_value(&trk[0], DV_SNARE, P_LEVEL) == 76 &&
              drum_value(&trk[0], DV_SNARE, P_PAN) == -25 && drum_value(&trk[0], DV_SNARE, P_DLY) == 42,
              "per-drum sound/volume/pan/send round trip", bank, DV_SNARE);
        check(drum_value(&trk[0], DV_KICK, P_E1) == 64 && drum_value(&trk[0], DV_KICK, P_LEVEL) == 112 &&
              !drum_control[1].mask[DV_SNARE], "edits stay on selected drum and track", bank, DV_SNARE);
    }
}

static void beat_demo(const char *dir)
{
    if (!dir) return;
    char name[1024];
    snprintf(name, sizeof name, "%s/Yama-bruh-Standard-beat.wav", dir);
    FILE *f = fopen(name, "wb");
    if (!f) { perror(name); exit(2); }
    enum { STEPS = 32, STEP = FS / 8u, TOTAL = STEPS * STEP + FS * 2u };
    dv_coef_t c[9]; dv_voice_t v[9];
    for (uint32_t drum = 0; drum < 9; drum++) {
        dv_param_t p; dv_default(&p, DV_YTYPE(0, drum));
        dv_setup(&c[drum], &p); dv_init(&v[drum], c[drum].type);
    }
    wav_hdr(f, TOTAL);
    for (uint32_t i = 0; i < TOTAL;) {
        uint32_t step = i / STEP, count = STEP - i % STEP;
        if (count > CTL) count = CTL;
        if (count > TOTAL - i) count = TOTAL - i;
        if (i % STEP == 0 && step < STEPS) {
            uint32_t s = step % 16;
            if (s == 0 || s == 6 || s == 10) dv_trigger(&v[DV_KICK]);
            if (s == 4 || s == 12) dv_trigger(&v[DV_SNARE]);
            if (s == 12) dv_trigger(&v[DV_CLAP]);
            if (s == 14) dv_trigger(&v[DV_HATO]);
            else if (!(s & 1)) { dv_trigger(&v[DV_HATC]); dv_choke(&v[DV_HATO]); }
            if (s == 3 || s == 11) dv_trigger(&v[DV_BELL]);
            if (step == 29 || step == 31) dv_trigger(&v[DV_TOM]);
            if (step == 0) dv_trigger(&v[DV_YCYM]);
        }
        int32_t mix[CTL] = {0}, y[CTL];
        for (uint32_t drum = 0; drum < 9; drum++) {
            dv_run(&c[drum], &v[drum], 0, y, count);
            for (uint32_t k = 0; k < count; k++) mix[k] += y[k] / 2;
        }
        for (uint32_t k = 0; k < count; k++) {
            check(abs(mix[k]) < 32767, "demo below full scale", 0, 0);
            wav_put(f, mix[k], mix[k]);
        }
        i += count;
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : 0;
    voices(dir);
    mapping_and_choke();
    beat_demo(dir);
    printf("yama_test: %s — 72 defaults, reference exports, controls, bounds, retrigger, GM map, hat choke, per-drum save payload\n",
           failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
