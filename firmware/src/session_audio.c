/* SPDX-License-Identifier: GPL-3.0-only
 * Last-session autosave: observe the rendered master, including effect tails.
 * <= 2 Q15 counts (~-84 dBFS) is effectively silent. Runs before DAC attenuation
 * so turning the hardware master down cannot disguise a sounding instrument. */
#define SESSION_QUIET_FRAMES (44118u * 5u)
static volatile uint32_t session_quiet_frames;
static void session_audio_observe(const int32_t *out, uint32_t n)
{
    for (uint32_t i = 0; i < 2u * n; i++)
        if (out[i] < -2 || out[i] > 2) {
            session_quiet_frames = 0;
            return;
        }
    uint32_t q = session_quiet_frames;
    session_quiet_frames = q + n < SESSION_QUIET_FRAMES ? q + n : SESSION_QUIET_FRAMES;
}
