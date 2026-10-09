/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* ANALOG: two band-limited oscillators (saw / square / tri / sine / PWM),
 * noise, drive and a trapezoidal low-pass. */
static const char *const N_ANALOG_WAVE[] = {"SAW", "SQR", "TRI", "SIN", "PWM"};

static void analog_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = v->ph[0] + 0x40000000u;
    v->s[0] = v->s[1] = 0;                            /* filter */
    if (!v->s[2])
        v->s[2] = 0x1234567 + (int32_t)v->age;        /* noise state */
}

static void analog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0], i;
    int32_t det = p[P_E1], mix = p[P_E2], noise = p[P_E3];
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tsvf_t flt;
    int32_t drive = 32768 + p[P_E6] * 512;                       /* 1x .. 3x */
    uint32_t inc1 = m->inc;
    uint32_t inc2 = det ? cents_inc(m->pitch16, det, 0) : inc1;      /* DTN in cents */
    uint32_t pw = 0x80000000u + (uint32_t)((m->shape - (64 << 8)) << 15);
    int32_t m2 = mix * 258, m1 = 32767 - m2;                    /* osc mix Q15 */
    int32_t nz = noise * 200, drv = p[P_E6];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1];                  /* state in locals: out[] may alias v->s[] */
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2];
    tsvf_coef(&flt, cut, p[P_E5]);
    for (i = 0; i < n; i++) {
        int32_t a, b, s;
        switch (wave) {
        case 1:
            a = osc_pulse(ph0, inc1, 0x80000000u);
            b = osc_pulse(ph1, inc2, 0x80000000u);
            break;
        case 2:
            a = osc_tri(ph0);
            b = osc_tri(ph1);
            break;
        case 3:
            a = osc_sine(ph0);
            b = osc_sine(ph1);
            break;
        case 4:
            a = osc_pulse(ph0, inc1, pw);
            b = osc_pulse(ph1, inc2, pw);
            break;
        default:
            a = osc_saw(ph0, inc1);
            b = osc_saw(ph1, inc2);
            break;
        }
        ph0 += inc1;
        ph1 += inc2;
        s = mulq15(a, m1) + mulq15(b, m2);
        if (nz)
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        if (drv)
            s = softclip(((s >> 2) * (drive >> 2)) >> 11);   /* pre-shifts: drive is up to 3x, no overflow */
        {   /* filter, linear up to half scale, then a soft knee (only resonance peaks saturate); soft_knee()
             * written out: the host compiler makes this loop 1 % slower with the call */
            int32_t y = tsvf_lp(&flt, s >> 1, &ic1, &ic2), a = y < 0 ? -y : y;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                y = y < 0 ? -a : a;
            }
            s = y << 1;
        }
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
}

static const preset_t ANALOG_PRESETS[] = {
    {"SAW LEAD", {0, 12, 64, 0, 90, 30, 10, 64}, {4, 70, 100, 50}, 20, 1, FX(0, 10, 45, 30), PAT(4)},
    {"SOFT PAD", {0, 20, 64, 4, 60, 10, 0, 32}, {80, 90, 110, 95}, 10, 0, FX(0, 60, 20, 70), PAT(5)},
    {"SQR BASS", {1, 0, 0, 0, 50, 70, 40, 64}, {0, 60, 40, 30}, 30, 1, FX(5, 0, 10, 10), PAT(2)},
    {"PWM STR", {4, 8, 40, 0, 75, 20, 0, 48}, {60, 80, 110, 85}, 8, 0, FX(0, 50, 20, 60), PAT(5)},
    {"ACID", {0, 0, 0, 0, 50, 100, 25, 64}, {0, 55, 20, 30}, 48, 1, FX(20, 0, 45, 15), PAT(1)},
    {"SINE KEY", {3, 6, 50, 0, 127, 0, 0, 0}, {2, 80, 30, 70}, 0, 0, FX(0, 30, 25, 40), PAT(6)},
    {"RAVE", {4, 30, 64, 0, 85, 20, 30, 64}, {20, 80, 110, 60}, 10, 1, FX(30, 40, 30, 30), PAT(13)},
    {"SUB BASS", {3, 0, 0, 0, 40, 0, 20, 0}, {0, 60, 100, 20}, 0, 1, FX(0, 0, 0, 10), PAT(8)},
    {"PLUCK", {0, 8, 50, 0, 30, 40, 0, 64}, {0, 88, 0, 60}, 55, 0, FX(0, 20, 50, 30), PAT(3)},
    {"BRASS", {0, 10, 64, 0, 45, 20, 10, 64}, {35, 70, 90, 45}, 40, 0, FX(0, 20, 20, 40), PAT(6)},
    {"WIND", {0, 0, 0, 90, 30, 90, 0, 0}, {60, 90, 60, 80}, 50, 0, FX(0, 30, 30, 70), PAT(5)},
    {"STRINGS", {0, 25, 64, 0, 70, 10, 0, 32}, {70, 90, 115, 90}, 5, 0, FX(0, 60, 20, 70), PAT(5)},
    /* WaveLoop mixed bank: appended so existing preset IDs keep their sounds. */
    {"ROUND BASS", {3, 6, 32, 0, 48, 12, 12, 48}, {0, 58, 88, 25}, 32, 1, FX(2, 0, 8, 10), PAT(2)},
    {"RUBBER BASS", {1, 12, 42, 0, 52, 20, 12, 48}, {0, 58, 88, 25}, 32, 1, FX(2, 0, 8, 10), PAT(2)},
    {"DEEP BASS", {3, 18, 52, 0, 56, 28, 12, 48}, {0, 58, 88, 25}, 32, 1, FX(2, 0, 8, 10), PAT(2)},
    {"REED BASS", {2, 24, 62, 0, 60, 36, 12, 48}, {0, 58, 88, 25}, 32, 1, FX(2, 0, 8, 10), PAT(2)},
    {"WARM PAD", {0, 6, 32, 0, 65, 12, 0, 64}, {66, 90, 112, 85}, 0, 0, FX(0, 38, 20, 55), PAT(5)},
    {"AIR PAD", {1, 12, 42, 0, 69, 20, 0, 64}, {66, 90, 112, 85}, 0, 0, FX(0, 38, 20, 55), PAT(5)},
    {"GLASS PAD", {4, 18, 52, 0, 73, 28, 0, 64}, {66, 90, 112, 85}, 0, 0, FX(0, 38, 20, 55), PAT(5)},
    {"DUSK PAD", {2, 24, 62, 0, 77, 36, 0, 64}, {66, 90, 112, 85}, 0, 0, FX(0, 38, 20, 55), PAT(5)},
    {"SOFT KEYS", {0, 6, 32, 0, 95, 12, 0, 64}, {1, 78, 36, 62}, 0, 0, FX(0, 14, 18, 35), PAT(6)},
    {"BRIGHT KEYS", {1, 12, 42, 0, 99, 20, 0, 64}, {1, 78, 36, 62}, 0, 0, FX(0, 14, 18, 35), PAT(6)},
    {"MALLET KEYS", {4, 18, 52, 0, 103, 28, 0, 64}, {1, 78, 36, 62}, 0, 0, FX(0, 14, 18, 35), PAT(6)},
    {"DREAM KEYS", {2, 24, 62, 0, 107, 36, 0, 64}, {1, 78, 36, 62}, 0, 0, FX(0, 14, 18, 35), PAT(6)},
    {"ROUND LEAD", {0, 6, 32, 0, 95, 12, 0, 64}, {4, 65, 98, 38}, 0, 1, FX(6, 10, 28, 25), PAT(4)},
    {"CUT LEAD", {1, 12, 42, 0, 99, 20, 0, 64}, {4, 65, 98, 38}, 0, 1, FX(6, 10, 28, 25), PAT(4)},
    {"SING LEAD", {4, 18, 52, 0, 103, 28, 0, 64}, {4, 65, 98, 38}, 0, 1, FX(6, 10, 28, 25), PAT(4)},
    {"FIFTH LEAD", {2, 24, 62, 0, 107, 36, 0, 64}, {4, 65, 98, 38}, 0, 1, FX(6, 10, 28, 25), PAT(4)},
    {"DUST CLOUD", {0, 6, 32, 20, 65, 12, 0, 64}, {80, 100, 104, 90}, 0, 0, FX(0, 30, 36, 60), PAT(5)},
    {"METAL AIR", {1, 12, 42, 26, 69, 20, 0, 64}, {80, 100, 104, 90}, 0, 0, FX(0, 30, 36, 60), PAT(5)},
    {"SLOW BLOOM", {4, 18, 52, 32, 73, 28, 0, 64}, {80, 100, 104, 90}, 0, 0, FX(0, 30, 36, 60), PAT(5)},
    {"NIGHT DRIFT", {2, 24, 62, 38, 77, 36, 0, 64}, {80, 100, 104, 90}, 0, 0, FX(0, 30, 36, 60), PAT(5)},
};

static const engine_t ENG_ANALOG = {
    .name = "ANALOG",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"WAVE", F_ENUM, 0, 4, 0, N_ANALOG_WAVE, 0},
        {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = ANALOG_PRESETS,
    .npresets = NELEM(ANALOG_PRESETS),
    .note_on = analog_note_on,
    .render = analog_render,
    .knob = {P_E4, P_E5, P_ATK, P_REL},
    .keep = 0x03,                /* the filter */
};
