/* SPDX-License-Identifier: GPL-3.0-only
 * Twelve complete songs. Nine additional A/B pairs and an order A/B pair use
 * the retired third user-sample area; existing flash objects never move. */
#define SETLIST_MAGIC 0x31534C53u
static int setlist_valid(const setlist_store_t *s)
{
    uint32_t seen=0;
    if (s->magic!=SETLIST_MAGIC || s->pick>=SETLIST_SONGS) return 0;
    for (uint32_t i=0;i<SETLIST_SONGS;i++) {
        uint32_t k=s->order[i];
        if (k>=SETLIST_SONGS || (seen&(1u<<k))) return 0;
        seen|=1u<<k;
    }
    return 1;
}
static void setlist_boot(void)
{
    memset(&setlist,0,sizeof setlist);
#if FELUCCA_FLASH
    if (!flash_ok || st_load(OBJ_SETLIST,&setlist,sizeof setlist)!=sizeof setlist)
        memset(&setlist,0,sizeof setlist);
#endif
    if (!setlist_valid(&setlist)) {
        memset(&setlist,0,sizeof setlist); setlist.magic=SETLIST_MAGIC;
        for (uint32_t i=0;i<SETLIST_SONGS;i++) setlist.order[i]=(uint8_t)i;
    }
    proj_extra_key=255;
    for (uint32_t i=4;i<PROJECT_SLOTS;i++) project_cache_note(i);
    setlist_dirty=setlist_pending=0; setlist_active=255;
}
static uint32_t setlist_slot(uint32_t pos)
{
    uint32_t k=setlist.order[pos%SETLIST_SONGS];
    return k<3u ? k : k+1u; /* skip the recovery journal */
}
static void setlist_pick(uint32_t pos)
{
    if (pos>=SETLIST_SONGS || pos==setlist.pick) return;
    setlist.pick=(uint8_t)pos; setlist_dirty=1; setlist_change_ms=fm1_ms; ui.force=1;
}
static void setlist_move(int32_t delta)
{
    if (transport_busy()) { ui_message("STOP TO EDIT"); return; }
    int32_t to=clamp((int32_t)setlist.pick+delta,0,SETLIST_SONGS-1u);
    if (to==setlist.pick) return;
    uint8_t k=setlist.order[to]; setlist.order[to]=setlist.order[setlist.pick];
    setlist.order[setlist.pick]=k; setlist_pick((uint32_t)to);
}
static void setlist_request(uint32_t pos)
{
    if (transport_busy()) { ui_message("STOP TO LOAD"); return; }
    if (pos>=SETLIST_SONGS || !project_used(setlist_slot(pos))) { ui_message("EMPTY SLOT"); return; }
    setlist_pick(pos); setlist_pending=(uint8_t)(setlist_slot(pos)+1u);
    ui_message("WAIT FOR SILENCE");
}
/* Compare canonical music, excluding recovery UI metadata, before any erase. */
static int setlist_save_current(void)
{
    if (setlist_active>=PROJECT_SLOTS || setlist_active==3u) return 0;
    project_capture(&proj_scratch); proj_wire_gen++;
    if (!proj_pack(&proj_wire,&proj_scratch)) return 1;
    if (!memcmp(project_cached(setlist_active),&proj_wire,sizeof proj_wire)) return 0;
    return project_save(setlist_active);
}
