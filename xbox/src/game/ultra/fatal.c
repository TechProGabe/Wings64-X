/* fatal.c - pw64_fatal: the game side's "stop with a message" (the patches,
 * the ROM loaders, the swap checks, and every libultra entry point that is
 * not written yet: stubs.c). Logs a WINGS64_FATAL line (the xemu runner and
 * console rounds grep for it) and shows the platform's error card, which
 * returns to the dashboard (xhw_fatal). From wings64-dc's src/dc/ultra/fatal.c,
 * over xhw.h instead of KOS. */
#include "xhw.h"

__attribute__((noreturn)) void pw64_fatal(const char* msg, unsigned int value) {
    static char line[160];
    xhw_logf("WINGS64_FATAL %s (0x%08x)", msg, value);
    wx_snprintf(line, sizeof line, "%s (0x%08x)", msg, value);
    xhw_fatal("Wings64-X stopped", line);
}
