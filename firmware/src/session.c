/* SPDX-License-Identifier: GPL-3.0-only
 * Automatic instrument resume, in project 4's existing power-safe A/B pair.
 * Never overwrite an ordinary project occupying that slot. No writes from ISR.
 * Uses the existing project scratch/wire buffers; no extra project-sized RAM. */
#define SESSION_SLOT 3u
#define SESSION_UI_OFF (PROJ_FM6_OFF - 8u)
_Static_assert(68u + NTRK * (P_COUNT + 2u + NSTEP * 9u) + sizeof(chain_config_t) + sizeof(motion_store_t) <= SESSION_UI_OFF, "resume metadata overlaps project music");
#define SESSION_MARK 0xA7u                 /* FUN8 reserved byte 65; byte 67: master filter + 100 */
static uint32_t session_scan_ms, session_change_ms, session_hash;
static uint8_t session_seen, session_error, session_external_hold;
static uint32_t session_external_ms;
static int session_idle(void)
{
    if (transport_busy() || session_quiet_frames < SESSION_QUIET_FRAMES || mi_r != mi_w || fm1_in.notes)
        return 0;
    for (uint32_t k = 0; k < NTRK; k++) {
        if (midi_owners[k] || trk[k].nheld) return 0;
        for (uint32_t v = 0; v < NVOICE; v++)
            if (trk[k].v[v].active && trk[k].v[v].gate) return 0;
    }
    return 1;
}
static void session_boot(void)
{
    session_seen = session_error = session_external_hold = 0;
    session_scan_ms = fm1_ms;
    session_quiet_frames = 0;
#if FELUCCA_FLASH
    if (!flash_ok) return;
    proj_wire_gen++;
    int n = st_load(OBJ_PROJECT0 + SESSION_SLOT, &proj_wire, sizeof proj_wire);
    if (n != sizeof proj_wire || proj_wire.raw[65] != SESSION_MARK ||
        !proj_import(&proj_scratch, &proj_wire, n)) return;
    uint32_t filter = proj_wire.raw[67];
    uint8_t view[8]; memcpy(view, proj_wire.raw + SESSION_UI_OFF, sizeof view);
    memcpy(&proj_slot[SESSION_SLOT], &proj_wire, sizeof proj_wire);
    if (!project_restore_runtime(&proj_scratch)) {
        perf_k[0] = (int8_t)(filter <= 200u ? (int32_t)filter - 100 : 0);
        for (uint32_t k=1;k<4;k++) perf_k[k] = view[k-1] <= 100u ? (int8_t)view[k-1] : 0;
        ui.home = view[3] != 0;
        if (view[4] < NPAGES) ui.page = view[4];
        ui.cursor = view[5] < NSTEP ? view[5] : 0;
        ui.bank = view[6] < 4u ? view[6] : 0;
        setlist_active=view[7] && view[7]<=PROJECT_SLOTS && view[7]!=4u ? view[7]-1u : 255u;
        if (setlist_active==255u) {
            /* Upgrade a recovery-only session into the first empty setlist song.
             * Existing songs are never replaced; the first quiet poll commits it. */
            for (uint32_t pos=0;pos<12u;pos++) {
                uint32_t slot=setlist_slot(pos);
                if (!project_used(slot)) { setlist_active=(uint8_t)slot; setlist_pick(pos); break; }
            }
        }
        proj_cur=setlist_active<PROJECT_SLOTS ? setlist_active : SESSION_SLOT;
        ui_message("SESSION RESTORED");
    }
    session_quiet_frames = 0;
#endif
}
static int session_pack(void)
{
    project_capture(&proj_scratch);
    proj_wire_gen++;
    if (!proj_pack(&proj_wire, &proj_scratch)) {
        if (!session_error) ui_message("AUTOSAVE FORMAT ERROR");
        session_error = 1;
        return 0;
    }
    proj_wire.raw[65] = SESSION_MARK;
    proj_wire.raw[67] = (uint8_t)(clamp(perf_k[0], -100, 100) + 100);
    for (uint32_t k=1;k<4;k++) proj_wire.raw[SESSION_UI_OFF+k-1] = (uint8_t)clamp(perf_k[k],0,100);
    proj_wire.raw[SESSION_UI_OFF+3] = ui.home;
    proj_wire.raw[SESSION_UI_OFF+4] = ui.page;
    proj_wire.raw[SESSION_UI_OFF+5] = ui.cursor;
    proj_wire.raw[SESSION_UI_OFF+6] = ui.bank;
    proj_wire.raw[SESSION_UI_OFF+7]=setlist_active<PROJECT_SLOTS && setlist_active!=3u ? setlist_active+1u : 0u;
    uint32_t hash = proj_hash(proj_wire.raw, sizeof proj_wire - 4u);
    memcpy(proj_wire.raw + sizeof proj_wire - 4u, &hash, 4);
    return 1;
}
static void session_poll(void)
{
#if FELUCCA_FLASH
    if (!flash_ok || (uint32_t)(fm1_ms - session_scan_ms) < 250u) return;
    session_scan_ms = fm1_ms;
    /* Backup transfers also use proj_wire: leave their staging untouched. */
    if (session_external_hold) {
        if ((uint32_t)(fm1_ms - session_external_ms) < 15000u) return;
        session_external_hold = 0;
    }
    /* Track edits even during sound; wait for both five quiet seconds and five
     * seconds of unchanged musical state. This coalesces continuous tweaking. */
    if (transport_busy()) { session_seen = 0; setlist_pending=0; return; }
    if (project_used(SESSION_SLOT) && proj_slot[SESSION_SLOT].raw[65] != SESSION_MARK) {
        if (!session_error) ui_message("AUTOSAVE SLOT4 USED");
        session_error = 1;
        return;
    }
    if (!session_pack()) return;
    uint32_t hash=proj_hash(proj_wire.raw,sizeof proj_wire-4u);
    if (!session_seen || hash != session_hash) {
        session_hash = hash; session_seen = 1; session_change_ms = fm1_ms;
        return;
    }
    if ((uint32_t)(fm1_ms - session_change_ms) < 5000u ||
        !session_idle()) return;
    if (setlist_save_current()) { session_change_ms=fm1_ms; return; }
    if (!session_idle()) return;
    if (setlist_dirty && (uint32_t)(fm1_ms-setlist_change_ms)>=5000u) {
        if (st_save(OBJ_SETLIST,&setlist,sizeof setlist)) { ui_message("SETLIST SAVE ERROR"); setlist_change_ms=fm1_ms; return; }
        setlist_dirty=0;
    }
    if (!session_pack() || !session_idle()) return;
    int changed=memcmp(&proj_slot[SESSION_SLOT],&proj_wire,sizeof proj_wire)!=0;
    /* Catch a PLAY/note arriving just before the write. Individual flash hooks
     * guard IRQs themselves; do not mask interrupts through the entire save. */
    if (!session_idle()) return;
    if (changed && st_save(OBJ_PROJECT0 + SESSION_SLOT, &proj_wire, sizeof proj_wire)) {
        if (!session_error) ui_message("AUTOSAVE ERROR");
        session_error = 1;
        session_change_ms = fm1_ms;       /* retry after five seconds, no hot failure loop */
        return;
    }
    memcpy(&proj_slot[SESSION_SLOT], &proj_wire, sizeof proj_wire);
    session_error = 0;
    if (changed) ui_message("AUTOSAVED");
    if (setlist_pending && session_idle()) {
        uint32_t slot=setlist_pending-1u; setlist_pending=0;
        project_load(slot); session_seen=0;
        for (uint32_t pos=0;pos<12u;pos++) if (setlist_slot(pos)==slot) { setlist_pick(pos); break; }
    }
#endif
}
