/* SPDX-License-Identifier: GPL-3.0-only
 * End-to-end channel input and external-clock tests using the real queues,
 * UART parser, ownership model, sequencer, voice renderer and UI source. */
static unsigned popup_blits, page_overwrites;
static void midi_blit_watch(unsigned x, unsigned y, unsigned w, unsigned h)
{
    if (x==12 && y==84 && w==216 && h==104) popup_blits++;
    else if (x<228 && x+w>12 && y<188 && y+h>84) page_overwrites++;
}
#define UI_BLIT_HOOK(x,y,w,h) midi_blit_watch(x,y,w,h)
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void midi_test_reset(void)
{
    ui_power_on();
    memset(drum_control,0,sizeof drum_control); memset(drum_focus,0,sizeof drum_focus); drum_focus_pending=0;
    memset(drum_dist_state,0,sizeof drum_dist_state); drum_mix_enabled=0;
    midi_browse_r = midi_browse_w = 0;
    midi_transport_r = midi_transport_w = 0;
    memset(midi_transport_held, 0, sizeof midi_transport_held);
    midi_notice_pending = midi_track_steps = midi_click_held = 0;
    memset(&midi_popup, 0, sizeof midi_popup);
    memset(midi_ch, 0, sizeof midi_ch); memset(midi_notes, 0, sizeof midi_notes);
    memset(midi_owners, 0, sizeof midi_owners); memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target); memset(&midi_clock, 0, sizeof midi_clock);
    memset(&um, 0, sizeof um); memset(midi_in_source, 0, sizeof midi_in_source);
    mi_r = mi_w = 0; midi_in_overflow = 0; midi_beat_samples = 0;
    fm1_in.notes = kb_prev = 0; fm1_ms = 0; song.sel = 0;
    events_block(CTL);
}
static void queued(uint32_t st, uint32_t d1, uint32_t d2, uint32_t source)
{
    midi_enqueue((st >= 0xF8u ? 0xFu : st >> 4) | st << 8 | d1 << 16 | d2 << 24, source);
    events_block(CTL);
}
static int gate_note(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++) if (t->v[i].active && t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}
static int controller_feedback_test(void)
{
    int bad = 0; midi_test_reset();
    int16_t previous = -1;
    for (uint32_t v = 0; v < 128u; v++) {
        queued(0xB0,7,v,1);
        bad += trk[0].p[P_LEVEL] < previous || trk[0].p[P_LEVEL] > 127;
        previous = trk[0].p[P_LEVEL];
    }
    bad += check("fader taper is monotonic with exact silence/full endpoints", !bad && midi_volume(0)==0 && trk[0].p[P_LEVEL]==127);
    bad += check("upper travel changes level less than lower travel", midi_volume(32)-midi_volume(0) > midi_volume(127)-midi_volume(95));
    queued(0xB1,76,70,2); midi_ui_poll();
    bad += check("TRS knob popup carries actual track, descriptor and value", midi_popup.active && midi_popup.notice.track==1 && midi_popup.notice.desc==track_desc(&trk[1],P_LRATE) && midi_popup.notice.value==70);
    popup_blits = page_overwrites = 0;
    ui_draw();
    bad += check("first popup paints once without transmitting the underlying graph", popup_blits==1 && !page_overwrites);
    popup_blits = page_overwrites = 0;
    for (uint32_t k=0;k<12;k++) {
        scope_w += 16; trk[0].p[P_E0]++; ui.force=1; ui_draw();
    }
    bad += check("live graph changes and forced refreshes never overwrite a held popup", !popup_blits && !page_overwrites);
    queued(0xB1,76,71,2); midi_ui_poll(); ui_draw();
    bad += check("changed knob popup transfers only its new card", popup_blits==1 && !page_overwrites);
    screen_save("/tmp", "felucca-midi-popup");
    uint32_t stamp=midi_popup.stamp; fm1_ms=stamp+1199; ui_draw();
    bad += check("popup persists briefly while idle", midi_popup.active);
    fm1_ms=stamp+1200; ui_draw();
    bad += check("popup expires and restores the page in the same frame", !midi_popup.active && !ui.force);
    ui_draw(); static uint16_t restored[240*240]; memcpy(restored,host_screen,sizeof restored);
    ui.force=1; ui_draw();
    bad += check("popup expiry restores exactly the original page", !memcmp(restored,host_screen,sizeof restored));
    for (uint32_t k=0; k<8; k++) {
        uint32_t before=song.sel;
        queued(0xB0,115,127,1); queued(0xB0,115,127,1);
        bad += check("click selection is deferred outside audio callback", song.sel==before);
        midi_ui_poll();
        bad += check("one encoder press cycles one displayed track, including wrap", song.sel==(before+1u)%4u);
        queued(0xB0,115,0,1); midi_ui_poll();
        bad += check("click release does not advance track", song.sel==(before+1u)%4u);
    }
    queued(0xB4,115,127,1); midi_ui_poll();
    bad += check("unsupported MIDI channel cannot change selected track", song.sel==0);
    queued(0xB0,19,0,1); midi_ui_poll();
    bad += check("master filter popup identifies the master and LPF", midi_popup.notice.kind==1 && midi_popup.notice.value==-100);
    midi_test_reset(); return bad;
}
static int pads_test(void)
{
    int bad = 0; midi_test_reset(); perf_midi_held = perf_held = perf_latched = 0;
    perf_latch_on = 1; perf_kill = 0; song.playing = 0;
    const uint8_t effects[8] = {PF_R8, PF_R16, PF_R32, PF_REV, PF_TAPE, PF_FRZ, PF_OUP, PF_ODN};
    for (uint32_t k = 0; k < 8; k++) {
        queued(0x9F, 36+k, 100, k&1);
        bad += check("A pad engages its master effect with latch on and CH1-4 routing", perf_midi_held == PF_BIT(effects[k]) && !perf_latched && !midi_notes[15][36+k]);
        perf_begin(CTL);
        bad += check("pad hold reaches the real performance DSP", perf_act & PF_BIT(effects[k]));
        queued(0x8F, 36+k, 0, k&1);
        perf_begin(CTL);
        bad += check("pad release removes the DSP activation", !(perf_act & PF_BIT(effects[k])));
        bad += check("pad lift clears its effect despite FX latch", !perf_midi_held);
    }
    queued(0x9F,36,100,1); queued(0x9F,39,100,0);
    bad += check("last pressed pad wins overlapping buffer holds", perf_pick(perf_midi_held) == PF_REV);
    queued(0x9F,39,0,0);
    bad += check("zero-velocity note-on releases and resumes older held pad", perf_pick(perf_midi_held) == PF_R8);
    perf_held = PF_BIT(PF_R8); queued(0x8F,36,0,1);
    bad += check("pad release preserves a matching local FX key", perf_held == PF_BIT(PF_R8) && !perf_midi_held);
    perf_held = 0;
    for (uint32_t cc = 120; cc <= 123; cc++) if (cc != 122) {
        queued(0x9F,43,100,1); queued(0xBF,cc,0,1);
        bad += check("channel 16 panic/reset releases pad effects", !perf_midi_held);
    }
    song.g[G_ROUTE] = 1; queued(0x9F,40,100,1); queued(0x8F,40,0,1);
    bad += check("SEL routing consumes pad notes without creating voices", !midi_notes[15][40] && !gate_note(&trk[0],40));
    queued(0x90,36,100,1);
    bad += check("keyboard notes remain ordinary notes", gate_note(&trk[0],36) && !perf_midi_held);
    perf_latch_on = 0; return bad;
}
static int controls_test(void)
{
    int bad = 0; midi_test_reset(); track_t *t = &trk[0];
    queued(0xE0, 127, 127, 1);
    bad += check("USB maximum bend is exactly the default +2 semitones", midi_bend_target[0] == 512);
    queued(0xE0, 0, 0, 1);
    bad += check("USB minimum bend is exactly the default -2 semitones", midi_bend_target[0] == -512);
    queued(0xE0, 0, 64, 1);
    bad += check("bend centre is zero", midi_bend_target[0] == 0);
    queued(0xB0, 101, 0, 1); queued(0xB0, 100, 0, 1); queued(0xB0, 6, 12, 1); queued(0xB0, 38, 50, 1);
    queued(0xE0, 127, 127, 1);
    bad += check("RPN0 supports semitone and cent bend sensitivity", midi_ch[0].semis == 12 && midi_ch[0].cents == 50 && midi_bend_target[0] == 3200);
    queued(0xB0, 99, 0, 1); queued(0xB0, 6, 24, 1);
    bad += check("selecting NRPN cancels RPN data entry", midi_ch[0].semis == 12);
    queued(0x90, 60, 100, 1); int32_t out[CTL]; int32_t before = midi_bend_q8[0];
    track_render(t, out, CTL);
    bad += check("bend is smoothed toward the target in the real voice renderer", midi_bend_q8[0] > before && midi_bend_q8[0] < midi_bend_target[0]);
    for (uint32_t i = 0; i < 64; i++) track_render(t, out, CTL);
    bad += check("bend smoothing reaches the exact target without changing saved pitch", midi_bend_q8[0] == 3200 && t->p[P_TRANS] == 0 && t->v[0].note == 60);
    queued(0xB0, 121, 0, 1);
    bad += check("Reset Controllers centres bend and preserves RPN sensitivity", !midi_bend_target[0] && midi_ch[0].semis == 12 && gate_note(t, 60));
    queued(0xB0, 1, 87, 1); queued(0xB0, 11, 23, 1); queued(0xD0, 90, 0, 1);
    bad += check("existing modwheel/expression/aftertouch stay routed to the track", t->mw == 87 && t->ex_off == 104 && t->at == 90);
    queued(0xE3, 127, 127, 1);
    bad += check("drum track ignores pitch bend", !midi_bend_target[3]);
    return bad;
}
static int sustain_test(void)
{
    int bad = 0; midi_test_reset(); track_t *t = &trk[0];
    queued(0x90, 60, 100, 1); queued(0xB0, 64, 127, 1); queued(0x80, 60, 0, 1);
    bad += check("pedal holds a released synth note and its owner", gate_note(t, 60) && midi_notes[0][60] == (1u | MIDI_PEDAL_NOTE));
    queued(0xB0, 64, 0, 1);
    bad += check("pedal release releases its held note", !gate_note(t, 60) && !midi_notes[0][60] && !midi_owners[0]);
    queued(0x90, 62, 100, 1); queued(0xB0, 64, 127, 1); queued(0xB0, 123, 0, 1);
    bad += check("All Notes Off honours sustain rather than hard-killing", gate_note(t, 62) && (midi_notes[0][62] & MIDI_PEDAL_NOTE));
    queued(0xB0, 121, 0, 1);
    bad += check("Reset Controllers releases pedal-held notes", !gate_note(t, 62) && !midi_notes[0][62]);
    queued(0x93, 36, 100, 1); queued(0xB3, 64, 127, 1); queued(0x83, 36, 0, 1);
    bad += check("drum note-offs do not accumulate pedal-held owners", !midi_notes[3][36] && !midi_owners[3]);
    queued(0x90, 67, 100, 1); queued(0xB0, 64, 127, 1); queued(0x80, 67, 0, 1); queued(0xB0, 120, 0, 1);
    bad += check("All Sound Off ignores pedal and discards all track ownership", !midi_notes[0][67] && !midi_owners[0] && !t->nheld && !gate_note(t, 67));
    return bad;
}
static int ownership_test(void)
{
    int bad = 0; midi_test_reset(); song.g[G_ROUTE] = 1; track_t *t = &trk[0];
    queued(0x94, 60, 100, 1); queued(0x95, 60, 100, 2);
    bad += check("USB/TRS channels can share one sounding note with independent owners", midi_owners[0] == 2u && gate_note(t, 60));
    queued(0x84, 60, 0, 1);
    bad += check("one channel's note-off preserves another channel's hold", midi_owners[0] == 1u && gate_note(t, 60));
    queued(0x85, 60, 0, 2);
    bad += check("last channel owner releases the shared note", !midi_owners[0] && !gate_note(t, 60));
    fm1_in.notes = 1u; events_block(CTL); uint32_t local = kb_note[0];
    queued(0x94, local, 100, 1); queued(0x84, local, 0, 1);
    bad += check("MIDI release cannot cut a still-held local keyboard note", gate_note(t, local));
    fm1_in.notes = 0; events_block(CTL);
    bad += check("local release after MIDI release ends the shared note", !gate_note(t, local));
    queued(0x94, 65, 100, 1); song.sel = 1; queued(0x84, 65, 0, 1);
    bad += check("note-off follows its note-on across selected-track changes", !gate_note(&trk[0], 65) && !midi_notes[4][65]);
    song.sel = 0; queued(0x94, 67, 100, 1); queued(0xB4, 64, 127, 1); queued(0x84, 67, 0, 1);
    panic_req |= 1u; events_block(CTL);
    queued(0x90, 67, 100, 1); queued(0xB4, 64, 0, 1);
    bad += check("preset panic forgets old pedal owners so later pedal-up preserves new notes", !midi_notes[4][67] && gate_note(&trk[0], 67));
    for (uint32_t i = 0; i <= MQ; i++) midi_enqueue(0x09u | 0x90u << 8 | 70u << 16 | 100u << 24, 1);
    events_block(CTL);
    bad += check("queue overflow recovers ownership and releases stuck notes", !midi_in_overflow && mi_r == mi_w && !midi_owners[0] && !gate_note(&trk[0], 67));
    return bad;
}
static void clock_setup(uint32_t mode)
{
    midi_test_reset(); song.g[G_CLOCK] = (int16_t)mode;
    trk[0].p[P_SLEN] = 16; trk[0].p[P_SDIV] = 2;
    for (uint32_t i = 0; i < 16; i++) trk[0].step[i] = (step_t){{(uint8_t)(60 + i)}, 1, ST_NOTE, 0, 100};
    events_block(CTL);
}
static void clock_packet(uint32_t source, uint32_t status, uint32_t ms)
{
    fm1_ms = ms;
    if (source == 2u) { um_byte(status); events_block(CTL); }
    else queued(status, 0, 0, source);
}
static void clock_to(uint32_t source, uint32_t start, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) clock_packet(source, 0xF8, start + (i * 125u) / 6u);
}
static int clock_test(uint32_t source)
{
    int bad = 0; clock_setup(source);
    clock_packet(source == 1u ? 2u : 1u, 0xFA, 1);
    bad += check(source == 1 ? "USB mode ignores TRS Start" : "TRS mode ignores USB Start", !song.playing);
    clock_packet(source, 0xFA, 2);
    bad += check(source == 1 ? "USB Start begins at step zero" : "TRS Start begins at step zero", song.playing && trk[0].seq_idx == 0u);
    clock_to(source, 2, 7);
    bad += check(source == 1 ? "USB six pulses advance exactly one 16th step" : "TRS six pulses advance exactly one 16th step", trk[0].seq_idx == 1u && song.g[G_BPM] == 120);
    fm1_ms = 132; events_block(CTL); uint16_t idx = trk[0].seq_idx; uint32_t pos = trk[0].seq_pos;
    clock_packet(source, 0xFC, 133);
    bad += check("external Stop preserves step position and releases sequence notes", !song.playing && trk[0].seq_idx == idx && trk[0].seq_pos == pos && !trk[0].seq_n);
    clock_packet(source, 0xFB, 150);
    bad += check("external Continue resumes the existing step position", song.playing && trk[0].seq_idx == idx && trk[0].seq_pos == pos);
    clock_packet(source, 0xF8, 151); fm1_ms = 672; events_block(CTL);
    bad += check("lost clock times out and releases transport notes", !song.playing && !trk[0].seq_n);
    clock_packet(source, 0xFA, 700); clock_to(source, 700, 7);
    bad += check("Start after timeout restarts cleanly at the master's phase", song.playing && trk[0].seq_idx == 1u);
    /* Original base values remain after MIDI Stop even while motion sounds. */
    trk[0].p[P_REV] = 23; motion_clear(&trk[0]); motion_set_event(&trk[0], 0, P_REV, 100);
    clock_packet(source, 0xFA, 1000); clock_packet(source, 0xF8, 1000);
    bad += check("external-clock playback applies recorded motion", trk[0].p[P_REV] == 100);
    clock_packet(source, 0xFC, 1010);
    bad += check("external Stop restores the musical parameter base", trk[0].p[P_REV] == 23);
    return bad;
}
static int clock_arp_and_boundaries(void)
{
    int bad = 0; clock_setup(1); trk[0].p[P_AMODE] = 1; trk[0].p[P_ARATE] = 2;
    queued(0x90, 60, 100, 1); queued(0x90, 64, 100, 1);
    clock_packet(1, 0xFA, 10); clock_to(1, 10, 1); uint32_t before = trk[0].arp_idx;
    clock_to(2, 20, 7);
    bad += check("wrong-source clocks cannot advance the ARP", trk[0].arp_idx == before);
    clock_to(1, 10, 7);
    bad += check("selected-source MIDI clock advances the ARP", trk[0].arp_idx > before);
    clock_setup(2); fm1_ms = 100; um_byte(0x90); um_byte(60); um_byte(0xFA); um_byte(100); events_block(CTL);
    bad += check("TRS realtime interleaving preserves the incomplete note message", gate_note(&trk[0], 60) && song.playing);
    /* Millisecond counter wraps naturally during external-clock operation. */
    clock_setup(1); clock_packet(1, 0xFA, 0xFFFFFFF0u); clock_packet(1, 0xF8, 0xFFFFFFF0u);
    clock_packet(1, 0xF8, 5u); fm1_ms = 10; events_block(CTL);
    bad += check("external clock timestamps survive uint32 millisecond wrap", song.playing && midi_clock.last_ms == 5u);
    clock_setup(1); transport_req = 1; events_block(CTL); fm1_ms = 501; events_block(CTL);
    bad += check("local PLAY waiting for missing external clock also times out", !song.playing);
    return bad;
}
/* ARP HOLD latched on an external clock: a Stop, a lost clock or a tap while stopped must not leave the arp note
 * sounding; Continue plays the latched chord again; the internal clock's free-running arp is unchanged by STOP */
static void arp_latch(uint32_t mode)
{
    midi_test_reset(); song.g[G_CLOCK] = (int16_t)mode; events_block(CTL);
    trk[0].p[P_AMODE] = 1; trk[0].p[P_AHOLD] = 1; trk[0].p[P_ARATE] = 2; trk[0].p[P_AGATE] = 120; trk[0].p[P_AOCT] = 2;
    events_block(CTL);
}
static uint32_t arp_ext_run(uint32_t ms)       /* Start, tap 60 (latched), clocks until the arp plays 72 */
{
    uint32_t i;
    clock_packet(1, 0xFA, ms); queued(0x90, 60, 100, 1); queued(0x80, 60, 0, 1);
    for (i = 0; i < 400 && trk[0].arp_note != 72; i++) clock_packet(1, 0xF8, ms + i * 21u);
    return ms + i * 21u;
}
static void idle_ms(uint32_t ms)               /* about ms of audio blocks, no MIDI */
{
    uint32_t i, ms0 = fm1_ms;
    for (i = 0; i < ms * 23u / 16u; i++) { fm1_ms = ms0 + i * 16u / 23u; events_block(CTL); }
}
static int arp_ext_stop_test(void)
{
    int bad = 0; uint32_t ms, i, off;
    arp_latch(1); ms = arp_ext_run(10);
    bad += check("ARP HOLD on the external clock plays the upper octave", trk[0].arp_note == 72 && gate_note(&trk[0], 72));
    clock_packet(1, 0xFC, ms + 5); idle_ms(2000);
    bad += check("external Stop ends the sounding ARP note", !trk[0].arp_note && !gate_note(&trk[0], 72) && !gate_note(&trk[0], 60));
    bad += check("external Stop keeps the latched HOLD chord", trk[0].nheld == 1u && trk[0].held[0] == 60u);
    ms = fm1_ms; clock_packet(1, 0xFB, ms);
    for (i = 1; i < 40 && !trk[0].arp_note; i++) clock_packet(1, 0xF8, ms + i * 21u);
    bad += check("Continue plays the latched chord again", trk[0].arp_note != 0);
    arp_latch(1); arp_ext_run(10); idle_ms(2000);
    bad += check("a lost external clock stops the transport and ends the ARP note", !song.playing && !trk[0].arp_note && !gate_note(&trk[0], 72));
    arp_latch(1); queued(0x90, 64, 100, 1); queued(0x80, 64, 0, 1);
    bad += check("a tap while the external transport is stopped sounds", trk[0].arp_note == 64);
    idle_ms(2000);
    bad += check("its ARP gate still ends in real time", !trk[0].arp_note && !gate_note(&trk[0], 64));
    arp_latch(0); transport_req = 1; events_block(CTL); queued(0x90, 60, 100, 1); queued(0x80, 60, 0, 1);
    for (i = 0; i < 4000 && trk[0].arp_note != 72; i++) events_block(CTL);
    off = trk[0].arp_off;
    transport_req = 2; events_block(CTL);
    bad += check("internal clock: STOP leaves the free-running ARP note to its gate", trk[0].arp_note == 72 && gate_note(&trk[0], 72) && trk[0].arp_off == off - CTL);
    for (i = 0; i < 4000 && (!trk[0].arp_note || trk[0].arp_note == 72); i++) events_block(CTL);
    bad += check("internal clock: the ARP keeps stepping after STOP", trk[0].arp_note && trk[0].arp_note != 72 && trk[0].nheld == 1u);
    return bad;
}
/* USB-MIDI back-pressure: full 64-byte bulk packets (16 events) faster than the audio ISR drains the ring are
 * left in the endpoint (NAK) instead of overflowing it, which would panic every sounding note */
static int usb_burst_test(void)
{
    int bad = 0; uint8_t pkt[64]; uint32_t e, k, taken = 0;
    midi_test_reset();
    queued(0x90, 48, 100, 1);
    for (e = 0; e < 16; e++) {                       /* CC7 = 100 on 16 channels */
        pkt[4 * e] = 0x0B; pkt[4 * e + 1] = (uint8_t)(0xB0 | e); pkt[4 * e + 2] = 7; pkt[4 * e + 3] = 100;
    }
    for (k = 0; k < 5; k++)                          /* 5 polls at 2 kHz before the next half drains */
        taken += (uint32_t)ep1_take(pkt, sizeof pkt);
    bad += check("a USB burst fills the MIDI ring without overflowing it", taken == 3u && !midi_in_overflow && mi_w - mi_r == 48u);
    bad += check("the packet that does not fit is refused (left for the next poll)", !ep1_take(pkt, sizeof pkt) && mi_w - mi_r == 48u);
    for (e = 0; e < 8; e++)                          /* TRS MIDI meanwhile: its 8 slots are free */
        midi_in_event(0x0Bu | 0xB1u << 8 | 1u << 16 | e << 24);
    bad += check("TRS MIDI into a ring full of USB still fits (no overflow)", !midi_in_overflow && mi_w - mi_r == 56u);
    events_block(CTL);
    bad += check("the ring drains with no panic: the held note keeps sounding", gate_note(&trk[0], 48) && mi_r == mi_w);
    bad += check("the refused packet is taken once the ring has room", ep1_take(pkt, sizeof pkt) && mi_w - mi_r == 16u);
    events_block(CTL);
    bad += check("the held note survives all 88 events", gate_note(&trk[0], 48) && !midi_in_overflow);
    for (k = 0; k < 3; k++) ep1_take(pkt, sizeof pkt);
    midi_in_event(0x09u | 0x90u << 8 | 50u << 16 | 100u << 24);   /* one more event: 49 used, 15 free */
    bad += check("free space below one packet refuses a whole packet", !ep1_take(pkt, sizeof pkt) && !midi_in_overflow);
    events_block(CTL);
    return bad;
}
/* GLO > SYSTEM ROUT (#68): CH1-4 listens to channels 1..4 only, channels 5..16 are free for other instruments
 * (notes, bend, CCs, aftertouch, panic and reset all ignored, from USB and TRS); SEL plays the selected track
 * from every channel; a switch to CH1-4 lets go of what channels 5..16 held */
static int any_gate(void)
{
    for (uint32_t i = 0; i < NTRK * NVOICE; i++) if (trk[i / NVOICE].v[i % NVOICE].active && trk[i / NVOICE].v[i % NVOICE].gate) return 1;
    return 0;
}
static int route_test(void)
{
    int bad = 0; uint32_t ch, src, owned = 0;
    midi_test_reset(); song.sel = 2;
    bad += check("ROUT defaults to CH1-4", song.g[G_ROUTE] == 0);
    for (src = 1; src <= 2u; src++)
        for (ch = 4; ch < 16u; ch++) queued(0x90 | ch, 60 + ch, 100, src);
    for (ch = 4; ch < 16u; ch++) owned |= midi_notes[ch][60 + ch];
    bad += check("CH1-4: note-ons on channels 5..16 (USB and TRS) play nothing", !any_gate() && !owned && !midi_owners[2] && !midi_hint);
    queued(0x91, 62, 100, 1); queued(0x93, 40, 100, 2);
    bad += check("CH1-4: channels 2 and 4 still play parts 2 and 4", gate_note(&trk[1], 62) && midi_notes[1][62] == 2u && midi_notes[3][40] == 4u);
    queued(0xE4, 127, 127, 1); queued(0xEF, 0, 0, 2); queued(0xE9, 127, 127, 1);
    bad += check("CH1-4: pitch bend on channels 5, 10, 16 bends no part", !midi_bend_target[0] && !midi_bend_target[1] && !midi_bend_target[2] && !midi_ch[4].bend && !midi_ch[15].bend);
    queued(0xB4, 1, 99, 1); queued(0xB9, 11, 10, 2); queued(0xDF, 77, 0, 1);
    bad += check("CH1-4: CC1 / CC11 and aftertouch on channels 5..16 reach no MOD source", !trk[2].mw && !trk[2].ex_off && !trk[2].at && !trk[0].mw && !trk[0].at);
    queued(0xB4, 101, 0, 1); queued(0xB4, 100, 0, 1); queued(0xB4, 6, 24, 1);
    bad += check("CH1-4: RPN on channel 5 changes nothing", midi_ch[4].semis != 24u);
    queued(0xB4, 64, 127, 1); queued(0x81, 62, 0, 1);
    bad += check("CH1-4: a channel 5 sustain pedal does not hold channel 2's note", !gate_note(&trk[1], 62) && !midi_notes[1][62]);
    queued(0x91, 62, 100, 1); queued(0xB1, 1, 50, 1);
    queued(0xB4, 120, 0, 1); queued(0xB9, 123, 0, 2); queued(0xBF, 121, 0, 1);
    bad += check("CH1-4: CC120 / CC123 / CC121 on channels 5..16 leave parts 1..4 sounding", gate_note(&trk[1], 62) && gate_note(&trk[3], 40) && midi_owners[1] == 1u && trk[1].mw == 50);
    queued(0xB1, 123, 0, 1); queued(0x83, 40, 0, 2);
    bad += check("CH1-4: panic on channel 2 still works", !gate_note(&trk[1], 62) && !midi_owners[1] && !midi_owners[3]);

    midi_test_reset(); song.sel = 2; song.g[G_ROUTE] = 1; trk[2].p[P_VOICE] = V_POLY; trk[2].p[P_SUS] = 127; events_block(CTL);
    queued(0x90, 60, 100, 1); queued(0x94, 62, 100, 2); queued(0x99, 64, 100, 1); queued(0x9F, 65, 100, 2);
    bad += check("SEL: channels 1, 5, 10, 16 all play the selected track", gate_note(&trk[2], 60) && gate_note(&trk[2], 62) && gate_note(&trk[2], 64) && gate_note(&trk[2], 65) && !gate_note(&trk[0], 60) && midi_owners[2] == 4u);
    queued(0xEF, 127, 127, 1); queued(0xB9, 1, 66, 2); queued(0xD4, 33, 0, 1);
    bad += check("SEL: bend, CC1 and aftertouch from channels 5..16 reach the selected track", midi_bend_target[2] == 512 && trk[2].mw == 66 && trk[2].at == 33);
    queued(0xB4, 64, 127, 1); queued(0x84, 62, 0, 2);
    bad += check("SEL: channel 5 pedal holds its note", gate_note(&trk[2], 62) && (midi_notes[4][62] & MIDI_PEDAL_NOTE));
    song.g[G_ROUTE] = 0; events_block(CTL);
    bad += check("SEL -> CH1-4 releases channels 5..16's notes (pedal-held too), keeps channel 1's",
                 !gate_note(&trk[2], 62) && !gate_note(&trk[2], 64) && !gate_note(&trk[2], 65) && gate_note(&trk[2], 60) &&
                 !midi_notes[4][62] && !midi_notes[9][64] && !midi_notes[15][65] && midi_owners[2] == 1u && !midi_ch[4].pedal);
    bad += check(".. and resets their bend: each part follows its own channel 1..4", !midi_bend_target[2] && !midi_ch[15].bend);
    queued(0x80, 60, 0, 1);
    bad += check("channel 1's note-off still releases its note after the switch", !any_gate() && !midi_owners[2]);
    return bad;
}
static int standalone_cc_test(void)
{
    int bad = 0;
    midi_test_reset(); song.sel = 2;
    for (uint32_t source = 1; source <= 2; source++) {
        for (uint32_t ch = 0; ch < NPART; ch++) {
            for (uint32_t k = 0; k < 8u; k++) {
                if (drum_track(&trk[ch]) && k>=1u && k<=3u) continue;
                const param_desc_t *d = track_desc(&trk[ch], P_E0 + k);
                queued(0xB0u | ch, 20u + k, 0, source);
                bad += check("USB/TRS engine CC minimum follows its channel", *drum_param_ref(&trk[ch],P_E0+k,0) == param_fit(d,d->min));
                queued(0xB0u | ch, 20u + k, 127, source);
                bad += check("USB/TRS engine CC maximum follows its channel", *drum_param_ref(&trk[ch],P_E0+k,0) == param_fit(d,d->max));
            }
            queued(0xB0u | ch, 7, 17u + ch, source);
            bad += check("four CC7 mixer strips are independent", trk[ch].p[P_LEVEL] == (int32_t)midi_volume(17u + ch));
        }
    }
    queued(0xB0, 10, 0, 1); queued(0xB1, 10, 127, 2);
    bad += check("pan reaches both signed endpoints", trk[0].p[P_PAN] == -64 && trk[1].p[P_PAN] == 63);
    queued(0xB0, 73, 43, 1); queued(0xB0, 75, 44, 1); queued(0xB0, 70, 45, 2); queued(0xB0, 72, 46, 2);
    bad += check("envelope CCs reach the addressed track", trk[0].p[P_ATK] == 43 && trk[0].p[P_DEC] == 44 && trk[0].p[P_SUS] == 45 && trk[0].p[P_REL] == 46);
    queued(0xB0, 90, 11, 1); queued(0xB0, 91, 12, 1); queued(0xB0, 93, 13, 2); queued(0xB0, 94, 14, 2);
    bad += check("effect sends are directly controllable", trk[0].p[P_DIST] == 11 && trk[0].p[P_REV] == 12 && trk[0].p[P_CHOR] == 13 && trk[0].p[P_DLY] == 14);
    for (uint32_t engine = 0; engine < NENGINES; engine++) {
        trk[1].eng_req = (uint8_t)engine;
        for (uint32_t k = 0; k < 8u; k++) {
            if (engine==ENGI_DRUM && k>=1u && k<=3u) continue;
            const param_desc_t *d = track_desc(&trk[1], P_E0 + k);
            int16_t before = trk[0].p[P_E0 + k];
            queued(0xB1, 20u + k, 0, 1);
            if (d->max > d->min)
                bad += check("every engine maps its own minimum", *drum_param_ref(&trk[1],P_E0+k,0) == param_fit(d,d->min));
            queued(0xB1, 20u + k, 127, 2);
            if (d->max > d->min)
                bad += check("every engine maps its own maximum", *drum_param_ref(&trk[1],P_E0+k,0) == param_fit(d,d->max));
            bad += check("an engine edit never leaks into another track", trk[0].p[P_E0 + k] == before);
        }
    }
    midi_test_reset(); song.sel = 2;
    um_byte(0xB1); um_byte(7); um_byte(64); um_byte(20); um_byte(127); events_block(CTL);
    const param_desc_t *uart_d = track_desc(&trk[1], P_E0);
    bad += check("real TRS parser with running status reaches track 2 volume and engine", trk[1].p[P_LEVEL] == (int32_t)midi_volume(64) && trk[1].p[P_E0] == param_fit(uart_d, uart_d->max) && trk[0].p[P_LEVEL] == TP[P_LEVEL].def);
    int16_t keep = trk[0].p[P_LEVEL]; queued(0xB4, 7, 99, 1);
    bad += check("CH1-4 ignores unsupported channels' parameter CCs", trk[0].p[P_LEVEL] == keep);
    song.g[G_ROUTE] = 1; events_block(CTL);
    queued(0xB0, 7, 61, 1); queued(0xB1, 7, 62, 2);
    bad += check("fixed faders remain independent in SEL mode", trk[0].p[P_LEVEL] == (int32_t)midi_volume(61) && trk[1].p[P_LEVEL] == (int32_t)midi_volume(62) && trk[2].p[P_LEVEL] == TP[P_LEVEL].def);
    queued(0xB0, 73, 90, 1);
    bad += check("other parameter CCs preserve SEL routing", trk[2].p[P_ATK] == 90);
    midi_test_reset(); trk[0].p[P_REV] = 23; song.sel = 0; song.rec = 1;
    seq_start(); seq_tick(&trk[0], CTL); queued(0xB0, 91, 92, 2);
    bad += check("TRS CC edit records motion while preserving the patch base", motion.count == 1u && motion.event[0].param == P_REV && motion.event[0].value == 92 && motion_base_value(&trk[0], P_REV) == 23);
    seq_stop();
    bad += check("stop restores the original base after a recorded MIDI edit", trk[0].p[P_REV] == 23);
    seq_start(); seq_tick(&trk[0], CTL); song.rec = 0; queued(0xB0, 91, 37, 1); seq_stop();
    bad += check("unrecorded MIDI edits survive stopping automation", trk[0].p[P_REV] == 37);
    midi_test_reset(); queued(0xB0, 1, 127, 2);
    int16_t depth = trk[0].p[P_LD_PIT];
    bad += check("wheel provides pitch modulation without changing the patch", mod_wheel_pitch(&trk[0], 32767) == 7 && mod_wheel_pitch(&trk[0], -32768) == -8 && trk[0].p[P_LD_PIT] == depth);
    queued(0xB0, 1, 0, 1);
    bad += check("wheel zero restores unmodulated pitch", !mod_wheel_pitch(&trk[0], 32767));
    queued(0xB0, 1, 127, 1); trk[0].p[P_M1SRC] = MS_MODW; trk[0].p[P_M1DST] = MD_VIB; trk[0].p[P_M1AMT] = 32;
    bad += check("an explicit wheel matrix assignment overrides default vibrato", !mod_wheel_pitch(&trk[0], 32767));
    trk[3].mw = 127;
    bad += check("drums ignore wheel vibrato", !mod_wheel_pitch(&trk[3], 32767));
    return bad;
}

static int master_filter_test(void)
{
    int bad = 0; midi_test_reset(); perf_k[0] = 0;
    int16_t original[NTRK][P_COUNT];
    for (uint32_t k = 0; k < NTRK; k++) memcpy(original[k], trk[k].p, sizeof original[k]);
    queued(0xB0, 19, 0, 1);
    bad += check("DJ CC reaches low-pass end without holding FX", perf_k[0] == -100 && perf_begin(CTL));
    queued(0xB3, 19, 127, 2);
    bad += check("DJ CC reaches high-pass end from another valid channel", perf_k[0] == 100);
    queued(0xB4, 19, 0, 2);
    bad += check("CH1-4 ignores master CC on channel 5", perf_k[0] == 100);
    queued(0xB1, 19, 63, 1);
    bad += check("DJ lower centre value bypasses filter", !perf_k[0]);
    queued(0xB2, 19, 64, 2);
    bad += check("DJ upper centre value bypasses filter", !perf_k[0]);
    int unchanged = 1;
    for (uint32_t k = 0; k < NTRK; k++) unchanged &= !memcmp(original[k], trk[k].p, sizeof original[k]);
    bad += check("master filter never overwrites track sounds or mixer", unchanged);
    return bad;
}

static int home_knob_test(void)
{
    int bad = 0; midi_test_reset();
    for (uint32_t e = 0; e < NENGINES; e++) {
        set_engine_of(&trk[1], e);
        const engine_t *engine = ENGINES[trk[1].eng_req % NENGINES];
        for (uint32_t knob = 0; knob < 4; knob++) {
            uint32_t id = engine->knob[knob];
            const param_desc_t *desc = track_desc(&trk[1], id);
            int16_t other = trk[0].p[id];
            queued(0xB1, 28 + knob, 127, 2);
            bad += check("bottom row matches HOME knob maximum in every engine", *drum_param_ref(&trk[1],id,0) == param_fit(desc,desc->max));
            queued(0xB1, 28 + knob, 0, 1);
            bad += check("bottom row matches HOME knob minimum in every engine", *drum_param_ref(&trk[1],id,0) == param_fit(desc,desc->min));
            bad += check("HOME knob CC stays on its MIDI channel", trk[0].p[id] == other);
        }
    }
    return bad;
}

static int browse_test(void)
{
    int bad = 0; midi_test_reset();
    track_t *t = &trk[1];
    set_engine_of(t, 0u);
    int16_t keep_level = t->p[P_LEVEL] = 77;
    t->step[0].note[0] = 64;
    uint32_t original = t->preset, other = trk[0].preset;
    queued(0xB1, 114, 65, 1);
    bad += check("preset browsing is deferred outside the audio callback", t->preset == original);
    midi_browse_poll();
    bad += check("main encoder browses only its MIDI channel", t->preset != original && trk[0].preset == other && song.sel == 0);
    queued(0xB1, 114, 63, 2); midi_browse_poll();
    bad += check("reverse encoder returns to previous sound", t->preset == original);
    queued(0xB1, 112, 65, 2); midi_browse_poll();
    bad += check("shift encoder selects next visible engine", t->eng_req == eng_step(0u, 1));
    bad += check("browsing preserves volume and sequencer", t->p[P_LEVEL] == keep_level && t->step[0].note[0] == 64);
    queued(0xB1, 112, 63, 1); midi_browse_poll();
    bad += check("reverse bank restores previous engine", t->eng_req == 0u);
    queued(0xB1, 114, 64, 1);
    bad += check("relative centre value is ignored", midi_browse_r == midi_browse_w);
    queued(0xB4, 112, 65, 2);
    bad += check("CH1-4 ignores browse on channel 5", midi_browse_r == midi_browse_w);
    song.g[G_ROUTE] = 1; song.sel = 2;
    queued(0xB7, 112, 65, 1); uint32_t before = trk[2].eng_req;
    song.sel = 0; midi_browse_poll();
    bad += check("SEL resolves browse target when MIDI arrives", trk[2].eng_req == eng_step(before, 1) && song.sel == 0);
    for (uint32_t n = 0; n < 32; n++) queued(0xB0, 114, 65, 1);
    bad += check("rapid browse queue stays bounded", (midi_browse_w + MIDI_BROWSE_N - midi_browse_r) % MIDI_BROWSE_N == MIDI_BROWSE_N - 1);
    while (midi_browse_r != midi_browse_w) midi_browse_poll();
    midi_test_reset(); song.sel = 0; set_engine_of(TSEL, 0u);
    midi_parameter(TSEL, P_E0, 7); int16_t saved = TSEL->p[P_E0]; up_store(31, "BROWSE");
    song.sel = 1; set_engine_of(TSEL, 0u); song.sel = 0;
    uint32_t total; (void)eng_list_pos_of(&trk[1], &total);
    for (uint32_t n = 0; n < total - 1u; n++) {
        queued(0xB1, 114, 65, 2); midi_browse_poll();
    }
    bad += check("engine browsing reaches saved user sound on addressed track", trk[1].user == 32u && trk[1].p[P_E0] == saved && song.sel == 0);
    queued(0xB1, 114, 65, 1); midi_browse_poll();
    bad += check("preset list wraps from user sound to first factory sound", !trk[1].user && trk[1].preset == 0);
    return bad;
}

static int drum_controls_test(void)
{
    int bad=0; midi_test_reset(); track_t *t=&trk[1]; set_engine_of(t,ENGI_DRUM); events_block(CTL);
    int16_t base=t->p[P_E1]; song.sel=0;
    queued(0x91,36,100,2); queued(0xB1,28,127,2);
    bad+=check("a live DIN kick selects only its drum for the next knob", drum_focus[1]==0 && drum_value(t,0,P_E1)==127 && drum_value(t,1,P_E1)==base && t->p[P_E1]==base);
    queued(0x91,38,100,2); queued(0xB1,28,0,2); midi_ui_poll();
    bad+=check("a live snare selects its displayed drum track and lane", song.sel==1 && ui.lane==1 && drum_value(t,1,P_E1)==0 && drum_value(t,0,P_E1)==127);
    bad+=check("popup captures the edited drum name with the parameter", midi_popup.notice.kind==3 && midi_popup.notice.lane==1);
    queued(0xB1,21,127,2); queued(0xB1,22,127,2); queued(0xB1,23,0,2); queued(0xB1,76,0,2);
    bad+=check("DRUM top row controls selected pan/reverb/delay and bottom 8 volume", drum_value(t,1,P_PAN)==63 && drum_value(t,1,P_REV)==127 && drum_value(t,1,P_DLY)==0 && drum_value(t,1,P_LEVEL)==0);
    bad+=check("snare mix edits leave kick volume and sends untouched", drum_value(t,0,P_LEVEL)==112 && drum_value(t,0,P_REV)==t->p[P_REV]);
    queued(0xB1,7,100,2);
    bad+=check("fader continues to mix the whole drum track", t->p[P_LEVEL]==midi_volume(100) && drum_value(t,1,P_LEVEL)==0);
    trk_note_on(t,36,100); midi_ui_poll();
    bad+=check("sequencer-style hits never steal drum editing focus", drum_focus[1]==1 && ui.lane==1);
    ui.home=0; ui.page=(uint8_t)page_first(FAM_SEQ); ui.cursor=0; ui.bank=0;
    grid_keys(1u);
    bad+=check("FM-1 white step key sequences the lane selected by MIDI", (step_lanes(&t->step[0]) & (1u<<1))!=0);
    t->p[P_SLEN]=64; page_go(1); page_go(1); page_go(1); grid_keys(1u);
    bad+=check("64-step drum pattern edits the fourth 16-step page", ui.bank==3 && ui.cursor==48 && (step_lanes(&t->step[48])&(1u<<1)));
    project_capture(&proj_scratch); uint8_t keep[128]; memcpy(keep,proj_scratch.fm6[1],128);
    drum_controls_reset(t); project_restore_runtime(&proj_scratch);
    bad+=check("project and autosave restore per-drum sound/mix and selected lane", drum_value(t,0,P_E1)==127 && drum_value(t,1,P_E1)==0 && drum_value(t,1,P_LEVEL)==0 && drum_focus[1]==1 && ui.lane==1 && !memcmp(keep,"DRM1",4));
    up_store(30,"DRUM TEST"); drum_controls_reset(t); up_load_to(t,30);
    bad+=check("user presets retain independent drums", drum_value(t,0,P_E1)==127 && drum_value(t,1,P_E1)==0 && drum_value(t,1,P_LEVEL)==0);
    undo.keep=0; set_engine_of(t,ENGI_DRUM); undo_swap();
    bad+=check("undo sound load restores the independent drum settings", drum_value(t,0,P_E1)==127 && drum_value(t,1,P_E1)==0 && drum_value(t,1,P_LEVEL)==0);
    /* Render snare alone: only the snare is muted; the kick's nonzero send cannot leak. */
    midi_test_reset(); t=&trk[1]; set_engine_of(t,ENGI_DRUM); events_block(CTL);
    t->p[P_DIST]=t->p[P_CHOR]=t->p[P_DLY]=t->p[P_REV]=0;
    drum_focus[1]=0; *drum_param_ref(t,P_REV,1)=127;
    drum_focus[1]=1; *drum_param_ref(t,P_LEVEL,1)=0;
    trk_note_on(t,38,100); int32_t out[2*CTL]; mix_block(out,CTL);
    int dry=0,send=0; for (uint32_t i=0;i<CTL;i++) {dry|=mix_l[i]|mix_r[i];send|=send_r[i];}
    bad+=check("muting snare yields no dry signal or kick reverb leakage", !dry && !send);
    memset(t->v,0,sizeof t->v); memset(drum_kit[1],0,sizeof drum_kit[1]);
    trk_note_on(t,36,100); mix_block(out,CTL); dry=send=0;
    for (uint32_t i=0;i<CTL;i++) {dry|=mix_l[i]|mix_r[i];send|=send_r[i];}
    bad+=check("kick remains audible and reaches its own reverb bus", dry && send);
    drum_focus[1]=0; *drum_param_ref(t,P_PAN,1)=-64;
    t->p[P_PAN]=0; memset(t->v,0,sizeof t->v); memset(drum_kit[1],0,sizeof drum_kit[1]);
    trk_note_on(t,36,100); memset(mix_l,0,sizeof mix_l); memset(mix_r,0,sizeof mix_r); mod_begin(t); drum_mix_enabled=1; track_render(t,part_buf,CTL); mix_drum_lanes(t,CTL);
    int left=0,right=0;for (uint32_t i=0;i<CTL;i++) {left|=mix_l[i];right|=mix_r[i];}
    bad+=check("kick pan routes its dry sound independently to the left", left && !right);
    return bad;
}

static int transport_controls_test(void)
{
    int bad = 0; midi_test_reset(); song.rec = 0; song.sel = 2;
    queued(0xBF,108,127,2);
    bad += check("DIN record waits for UI thread", !song.rec && !song.playing);
    song.sel = 0; midi_transport_poll();
    bad += check("record arms displayed track at receipt, not channel 16 or later selection", song.rec==4 && transport_req==1);
    events_block(CTL);
    bad += check("record starts stopped sequencer", song.playing);
    queued(0xBF,108,127,2); midi_transport_poll();
    bad += check("held record does not toggle twice", song.rec==4);
    queued(0xBF,108,0,2); song.sel=2; queued(0xBF,108,127,2); midi_transport_poll();
    bad += check("second record press disarms only that track", !song.rec && song.playing);
    queued(0xBF,106,127,2); midi_transport_poll(); events_block(CTL);
    bad += check("STOP stops playback regardless of ROUT", !song.playing);
    queued(0xB0,107,127,1); midi_transport_poll(); events_block(CTL);
    bad += check("USB PLAY starts transport", song.playing);
    queued(0xB0,107,0,1); queued(0xB0,107,127,1); uint32_t step=trk[0].seq_pos; midi_transport_poll();
    bad += check("PLAY while playing does not toggle or restart", song.playing && !transport_req && trk[0].seq_pos==step);
    queued(0xBF,106,0,2); queued(0xBF,106,127,2);
    queued(0xB0,107,0,1); queued(0xB0,107,127,1); midi_transport_poll(); events_block(CTL);
    bad += check("queued STOP then PLAY preserves last command", song.playing);
    midi_test_reset(); song.g[G_BPM]=90;
    for (uint32_t k=0;k<3;k++) { fm1_ms=1000+500*k; queued(0xBF,109,127,2); queued(0xBF,109,0,2); }
    fm1_ms=7000; midi_transport_poll();
    bad += check("tap uses input timestamps even when UI polling is delayed", song.g[G_BPM]==120);
    song.g[G_CLOCK]=2; queued(0xBF,109,127,2); midi_transport_poll();
    bad += check("tap never overwrites external clock tempo", song.g[G_BPM]==120);
    midi_test_reset(); song.rec=0; ui.menu=1; queued(0xBF,108,127,2); midi_transport_poll();
    bad += check("record respects modal UI", !song.rec && !transport_req);
    ui.menu=0; queued(0xBF,108,0,2); chain.armed=1; queued(0xBF,108,127,2); midi_transport_poll();
    bad += check("record never edits while song chain is armed", !song.rec && !transport_req);
    midi_test_reset(); song.rec=0;
    for (uint32_t k=0;k<20;k++) { queued(0xBF,109,127,2); queued(0xBF,109,0,2); }
    queued(0xBF,106,127,2);
    bad += check("queue saturation cannot lose STOP", transport_req==2 && midi_transport_r==midi_transport_w);
    return bad;
}

int main(void)
{
    int bad = drum_controls_test() + transport_controls_test() + controller_feedback_test() + pads_test() + controls_test() + sustain_test() + ownership_test() + clock_test(1) + clock_test(2) + clock_arp_and_boundaries() +
              arp_ext_stop_test() + usb_burst_test() + route_test() + standalone_cc_test() + browse_test() + home_knob_test() + master_filter_test();
    printf("%s\n", bad ? "MIDI CONTROL/CLOCK TEST FAILED" : "MIDI control/clock integration tests passed"); return bad != 0;
}
