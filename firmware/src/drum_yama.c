/* SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Lucian Labs
 * Fixed-point adaptation for WaveLoop / Felucca, 2026.
 * Derived from Yama-bruh www/drum-worklet.js at
 * 3eca861383e63e6c507c30402a204499faf79887. See LICENSES/MIT-YamaBruh.txt.
 *
 * Nine sounds in each of eight banks, two sine operators, white noise, a 2 ms
 * click, exponential pitch/amplitude and the original three clap teeth. This
 * is the Yama-bruh synthesizer, not a register-level Yamaha chip emulation.
 * Reuses dv_coef_t/dv_voice_t: no additional per-lane storage. Coefficients are
 * calculated only on parameter changes; the sample loop is integer-only.
 */
#define DV_NYAMA 8u
#define DV_YCYM 8u
#define DV_YTYPE(bank, sound) DV_KTYPE(DV_NKIT + 1u + (bank), (sound))
#define DV_IS_YAMA(type) (DV_KITOF(type) - (DV_NKIT + 1u) < DV_NYAMA && ((type) & 15u) <= DV_YCYM)
#define DVT_YAMA 255u

typedef struct {
    uint16_t carrier2, mod2, sweep2, decay_ms; /* frequencies in half-Hz units; source decay, not tau */
    uint8_t index10, pitch_ms, noise_pct, click_pct, gap_ms;
} dv_yama_design_t;

/* Actual worklet defaults, with bank overrides applied at note 0. Order:
 * Standard, Electronic, Power, Brush, Orchestra, Synth, Latin, Lo-Fi.
 * The cymbal is 940/6580 Hz, 0.8 s, noise .4 (drums.js has stale UI defaults).
 * The nine columns are the eight FM-1 lanes, then CYM sharing BELL's owner. */
static const dv_yama_design_t DV_YAMA[DV_NYAMA][9] = {
    {
        {120, 180, 320, 250, 30, 15, 0, 30, 0}, /* kick */
        {400, 680, 120, 180, 25, 10, 60, 15, 0}, /* snare */
        {2400, 4800, 0, 200, 15, 10, 85, 0, 12}, /* clap */
        {1600, 11200, 0, 40, 40, 10, 50, 0, 0}, /* hihat_c */
        {1600, 11200, 0, 220, 40, 10, 50, 0, 0}, /* hihat_o */
        {330, 495, 165, 220, 20, 20, 0, 10, 0}, /* tom */
        {1000, 3200, 400, 60, 20, 5, 20, 50, 0}, /* rimshot */
        {1174, 1658, 0, 120, 18, 10, 0, 10, 0}, /* cowbell */
        {1880, 13160, 0, 800, 50, 10, 40, 0, 0}, /* cymbal */
    },
    {
        {120, 180, 440, 400, 15, 15, 0, 10, 0}, /* kick */
        {400, 680, 120, 220, 15, 10, 75, 5, 0}, /* snare */
        {2400, 4800, 0, 250, 15, 10, 90, 0, 12}, /* clap */
        {2400, 15600, 0, 25, 50, 10, 50, 0, 0}, /* hihat_c */
        {2400, 15600, 0, 350, 50, 10, 50, 0, 0}, /* hihat_o */
        {330, 495, 80, 350, 10, 20, 0, 10, 0}, /* tom */
        {1000, 3200, 400, 60, 20, 5, 20, 50, 0}, /* rimshot */
        {1174, 1658, 0, 120, 18, 10, 0, 10, 0}, /* cowbell */
        {1880, 13160, 0, 800, 50, 10, 40, 0, 0}, /* cymbal */
    },
    {
        {120, 180, 400, 350, 40, 15, 0, 50, 0}, /* kick */
        {400, 680, 120, 250, 35, 10, 50, 30, 0}, /* snare */
        {2400, 4800, 0, 200, 15, 10, 85, 0, 12}, /* clap */
        {1600, 11200, 0, 40, 40, 10, 50, 0, 0}, /* hihat_c */
        {1600, 11200, 0, 220, 40, 10, 50, 0, 0}, /* hihat_o */
        {330, 495, 200, 300, 30, 20, 0, 20, 0}, /* tom */
        {1000, 3200, 400, 60, 20, 5, 20, 50, 0}, /* rimshot */
        {1174, 1658, 0, 120, 18, 10, 0, 10, 0}, /* cowbell */
        {1880, 13160, 0, 1200, 50, 10, 40, 0, 0}, /* cymbal */
    },
    {
        {120, 180, 160, 150, 15, 15, 0, 10, 0}, /* kick */
        {400, 680, 120, 300, 8, 10, 85, 0, 0}, /* snare */
        {2400, 4800, 0, 200, 15, 10, 85, 0, 12}, /* clap */
        {1600, 11200, 0, 60, 25, 10, 70, 0, 0}, /* hihat_c */
        {1600, 11200, 0, 300, 25, 10, 70, 0, 0}, /* hihat_o */
        {330, 495, 165, 200, 12, 20, 10, 10, 0}, /* tom */
        {1000, 3200, 400, 60, 20, 5, 40, 30, 0}, /* rimshot */
        {1174, 1658, 0, 120, 18, 10, 0, 10, 0}, /* cowbell */
        {1880, 13160, 0, 800, 50, 10, 40, 0, 0}, /* cymbal */
    },
    {
        {100, 180, 60, 500, 10, 15, 0, 5, 0}, /* kick */
        {560, 680, 120, 150, 18, 10, 30, 20, 0}, /* snare */
        {2400, 4800, 0, 200, 15, 10, 85, 0, 12}, /* clap */
        {1600, 11200, 0, 40, 40, 10, 50, 0, 0}, /* hihat_c */
        {1600, 11200, 0, 220, 40, 10, 50, 0, 0}, /* hihat_o */
        {330, 495, 40, 450, 10, 20, 0, 5, 0}, /* tom */
        {1000, 3200, 400, 60, 20, 5, 20, 50, 0}, /* rimshot */
        {1174, 1658, 0, 120, 18, 10, 0, 10, 0}, /* cowbell */
        {1400, 13160, 0, 1500, 60, 10, 40, 0, 0}, /* cymbal */
    },
    {
        {110, 180, 600, 180, 50, 8, 0, 60, 0}, /* kick */
        {500, 680, 120, 120, 40, 10, 40, 40, 0}, /* snare */
        {2400, 4800, 0, 120, 15, 10, 95, 0, 8}, /* clap */
        {3000, 18000, 0, 20, 60, 10, 50, 0, 0}, /* hihat_c */
        {3000, 18000, 0, 150, 60, 10, 50, 0, 0}, /* hihat_o */
        {330, 495, 240, 150, 35, 20, 0, 30, 0}, /* tom */
        {1000, 3200, 400, 60, 20, 5, 20, 50, 0}, /* rimshot */
        {1400, 2000, 0, 120, 25, 10, 0, 10, 0}, /* cowbell */
        {1880, 13160, 0, 800, 50, 10, 40, 0, 0}, /* cymbal */
    },
    {
        {160, 180, 100, 200, 15, 15, 0, 15, 0}, /* kick */
        {600, 680, 120, 100, 15, 10, 20, 35, 0}, /* snare */
        {2400, 4800, 0, 200, 15, 10, 85, 0, 12}, /* clap */
        {1600, 11200, 0, 40, 40, 10, 50, 0, 0}, /* hihat_c */
        {1600, 11200, 0, 220, 40, 10, 50, 0, 0}, /* hihat_o */
        {330, 495, 60, 150, 15, 10, 0, 25, 0}, /* tom */
        {1000, 3200, 400, 40, 20, 5, 10, 70, 0}, /* rimshot */
        {1174, 1658, 0, 80, 18, 10, 0, 10, 0}, /* cowbell */
        {1880, 13160, 0, 800, 50, 10, 40, 0, 0}, /* cymbal */
    },
    {
        {120, 180, 320, 200, 60, 15, 0, 15, 0}, /* kick */
        {400, 680, 120, 150, 50, 10, 70, 15, 0}, /* snare */
        {2400, 4800, 0, 200, 15, 10, 85, 0, 12}, /* clap */
        {1600, 11200, 0, 30, 70, 10, 50, 0, 0}, /* hihat_c */
        {1600, 11200, 0, 180, 70, 10, 50, 0, 0}, /* hihat_o */
        {330, 495, 165, 200, 40, 20, 0, 10, 0}, /* tom */
        {1000, 3200, 400, 60, 20, 5, 20, 50, 0}, /* rimshot */
        {1174, 1658, 0, 120, 18, 10, 0, 10, 0}, /* cowbell */
        {1880, 13160, 0, 800, 50, 10, 40, 0, 0}, /* cymbal */
    },
};

/* exp(-1 / (tau * fs)), Q24. Third-order expansion; tau >= 1.5 ms.
 * Q24 factors preserve long cymbal tails more accurately than Q16. */
static uint32_t dv_yama_k(uint32_t us)
{
    /* Fold FS first: floor(floor(A / FS) / us) == floor(A / (FS * us)).
     * The runtime division fits 32 bits; the target has no 64-bit division helper. */
    uint32_t x = (uint32_t)(16777216000000ull / FS) / us;
    uint32_t x2 = (uint32_t)(((uint64_t)x * x) >> 24);
    return 16777216u - x + x2 / 2u - (uint32_t)(((uint64_t)x2 * x) >> 24) / 6u;
}

/* Q30 envelope * Q24 factor, with 32-bit products only (error < 2 Q30 units). */
static inline int32_t dv_yama_decay(int32_t e, uint32_t k)
{
    return mulq16(e, k >> 8) + (((e >> 16) * (int32_t)(k & 255u)) >> 8);
}

static uint32_t dv_yama_hz(uint32_t hz2, uint32_t ratio)
{
    return (uint32_t)(((uint64_t)hz2 * ratio * 32768u + FS / 2u) / FS);
}

static void dv_yama_setup(dv_coef_t *c, const dv_param_t *p)
{
    const dv_yama_design_t *d = &DV_YAMA[DV_KITOF(p->type) - DV_NKIT - 1u][p->type & 15u];
    uint32_t i, ratio = dv_exp2((uint32_t)(clamp(p->tune, -768, 768) + 768) * 4096u / 192u) >> 4;
    uint32_t decay = dv_exp2((uint32_t)p->decay * 128u) >> 2; /* .25 .. 3.91, exactly 1 at 64 */
    uint32_t tau = (uint32_t)(((uint64_t)d->decay_ms * 400u * decay) >> 16);
    for (i = 0; i < sizeof *c / 4u; i++) ((uint32_t *)c)[i] = 0;
    c->type = DVT_YAMA;
    c->inc[0] = dv_yama_hz(d->carrier2, ratio);
    c->inc[1] = dv_yama_hz(d->mod2, ratio);
    c->span[0] = (uint32_t)(((uint64_t)dv_yama_hz(d->sweep2, ratio) * p->extra) / 64u);
    c->k[0] = dv_yama_k(tau);
    c->k[1] = dv_yama_k((uint32_t)d->pitch_ms * 1000u);
    c->k[2] = dv_yama_k(8000u);
    c->kb[0] = dv_yama_k(1500u); /* native closed-hat choke */
    /* sin(Q15) * index * 2^32 / (2 pi * 32768): unsigned product wraps phase intentionally. */
    c->g[0] = (int32_t)(((uint32_t)d->index10 * 2086076u + 500u) / 1000u * p->tone / 64u);
    c->g[1] = (int32_t)d->noise_pct * 32768 / 100;
    if (!d->sweep2 && !d->click_pct && !d->gap_ms)
        c->g[1] = clamp(c->g[1] * p->extra / 64, 0, 32768); /* SNAP: hat/cymbal noise balance */
    c->g[2] = (int32_t)(((uint64_t)d->click_pct * 16777216u * p->extra) / 6400u); /* Q24 click */
    c->g[3] = (int32_t)(((uint64_t)(uint32_t)c->g[2] * 500u + FS / 2u) / FS);
    /* Clap spacing in tenths of a sample: exact .012/.008 seconds at 44.1 kHz. */
    c->hold = (uint16_t)((uint32_t)d->gap_ms * 441u * (p->extra + 64u) / 128u);
    c->out = (int32_t)p->level * 4096 / 100;
    c->out += (c->out * p->accent * 75) >> 14;
}

static __attribute__((noinline)) void dv_yama_run(const dv_coef_t *c, dv_voice_t *v, int32_t *y, uint32_t n)
{
    uint32_t i;
    if (v->trig) {
        for (i = 0; i < sizeof *v / 4u; i++) ((uint32_t *)v)[i] = 0;
        v->type = DVT_YAMA;
        v->live = 1;
        v->rng = 1;
        v->q[0] = v->q[1] = 1 << 30;
        v->e[0] = 1u << 30;
        v->s[0] = c->g[2];
    }
    for (i = 0; i < n; i++) {
        int32_t carrier, body, s, clap = 32768;
        uint32_t sweep;
        if (!v->live || v->type != DVT_YAMA || v->q[0] < 1073742) {
            v->live = 0;
            y[i] = 0;
            continue;
        }
        carrier = sine_i(v->ph[0] + (uint32_t)sine_i(v->ph[1]) * (uint32_t)c->g[0]);
        body = (carrier * (32768 - c->g[1])) >> 15;
        if (c->g[1]) {
            uint32_t rng = (uint32_t)v->rng;
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            if (!rng) rng = 1;
            v->rng = (int32_t)rng;
            body += (((int32_t)((rng & 0x7FFFFFFFu) >> 15) - 32768) * c->g[1]) >> 15;
        }
        if (c->hold && v->cnt < 3u) {
            if (v->n >= c->hold) {
                v->n = (uint16_t)(v->n - c->hold);
                v->cnt++;
                v->e[0] = 1u << 30;
            }
            if (v->cnt < 3u) clap = (int32_t)(v->e[0] >> 15);
            v->n += 10u;
            v->e[0] = (uint32_t)dv_yama_decay((int32_t)v->e[0], c->k[2]);
        }
        /* Original half gain, DC-bearing linear click and hard limiter. The
         * native engine applies its normal velocity/mix protection afterward. */
        s = (((body * (v->q[0] >> 15)) >> 15) + (v->s[0] >> 9)) >> 1;
        s = (s * clap) >> 15;
        y[i] = clamp((s * c->out) >> 12, -31130, 31130);
        if (v->s[0] > 0) v->s[0] = clamp(v->s[0] - c->g[3], 0, 1 << 26);
        sweep = (uint32_t)mulq16((int32_t)c->span[0], (uint32_t)v->q[1] >> 14);
        v->ph[0] += c->inc[0] + sweep;
        v->ph[1] += c->inc[1] + (sweep >> 1);
        v->q[0] = dv_yama_decay(v->q[0], v->choke ? c->kb[0] : c->k[0]);
        v->q[1] = dv_yama_decay(v->q[1], c->k[1]);
    }
}
