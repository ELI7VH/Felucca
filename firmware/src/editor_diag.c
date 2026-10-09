/* SPDX-License-Identifier: GPL-3.0-only
 * WaveLoop developer telemetry. Read-only; never scans/erases flash or captures a backup.
 * Reply: schema=1, rc=0, count, repeated (id, u32 in five 7-bit bytes).
 * Fields absent on a host build are unavailable, not zero measurements. */
#if FELUCCA_FLASH
#include "fm1_diag.h"
#endif
static void ed_diag_field(uint32_t count_pos, uint32_t id, uint32_t value)
{
    ed_b(id);
    for (uint32_t j = 0; j < 5u; j++) ed_b(value >> (7u * j));
    ed_out[count_pos]++;
}
static void ed_diag_reply(void)
{
    uint32_t cp, active = 0, songs = 0, patches = 0, samples = 0, bytes = 0;
    ed_b(1); ed_b(0); cp = ed_n; ed_b(0);
    for (uint32_t k = 0; k < NTRK; k++)
        for (uint32_t v = 0; v < NVOICE; v++) active += !!trk[k].v[v].active;
    for (uint32_t k = 0; k < PROJECT_SLOTS; k++) if (k != SESSION_SLOT) songs += !!project_used(k);
    for (uint32_t k = 0; k < UP_SLOTS; k++) patches += !!up_used(k);
#if FELUCCA_FLASH
    for (uint32_t k = 0; k < SMP_USER_SLOTS; k++) if (usr_nz[k]) {
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
        samples++; bytes += h->data_len;
    }
#endif
#define DF(id, value) ed_diag_field(cp, id, (uint32_t)(value))
    DF(1, fm1_ms); DF(2, song.cpu_q8 * 100u / 256u); DF(3, song.batt_raw);
    DF(4, song.playing); DF(5, active); DF(7, voice_kills);
    DF(8, song.sel + 1u); DF(9, song.g[G_BPM]); DF(10, FELUCCA_FLASH);
    DF(11, midi_in_overflow); DF(12, usb.resets); DF(13, usb.rx_pkts); DF(14, usb.tx_pkts);
    DF(15, usb.frame_stalls); DF(16, samples); DF(17, SMP_USER_SLOTS);
    DF(18, bytes); DF(19, SMP_USER_SLOTS * (SMP_USER_SIZE - SMP_USER_DATA));
    DF(20, songs); DF(21, 12); DF(22, patches); DF(23, UP_SLOTS);
    DF(24, setlist_active < PROJECT_SLOTS ? setlist_active + 1u : 0u);
    DF(25, setlist_pending); DF(26, session_error); DF(27, session_quiet_frames / FS);
    DF(28, usb.retries); DF(29, usb.suspends);
#if FELUCCA_FLASH
    DF(6, shed_count);
    DF(30, fm1_diag_main_ram()); DF(31, 98304u);
    DF(32, fm1_diag_pool()); DF(33, 0x54000u);
    DF(34, fm1_diag_app());
    DF(35, 0x8DFBCu); DF(36, fm1_diag_ram_code()); DF(37, 0x6000u);
    DF(38, 0x100000u); DF(39, FL_DATA_HI - FL_DATA_LO + FL_GLOB_HI - FL_GLOB_LO);
    DF(40, felucca_dbg.max_us); DF(41, felucca_dbg.late); DF(42, felucca_dbg.boots);
#if FELUCCA_UART
    DF(43, um.drops);
#endif
#endif
#undef DF
}
