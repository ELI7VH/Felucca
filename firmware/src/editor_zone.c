/* SPDX-License-Identifier: GPL-3.0-only */
/* SMP_ZONE (EDITOR_PROTOCOL.md "Live zones"): read or change one zone of a published user slot while it plays.
 * The change goes into the RAM copy only (usr_zone): no flash write, the transport keeps running, and the slot
 * returns to its stored header at the next boot or upload. The zone is checked as smp_user_scan checks a stored one.
 * A sounding SAMPLE voice follows a new pitch (rate, root) at its next block and a new end or loop at once; a new
 * start (off) ends the voices on that zone, whose decoder was positioned in the old data. GRAIN re-reads the slot
 * (smp_user_gen); SLICE keeps the slices it scanned at upload. */
enum { ED_SMP_ZONE = 96 };
typedef char ed_zone_size_check[sizeof(smp_zone_t) == 28u ? 1 : -1];   /* the wire record is the stored zone */
/* slot, zone -> slot, zone, rc, then (rc 0) the zone as stored: pack7 of 28 bytes, off relative to the slot's data.
 * slot, zone, pack7 zone -> the same, after the change. rc: 0 ok, 1 arguments, 2 no such zone (slot empty or zone
 * past its count), 3 the zone is not valid for this slot's data. */
static int ed_zone_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    smp_zone_t z;
    uint32_t k, i, rc = 0, base;
    if (cmd != ED_SMP_ZONE)
        return 0;
    k = a[0];
    base = (uint32_t)(uintptr_t)(smp_user_xip(k) + SMP_USER_DATA) - (uint32_t)(uintptr_t)SMP_DATA;
    if (a[1] >= usr_nz[k])
        rc = 2;
    else if (n > 2u) {
        if (ed_unpack7(a + 2, n - 2u, (uint8_t *)&z, sizeof z) != sizeof z)
            rc = 1;
        else if (!smp_zone_ok(&z, ((const smp_user_hdr_t *)smp_user_xip(k))->data_len))
            rc = 3;
        else {
            uint32_t zi = 0x8000u | k << 5 | a[1], moved;
            z.off += base;
            moved = z.off != usr_zone[k][a[1]].off;
            fm1_irq_off();                             /* the audio ISR never sees half a zone */
            usr_zone[k][a[1]] = z;
            for (i = 0; moved && i < NTRK * NVOICE; i++) {
                voice_t *v = &trk[i / NVOICE].v[i % NVOICE];
                if (v->active && trk[i / NVOICE].engine == ENGI_SAMPLE && (uint32_t)v->s[4] == zi)
                    v->s[6] = 1;                       /* (sample_render: an ended one-shot) */
            }
            fm1_irq_on();
            smp_user_gen++;
        }
    }
    ed_b(k);
    ed_b(a[1]);
    ed_b(rc);
    if (!rc) {
        z = usr_zone[k][a[1]];
        z.off -= base;
        ed_bk_pack((const uint8_t *)&z, sizeof z);
    }
    return 1;
}
