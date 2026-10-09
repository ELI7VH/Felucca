/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from MIDI control contribution by ChanceTheMaker (2026).
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Shared USB/TRS channel controls. Included by seq.c after its input helpers.
 * USB and TRS intentionally share channel state, matching the existing routing.
 * A synth part has one live bend/wheel state; channels assigned to the same part
 * share it (last controller wins). Drum hits ignore bend and sustain. CC1 keeps the existing matrix routing. */
typedef struct {
    int16_t bend;                           /* signed 14-bit value, zero = centre */
    uint8_t wheel, pedal, targets;
    uint8_t owned[NTRK];                    /* held/pedal notes per track, at most 128 per channel */
    uint8_t ready, semis, cents;
    uint8_t rpn_msb, rpn_lsb;
} midi_channel_t;
static midi_channel_t midi_ch[16];
/* Preset loads belong on the UI thread, never in the audio callback. Keep
 * arrival order and the resolved track even if SEL changes before polling. */
#define MIDI_BROWSE_N 16u
typedef struct { uint8_t track, bank; int8_t step; } midi_browse_t;
static midi_browse_t midi_browse_q[MIDI_BROWSE_N];
static volatile uint8_t midi_browse_r, midi_browse_w;
static void midi_browse_enqueue(uint32_t ch, uint32_t bank, uint32_t value)
{
    int32_t step = (int32_t)value - 64; /* MiniLab main encoder: 63 down, 65 up */
    uint8_t w = midi_browse_w, next = (uint8_t)((w + 1u) % MIDI_BROWSE_N);
    if (!step || next == midi_browse_r) return;
    midi_browse_q[w].track = (uint8_t)trk_index(midi_track(ch));
    midi_browse_q[w].bank = (uint8_t)bank;
    midi_browse_q[w].step = (int8_t)clamp(step, -8, 8);
    midi_browse_w = next;
}

/* Transport commands are global, independent of ROUT and keyboard channel.
 * Record remembers the displayed track at receipt; tap timing survives UI delay. */
#define MIDI_TRANSPORT_N 16u
typedef struct { uint32_t ms; uint8_t command, track; } midi_transport_t;
static midi_transport_t midi_transport_q[MIDI_TRANSPORT_N];
static volatile uint8_t midi_transport_r, midi_transport_w;
static uint8_t midi_transport_held[16];
static void midi_transport_cc(uint32_t ch, uint32_t cc, uint32_t value)
{
    uint32_t bit = 1u << (cc - 106u);
    uint8_t w = midi_transport_w, next = (uint8_t)((w + 1u) % MIDI_TRANSPORT_N);
    if (value >= 64u && !(midi_transport_held[ch] & bit)) {
        if (next != midi_transport_r) {
            midi_transport_q[w].command = (uint8_t)cc;
            midi_transport_q[w].track = song.sel;
            midi_transport_q[w].ms = fm1_ms;
            midi_transport_w = next;
        } else if (cc == 106u) {
            /* A full UI queue must never swallow STOP. */
            midi_transport_r = midi_transport_w;
            transport_req = 2;
        }
    }
    if (value >= 64u) midi_transport_held[ch] |= (uint8_t)bit;
    else midi_transport_held[ch] &= (uint8_t)~bit;
}

/* Audio/ISR -> UI mailbox. Coalesce rapid knob movement to the latest value;
 * formatting and drawing are entirely on the UI thread. */
typedef struct {
    const param_desc_t *desc;
    int16_t value;
    uint8_t track, kind, lane; /* 0 parameter, 1 master filter, 2 track, 3 drum parameter, 4 track FX */
} midi_notice_t;
static volatile midi_notice_t midi_notice;
static volatile uint8_t midi_notice_pending, midi_track_steps;
static volatile int8_t midi_setlist_step;
static uint8_t midi_setlist_held[2];
static uint8_t midi_click_held;
/* A-bank pad 1 is a global held modifier across keyboard channels. Each
 * other pad remembers its press layer so release order cannot stick an FX. */
static uint8_t midi_pad_shift, midi_pad_down, midi_pad_layer;
static void midi_notify(track_t *t, const param_desc_t *d, int32_t value, uint32_t kind)
{
    midi_notice.desc = d;
    midi_notice.value = (int16_t)value;
    midi_notice.track = (uint8_t)trk_index(t);
    midi_notice.kind = (uint8_t)kind;
    midi_notice.lane = drum_focus[trk_index(t)];
    midi_notice_pending = 1;
}
/* Concave taper: slope 1.5 at silence -> 0.5 at full level. More travel for
 * the upper levels, exact silence/full endpoints, no floating-point ISR work. */
static uint32_t midi_volume(uint32_t value)
{
    return (value * (381u - value) + 127u) / 254u;
}

/* Low bits: track + 1. High bit: key released, held by its channel's pedal. */
static uint8_t midi_sel_on[16][128];
#define midi_notes midi_sel_on
static uint16_t midi_owners[NTRK];           /* avoids rescanning all 2048 entries for CC123 */
#define MIDI_PEDAL_NOTE 0x80u

static midi_channel_t *midi_channel(uint32_t ch)
{
    midi_channel_t *c = &midi_ch[ch];
    if (!c->ready) {
        c->semis = 2;
        c->rpn_msb = c->rpn_lsb = 127;
        c->ready = 1;
    }
    return c;
}

static uint32_t midi_targets(uint32_t ch)
{
    return midi_channel(ch)->targets | (1u << trk_index(midi_track(ch)));
}

static void midi_expression(track_t *t, const midi_channel_t *c)
{
    int32_t range = ((int32_t)c->semis * 100 + c->cents) * 256 / 100;
    if (drum_track(t))
        return;
    midi_bend_target[trk_index(t)] = (int32_t)c->bend * range / (c->bend < 0 ? 8192 : 8191);
}

static void midi_expression_channel(uint32_t ch)
{
    uint32_t i, mask = midi_targets(ch);
    for (i = 0; i < NPART; i++)
        if (mask & (1u << i))
            midi_expression(&trk[i], midi_channel(ch));
}

/* a MIDI note of track t sounds note: one played as itself, or a tone of one played as a chord (chord.c) */
static int midi_note_held(const track_t *t, uint32_t note)
{
    uint32_t ch, id = trk_index(t) + 1u;
    for (ch = 0; ch < 16u; ch++)
        if ((midi_notes[ch][note] & 0x7Fu) == id && !mchord_of(ch, note, id))
            return 1;
    return mchord_held(id, note);
}

/* a key held on track t sounds note (the key's own note, or a tone of its chord) */
static int midi_local_held(const track_t *t, uint32_t note)
{
    uint32_t k, i;
    for (k = 0; k < 27u; k++)
        if (kb_chn[k] && kb_trk[k] == trk_index(t))
            for (i = 0; i < kb_chn[k]; i++)
                if (kb_chord[k][i] == note)
                    return 1;
    return 0;
}

static void midi_release(uint32_t ch, uint32_t note)
{
    uint32_t id = midi_notes[ch][note] & 0x7Fu;
    midi_notes[ch][note] = 0;
    if (id) {
        midi_owners[id - 1u]--;
        if (!--midi_ch[ch].owned[id - 1u])
            midi_ch[ch].targets &= (uint8_t)~(1u << (id - 1u));
    }
    if (id) {
        mchord_t *m = mchord_of(ch, note, id);
        uint8_t nn[CHORD_MAX];
        uint32_t n = 1, i;
        nn[0] = (uint8_t)note;
        if (m) {                                  /* a chord: exactly the notes it started */
            n = m->n;
            for (i = 0; i < n; i++)
                nn[i] = m->note[i];
            m->id = 0;
        }
        for (i = 0; i < n; i++)
            if (!midi_local_held(&trk[id - 1u], nn[i]))
                input_off(&trk[id - 1u], nn[i]);  /* input_off also checks other MIDI owners */
    }
}

/* a MIDI note-on (ch, note) of track t: its chord (chord.c) or the note alone. A note another key or MIDI
 * note holds already sounds: not started again */
static void midi_play(track_t *t, uint32_t ch, uint32_t note, uint32_t vel)
{
    uint8_t nn[CHORD_MAX];
    uint32_t n = chord_build(t, note, nn), i, f = 0;
    if (n > 1u || nn[0] != note)
        for (f = 0; f < MCHORD_N && mchord[f].id; f++)
            ;
    if (f == MCHORD_N) {                          /* no room to keep a chord: the note alone */
        n = 1;
        nn[0] = (uint8_t)note;
    }
    for (i = 0; i < n; i++)
        if (!midi_note_held(t, nn[i]) && !midi_local_held(t, nn[i]))
            input_on(t, nn[i], vel);
    if (n > 1u || nn[0] != note) {
        mchord_t *m = &mchord[f];
        m->ch = (uint8_t)ch;
        m->src = (uint8_t)note;
        m->n = (uint8_t)n;
        for (i = 0; i < n; i++)
            m->note[i] = nn[i];
        m->id = (uint8_t)(trk_index(t) + 1u);
    }
}

static void midi_note_event(uint32_t ch, uint32_t note, uint32_t vel)
{
    midi_channel_t *c = midi_channel(ch);
    uint32_t id = midi_notes[ch][note] & 0x7Fu;
    if (vel) {
        track_t *t = midi_track(ch);
        /* Repeated notes replace the previous press, including a pedal-held one. */
        if (id)
            midi_release(ch, note);
        drum_focus_note(t, note);
        midi_expression(t, c);
        if (t != TSEL)
            midi_hint = (uint8_t)(trk_index(t) + 1u);
        c->targets |= (uint8_t)(1u << trk_index(t));
        midi_play(t, ch, note, vel);
        midi_notes[ch][note] = (uint8_t)(trk_index(t) + 1u);
        midi_owners[trk_index(t)]++;
        c->owned[trk_index(t)]++;
    } else if (id) {
        if (c->pedal && !drum_track(&trk[id - 1u]))
            midi_notes[ch][note] |= MIDI_PEDAL_NOTE;
        else
            midi_release(ch, note);
    }
}

static void midi_pedal_up(uint32_t ch)
{
    uint32_t note;
    midi_channel(ch)->pedal = 0;
    for (note = 0; note < 128u; note++)
        if (midi_notes[ch][note] & MIDI_PEDAL_NOTE)
            midi_release(ch, note);
}

/* Preset/project panic and CC120 must discard ownership so a later pedal-up
 * or note-off cannot release notes subsequently started on another patch. */
static void __attribute__((noinline)) midi_forget_track(uint32_t track)
{
    uint32_t ch, note;
    for (ch = 0; ch < 16u; ch++) {
        if (!(midi_ch[ch].targets & (1u << track)))
            continue;
        for (note = 0; note < 128u; note++)
            if ((midi_notes[ch][note] & 0x7Fu) == track + 1u)
                midi_notes[ch][note] = 0;
        midi_ch[ch].targets &= (uint8_t)~(1u << track);
        midi_ch[ch].owned[track] = 0;
    }
    midi_owners[track] = 0;
    mchord_forget(track);
    midi_bend_q8[track] = midi_bend_target[track] = 0;
}

static void midi_silence_track(uint32_t track)
{
    track_t *t = &trk[track];
    uint32_t i;
    trk_all_off(t);
    t->nheld = t->arp_phys = t->arp_note = t->rh_n = 0;
    t->seq_n = t->seq_hold = t->slide_glide = 0;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            voice_kill(&t->v[i]);             /* one-block fade, regardless of RELEASE */
    sl[track].rec = sl[track].loop = 0;       /* do not keep replaying captured sound */
    midi_forget_track(track);
}

static int midi_track_held(uint32_t track)
{
    uint32_t k;
    if (midi_owners[track])
        return 1;
    for (k = 0; k < 27u; k++)
        if ((fm1_in.notes & (1u << k)) && kb_trk[k] == track)
            return 1;
    return 0;
}

/* Standalone controllers: CC20..27 are the engine's eight EDIT parameters.
 * These follow ROUT like notes. CC7 on channels 1..4 always addresses that
 * channel's mixer strip, even in SEL, so four fixed-channel faders stay useful.
 * Called between audio blocks: descriptors include engine-specific signed / enum
 * ranges; motion_capture keeps automation's base and recording consistent. */
static void midi_parameter(track_t *t, uint32_t id, uint32_t value)
{
    const param_desc_t *d = track_desc(t, id);
    int32_t v = d->min + ((int32_t)value * (d->max - d->min) + 63) / 127;
    if (d->max <= d->min)
        return;
    int16_t *vp = id==P_LEVEL ? &t->p[id] : drum_param_ref(t,id,1);
    *vp=(int16_t)param_fit(d,v);
    if (vp==&t->p[id]) (void)motion_capture(t,id,*vp);
}

static int midi_parameter_cc(uint32_t ch, uint32_t cc, uint32_t value)
{
    track_t *t = midi_track(ch);
    uint32_t id;
    if (midi_pad_shift) {
        static const uint8_t knobs[8]={19,21,22,23,28,29,30,76};
        /* Shifted knobs are reserved until their assignments are chosen. */
        for (uint32_t k=0;k<8;k++) if (cc==knobs[k]) return 1;
        if (cc==7u && ch<NPART) {
            t=&trk[ch];
            /* One track-wide macro, including on drums. */
            const uint8_t sends[2]={P_DLY,P_REV};
            for (uint32_t k=0;k<2;k++) {
                id=sends[k]; const param_desc_t *d=track_desc(t,id);
                t->p[id]=(int16_t)param_fit(d,d->min+((int32_t)value*(d->max-d->min)+63)/127);
                (void)motion_capture(t,id,t->p[id]);
            }
            midi_notify(t,0,(int32_t)(value*100u/127u),4);
            return 1;
        }
    }
    if (cc >= 20u && cc <= 27u)
        id = P_E0 + cc - 20u;
    else if (cc >= 28u && cc <= 31u)
        id = ENGINES[t->eng_req % NENGINES]->knob[cc - 28u];
    else switch (cc) {
    case 7: t = ch < NPART ? &trk[ch] : t; id = P_LEVEL; break;
    case 17: if (!drum_track(t)) return 0; id=P_LEVEL; break;
    case 10: id = P_PAN; break;
    case 70: id = P_SUS; break;
    case 72: id = P_REL; break;
    case 73: id = P_ATK; break;
    case 75: id = P_DEC; break;
    case 76: id = P_LRATE; break;
    case 90: id = P_DIST; break;
    case 91: id = P_REV; break;
    case 93: id = P_CHOR; break;
    case 94: id = P_DLY; break;
    default: return 0;
    }
    if (drum_track(t)) {
        /* DRUM layout: top 2..4 pan/reverb/delay; bottom 8 drum volume. */
        if (cc==21u) id=P_PAN;
        else if (cc==22u) id=P_REV;
        else if (cc==23u) id=P_DLY;
        else if (cc==76u || cc==17u) id=P_LEVEL;
    }
    if (drum_track(t) && id==P_LEVEL && cc!=7u) {
        const param_desc_t *d=&DRUM_LEVEL_DESC;
        *drum_param_ref(t,id,1)=(int16_t)param_fit(d,(midi_volume(value)*112u+63u)/127u);
    } else midi_parameter(t,id,cc==7u ? midi_volume(value):value);
    int perdrum=drum_track(t) && cc!=7u && drum_control_index(id)>=0;
    midi_notify(t,perdrum && id==P_LEVEL ? &DRUM_LEVEL_DESC : track_desc(t,id),perdrum ? *drum_param_ref(t,id,0):t->p[id],perdrum ? 3u:0u);
    return 1;
}

static void midi_control(uint32_t ch, uint32_t cc, uint32_t value)
{
    midi_channel_t *c = midi_channel(ch);
    uint32_t i, mask;
    if (midi_parameter_cc(ch, cc, value))
        return;
    switch (cc) {
    case 19: { /* Master DJ filter: left LPF, centre bypass, right HPF.
                * Uses the existing smoothed FX macro; never edits a sound. */
        int32_t v = (int32_t)value;
        perf_k[0] = (int8_t)(v < 63 ? (v - 63) * 100 / 63 : v > 64 ? (v - 64) * 100 / 63 : 0);
        midi_notify(midi_track(ch), 0, perf_k[0], 1);
        return;
    }
    case 110: case 111: {
        uint32_t k=cc-110u;
        if (ch!=15u) return;
        if (value>=64u && !midi_setlist_held[k]) midi_setlist_step=k?1:-1;
        midi_setlist_held[k]=value>=64u; return;
    }
    case 115: /* Main encoder click: one displayed-track step per rising edge. */
        if (value >= 64u && !midi_click_held)
            midi_track_steps = (uint8_t)((midi_track_steps + 1u) % NTRK);
        midi_click_held = value >= 64u;
        return;
    case 114: case 112:
        midi_browse_enqueue(ch, cc == 112u, value);
        return;
    case 1:
        c->wheel = (uint8_t)value;
        break;
    case 120:                                      /* All Sound Off: ignores the pedal */
        mask = midi_targets(ch);
        for (i = 0; i < NTRK; i++)
            if (mask & (1u << i))
                midi_silence_track(i);
        break;
    case 123:                                      /* All Notes Off: normal releases, honours pedal */
        mask = midi_targets(ch);
        for (i = 0; i < 128u; i++)
            if (midi_notes[ch][i])
                midi_note_event(ch, i, 0);
        for (i = 0; i < NTRK; i++)
            if ((mask & (1u << i)) && !midi_track_held(i)) {
                trk_all_off(&trk[i]);
                trk[i].nheld = trk[i].arp_phys = trk[i].arp_note = trk[i].rh_n = 0;
            }
        break;
    case 121:                                      /* Reset All Controllers, keep bend sensitivity */
        c->bend = 0;
        c->wheel = 0;
        c->rpn_msb = c->rpn_lsb = 127;
        midi_expression_channel(ch);
        midi_pedal_up(ch);
        break;
    case 64:
        if (value >= 64u)
            c->pedal = 1;
        else
            midi_pedal_up(ch);
        break;
    case 101: c->rpn_msb = (uint8_t)value; break;
    case 100: c->rpn_lsb = (uint8_t)value; break;
    case 99: case 98:                              /* NRPN selection cancels RPN data entry */
        c->rpn_msb = c->rpn_lsb = 127;
        break;
    case 6: case 38:
        if (!c->rpn_msb && !c->rpn_lsb) {           /* RPN 0: +/-0..24 semitones, 0..99 cents */
            if (cc == 6u)
                c->semis = (uint8_t)(value > 24u ? 24u : value);
            else
                c->cents = (uint8_t)(value > 99u ? 99u : value);
            midi_expression_channel(ch);
        }
        break;
    default: break;
    }
}

/* ROUT CH1-4 listens to channels 1..4 only. A switch to it from SEL (events_block, before the queue) lets go
 * of what channels 5..16 hold: their notes released (also pedal-held ones), their pedal, bend, wheel and RPN
 * selection reset, so no note can hang on a channel that is no longer heard. Each part's bend then follows
 * its own channel (1..4), as CH1-4 routes it. */
static void __attribute__((noinline)) midi_route_ch14(void)
{
    uint32_t ch, note;
    for (ch = NPART; ch < 16u; ch++) {
        midi_channel_t *c = midi_channel(ch);
        for (note = 0; note < 128u; note++)
            if (midi_notes[ch][note])
                midi_release(ch, note);
        c->pedal = c->wheel = 0;
        c->bend = 0;
        c->rpn_msb = c->rpn_lsb = 127;
    }
    for (ch = 0; ch < NPART; ch++)
        midi_expression(&trk[ch], midi_channel(ch));
}

/* Keep the occasional controller/panic dispatch outside the hot rendering loop. Channel voice messages only
 * (realtime and clock are handled in events_block, SysEx never reaches here). With ROUT CH1-4 channels
 * 5..16 are ignored entirely: notes, bend, CCs (CC1/11/64, RPN, and the CC120/121/123 panic and reset),
 * channel aftertouch, except dedicated channel 16 master-effect pads/panic below. */
static void __attribute__((noinline)) midi_event(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    if (st == 0xB0u && d1 >= 106u && d1 <= 109u) {
        midi_transport_cc(ch, d1, d2);
        return;
    }
    if (st == 0xB0u && (d1 == 120u || d1 == 121u || d1 == 123u))
        midi_transport_held[ch] = 0;
    /* A-bank pads use notes 36..43 on dedicated channel 16. Consume them
     * before track routing: pads always affect the master, never play notes. */
    if (ch == 15u) {
        if ((st == 0x90u || st == 0x80u) && d1 >= 36u && d1 <= 43u) {
            uint32_t k=d1-36u, down=st==0x90u && d2;
            if (!k) { midi_pad_shift=(uint8_t)down; return; }
            static const uint8_t effects[8]={PF_R8,PF_R16,PF_R32,PF_REV,PF_TAPE,PF_FRZ,PF_OUP,PF_ODN};
            static const uint8_t commands[8]={0,0,0,0,0,0,0,109};
            uint8_t pad=(uint8_t)(1u<<k);
            if (down) {
                if (midi_pad_down & pad) return;
                midi_pad_down |= pad;
                if (midi_pad_shift) midi_pad_layer |= pad;
                else midi_pad_layer &= (uint8_t)~pad;
            } else if (!(midi_pad_down & pad)) return;
            if (midi_pad_layer & pad) {
                uint32_t cc=commands[k];
                if (cc>=106u && cc<=109u) midi_transport_cc(ch,cc,down?127u:0u);
                else if (cc) midi_control(ch,cc,down?127u:0u);
            } else {
                uint32_t e=effects[k], bit=PF_BIT(e);
                if (down) {
                    if (!(perf_midi_held & bit)) perf_ord[e]=++perf_seq;
                    perf_midi_held |= bit;
                } else perf_midi_held &= ~bit;
            }
            if (!down) { midi_pad_down &= (uint8_t)~pad; midi_pad_layer &= (uint8_t)~pad; }
            return;
        }
        if (st == 0xB0u && (d1 == 120u || d1 == 121u || d1 == 123u)) {
            perf_midi_held = 0;
            midi_pad_shift=midi_pad_down=midi_pad_layer=0;
            midi_click_held=0;
            midi_setlist_held[0]=midi_setlist_held[1]=0;
            return;
        }
    }
    if (ch >= NPART && !song.g[G_ROUTE])
        return;
    if (st == 0x90u || st == 0x80u)
        midi_note_event(ch, d1, st == 0x90u ? d2 : 0);
    else if (st == 0xE0u) {
        midi_channel(ch)->bend = (int16_t)((int32_t)(d1 | (d2 << 7)) - 8192);
        midi_expression_channel(ch);
    } else if (st == 0xB0u) {
        if (!(midi_pad_shift && (d1==7u || d1==19u || (d1>=21u && d1<=23u) || (d1>=28u && d1<=30u) || d1==76u)))
            mod_midi(midi_track(ch), st, d1, d2);
        midi_control(ch, d1, d2);
    } else if (st == 0xD0u)
        mod_midi(midi_track(ch), st, d1, d2);
}
