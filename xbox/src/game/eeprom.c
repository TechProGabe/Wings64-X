/* eeprom.c - the cartridge's 16 Kbit EEPROM as a file (PLAN.md D7).
 *
 * E:\UDATA\57580001\pilotwings64.eep holds the 0x800 bytes in the order
 * the cartridge stores them, which is what Project64 and Mupen64Plus read
 * and write, so a save moves between the port and an emulator as is. A
 * missing or short file reads as all 0xFF (an erased chip): the game then
 * formats its files ("file_load: checksum failure" once is normal,
 * wings64-dc traps.md). Writes go through xhw_write_file_atomic (temp,
 * read back, rename), so a full E: or a power cut leaves the last good
 * save. DIVERGES: the N64 wrote the chip in the background; this writes the
 * file before osEepromLongWrite returns (~a frame on the HDD), counted in
 * the log. */
#include <stdio.h>
#include <string.h>

#include "wx_game.h"
#include "xhw.h"

#define EEP_BYTES 0x800
static uint8_t s_eep[EEP_BYTES];
static int s_loaded;
static unsigned s_writes, s_fails;

static void path(char* out, int cap) { snprintf(out, (size_t)cap, "%spilotwings64.eep", xhw_save_dir()); }

void wx_eeprom_load(void) {
    char p[96];
    void* f;
    if (s_loaded) return;
    s_loaded = 1;
    memset(s_eep, 0xFF, sizeof s_eep);
    path(p, sizeof p);
    f = xhw_file_open(p);
    if (!f) {
        xhw_logf("[EEP] no %s: a fresh EEPROM (the game formats it)", p);
        return;
    }
    if (xhw_file_size(f) < EEP_BYTES || !xhw_file_read(f, 0, s_eep, EEP_BYTES)) {
        xhw_logf("[EEP] %s is short or unreadable (%u bytes): a fresh EEPROM", p, (unsigned)xhw_file_size(f));
        memset(s_eep, 0xFF, sizeof s_eep);
    } else {
        xhw_logf("[EEP] loaded %s", p);
    }
    xhw_file_close(f);
}

int wx_eeprom_read(uint32_t offset, uint8_t* buf, uint32_t n) {
    wx_eeprom_load();
    if (offset > EEP_BYTES || n > EEP_BYTES - offset) return 0;
    memcpy(buf, s_eep + offset, n);
    return 1;
}

int wx_eeprom_write(uint32_t offset, const uint8_t* buf, uint32_t n) {
    char p[96];
    uint64_t t0;
    wx_eeprom_load();
    if (offset > EEP_BYTES || n > EEP_BYTES - offset) return 0;
    if (!memcmp(s_eep + offset, buf, n)) return 1;
    memcpy(s_eep + offset, buf, n);
    path(p, sizeof p);
    t0 = xhw_time_us();
    if (!xhw_write_file_atomic(p, s_eep, EEP_BYTES)) {
        if (s_fails++ < 8) xhw_logf("[EEP] FAILED to save %s", p);
        xhw_notice("The save could not be written to E:.", "Is E: full? See boot.log.");
        return 1;   /* the game keeps its copy; the N64 chip never fails */
    }
    s_writes++;
    xhw_logf("[EEP] saved (%u bytes at 0x%03x, write %u, %u ms)", (unsigned)n, (unsigned)offset, s_writes,
             (unsigned)((xhw_time_us() - t0) / 1000));
    return 1;
}
