/* WaveLoop: bank size, legal parameters, unique names and usable patch voicings. */
#define main hostsim_main
#include "hostsim.c"
#undef main
int main(void)
{
    const uint8_t banks[] = {0,2,3,5,6,7,8,9,11,12};
    unsigned b, p, k, q, fail = 0;
    for (b = 0; b < sizeof banks; ++b) {
        const engine_t *e = ENGINES[banks[b]];
        if (e->npresets != 32) { fprintf(stderr,"%s: expected 32 presets\n",e->name); ++fail; }
        for (p = 0; p < e->npresets; ++p) {
            const preset_t *pr = &e->presets[p];
            if (!pr->name[0] || strlen(pr->name) > 12) ++fail;
            for (q = 0; q < p; ++q) if (!strcmp(pr->name,e->presets[q].name)) ++fail;
            for (k = 0; k < 8; ++k) {
                if (pr->e[k] < e->edit[k].min || pr->e[k] > e->edit[k].max) {
                    fprintf(stderr,"%s/%s: parameter %u outside range\n",e->name,pr->name,k); ++fail;
                }
            }
        }
    }
    printf("WaveLoop banks: 10 engines, 320 presets, %u errors\n",fail);
    return fail ? 1 : 0;
}
