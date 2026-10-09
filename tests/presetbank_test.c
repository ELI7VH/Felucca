/* WaveLoop: bank size, legal parameters, unique names and usable patch voicings. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint32_t bank_hash_byte(uint32_t h, uint8_t b) { return (h ^ b) * 16777619u; }

/* Captured before retiring the bass IDs. Includes each retained ID, name and every
 * sound field, without pointer bytes or struct padding. */
static uint32_t retained_voice_hash(void)
{
    uint32_t h = 2166136261u, p, k;
    for (p = 0; p < ENG_FORMANT.npresets; p++) {
        const preset_t *pr = &FORMANT_PRESETS[p];
        if (formant_preset_orig(p) != p) continue;
        h = bank_hash_byte(h, (uint8_t)p);
        for (k = 0;; k++) { h = bank_hash_byte(h, (uint8_t)pr->name[k]); if (!pr->name[k]) break; }
        for (k = 0; k < 8; k++) h = bank_hash_byte(h, (uint8_t)pr->e[k]);
        for (k = 0; k < 4; k++) h = bank_hash_byte(h, pr->env[k]);
        h = bank_hash_byte(h, (uint8_t)pr->fenv); h = bank_hash_byte(h, pr->mono);
        for (k = 0; k < 4; k++) h = bank_hash_byte(h, pr->fx[k]);
        h = bank_hash_byte(h, pr->pat);
    }
    return h;
}

int main(void)
{
    const uint8_t banks[] = {0,2,3,5,6,7,8,9,11,12};
    unsigned b, p, k, q, shown, total = 0, fail = 0;
    for (b = 0; b < sizeof banks; ++b) {
        const engine_t *e = ENGINES[banks[b]];
        if (e->npresets != 32) { fprintf(stderr,"%s: expected 32 presets\n",e->name); ++fail; }
        shown = 0;
        for (p = 0; p < e->npresets; ++p) {
            const preset_t *pr = &e->presets[p];
            if (!pr->name[0] || strlen(pr->name) > 12) ++fail;
            if (e != &ENG_FORMANT || formant_preset_orig(p) == p) {
                shown++;
                for (q = 0; q < p; ++q)
                    if ((e != &ENG_FORMANT || formant_preset_orig(q) == q) && !strcmp(pr->name,e->presets[q].name)) ++fail;
            } else if (strcmp(pr->name, e->presets[0].name) || memcmp(pr->e, e->presets[0].e, sizeof pr->e) ||
                       memcmp(pr->env, e->presets[0].env, sizeof pr->env) || pr->fenv != e->presets[0].fenv ||
                       pr->mono != e->presets[0].mono || memcmp(pr->fx, e->presets[0].fx, sizeof pr->fx) ||
                       pr->pat != e->presets[0].pat) ++fail;
            for (k = 0; k < 8; ++k) {
                if (pr->e[k] < e->edit[k].min || pr->e[k] > e->edit[k].max) {
                    fprintf(stderr,"%s/%s: parameter %u outside range\n",e->name,pr->name,k); ++fail;
                }
            }
        }
        if (shown != (e == &ENG_FORMANT ? 26u : 32u)) { fprintf(stderr,"%s: wrong selectable count\n",e->name); ++fail; }
        total += shown;
    }
    if (retained_voice_hash() != 0xb0fad48eu) { fprintf(stderr,"VOICE: a retained preset ID or sound changed\n"); ++fail; }
    printf("WaveLoop banks: 10 engines, %u selectable presets, %u errors\n",total,fail);
    return fail ? 1 : 0;
}
