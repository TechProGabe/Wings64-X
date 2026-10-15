/* audio.c - the game's audio tasks and AI buffers.
 *
 * Until M6 wires the RSP audio HLE (xbox/src/hw/asp_hle.c) and the AC97
 * pump, audio command lists are counted and dropped and the AI buffers go
 * nowhere; the shim's virtual DAC (os_shim.c) keeps the game's audio clock
 * at real time either way. DIVERGES: silent until M6. */
#include "wx_game.h"
#include "xhw.h"

static unsigned s_tasks, s_buffers;

void wx_audio_task(void* task) {
    (void)task;
    s_tasks++;
}

void wx_audio_rate(uint32_t hz) { xhw_logf("[AUDIO] AI rate %u Hz", (unsigned)hz); }

void wx_audio_buffer(void* buf, uint32_t bytes) {
    (void)buf;
    (void)bytes;
    s_buffers++;
}
