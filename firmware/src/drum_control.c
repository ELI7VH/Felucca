/* SPDX-License-Identifier: GPL-3.0-only
 * Per-lane sound/mix overrides. Kit, track faders and master FX remain shared.
 * DRUM projects/presets reuse their otherwise unused 128-byte FM6 patch payload. */
#define DRUM_CONTROLS 13u
static const uint8_t DRUM_CONTROL_ID[DRUM_CONTROLS] = {
    P_E1,P_E2,P_E3,P_E4,P_E5,P_E6,P_E7,
    P_LEVEL,P_PAN,P_DIST,P_CHOR,P_DLY,P_REV
};
typedef struct {
    int16_t value[NLANE][DRUM_CONTROLS];
    uint16_t mask[NLANE];
} drum_control_t;
static drum_control_t drum_control[NTRK];
static int16_t drum_mod_delta[DRUM_CONTROLS];
static uint8_t drum_mod_active;
static int16_t drum_volume_default=112, drum_pan_default=0;
static int drum_control_index(uint32_t id)
{
    if (id>=P_E1 && id<=P_E7) return (int)(id-P_E1);
    switch (id) {
    case P_LEVEL: return 7;
    case P_PAN: return 8;
    case P_DIST: return 9;
    case P_CHOR: return 10;
    case P_DLY: return 11;
    case P_REV: return 12;
    default: return -1;
    }
}
static int16_t drum_default_value(const track_t *t, uint32_t id)
{
    return id==P_LEVEL ? 112 : id==P_PAN ? 0 : t->p[id];
}
static int16_t drum_value(const track_t *t, uint32_t lane, uint32_t id)
{
    int index=drum_control_index(id);
    uint32_t k=(uint32_t)(t-trk);
    if (k<NTRK && lane<NLANE && index>=0 && (drum_control[k].mask[lane] & (1u<<index)))
        return (int16_t)clamp(drum_control[k].value[lane][index]+(drum_mod_active ? drum_mod_delta[index]:0),
                              id==P_PAN ? -64:0, id==P_PAN ? 63:id==P_E6 ? 1:id==P_LEVEL ? 112:127);
    return drum_default_value(t,id);
}
/* Reads never create overrides. A first edit starts at the inherited value. */
static int16_t *drum_param_ref(track_t *t, uint32_t id, int write)
{
    uint32_t k=(uint32_t)(t-trk), lane=k<NTRK ? drum_focus[k] : 0;
    int index=drum_control_index(id);
    if (t->eng_req!=ENGI_DRUM || k>=NTRK || lane>=NLANE || index<0) return &t->p[id];
    drum_control_t *d=&drum_control[k];
    if (!(d->mask[lane] & (1u<<index))) {
        if (!write) return id==P_LEVEL ? &drum_volume_default : id==P_PAN ? &drum_pan_default : &t->p[id];
        d->value[lane][index]=drum_default_value(t,id);
        if (write) d->mask[lane] |= (uint16_t)(1u<<index);
    }
    return &d->value[lane][index];
}
static void drum_controls_reset(track_t *t)
{
    uint32_t k=(uint32_t)(t-trk);
    if (k>=NTRK) return;
    memset(&drum_control[k],0,sizeof drum_control[k]);
    drum_focus[k]=0;
}
static __attribute__((noinline)) int drum_mix_custom(const track_t *t)
{
    uint32_t k=(uint32_t)(t-trk);
    if (k>=NTRK) return 0;
    for (uint32_t l=0;l<NLANE;l++) if (drum_control[k].mask[l] & 0x1f80u) return 1;
    return 0;
}
static void drum_controls_pack(const track_t *t, uint8_t *out)
{
    uint32_t k=(uint32_t)(t-trk), pos=21;
    memset(out,0,128); memcpy(out,"DRM1",4); out[4]=drum_focus[k];
    for (uint32_t l=0;l<NLANE;l++) {
        out[5+2*l]=(uint8_t)(drum_control[k].mask[l]&127);
        out[6+2*l]=(uint8_t)(drum_control[k].mask[l]>>7);
        for (uint32_t i=0;i<DRUM_CONTROLS;i++) {
            uint32_t id=DRUM_CONTROL_ID[i];
            int32_t v=drum_value(t,l,id);
            out[pos++]=(uint8_t)clamp(v+(id==P_PAN ? 64 : 0),0,127);
        }
    }
}
static int drum_controls_unpack(track_t *t, const uint8_t *in)
{
    uint32_t k=(uint32_t)(t-trk), pos=21;
    drum_controls_reset(t);
    if (k>=NTRK || memcmp(in,"DRM1",4) || in[4]>=NLANE) return 0;
    drum_focus[k]=in[4];
    for (uint32_t l=0;l<NLANE;l++) {
        drum_control[k].mask[l]=(uint16_t)((in[5+2*l] | (in[6+2*l]<<7)) & 0x1fffu);
        for (uint32_t i=0;i<DRUM_CONTROLS;i++) {
            uint32_t id=DRUM_CONTROL_ID[i];
            int32_t v=in[pos++] & 127;
            if (id==P_PAN) v-=64;
            if (id==P_E6) v=v>0;
            if (id==P_LEVEL && v>112) v=112;
            drum_control[k].value[l][i]=(int16_t)v;
        }
    }
    return 1;
}
/* Audio scratch is reused for each part; only needed after a per-drum mix edit. */
static int32_t drum_mix_buf[NLANE][CTL];
static uint8_t drum_mix_enabled;
