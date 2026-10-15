/* xhw_debug.c - console A/B switches without a rebuild (test builds only).
 *
 * E:\UDATA\57580001\debug.txt holds "name = value" lines (integers; "#"
 * starts a comment), read once at the first lookup. A switch the file does
 * not name, every switch of a release build, and every switch when the
 * file is missing return the built-in default. Put the file up over FTP
 * before a run (tools/xbox/console.py); each value used is logged once. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
#include "xhw_internal.h"

static char s_text[2048];
static int s_loaded;

static void load(void) {
    void* f;
    uint32_t n;
    s_loaded = 1;
    if (!XHW_TEST_BUILD) return;
    f = xhw_file_open(XHW_UDATA_DIR "debug.txt");
    if (!f) return;
    n = xhw_file_size(f);
    if (n > sizeof s_text - 1) n = sizeof s_text - 1;
    if (!xhw_file_read(f, 0, s_text, n)) n = 0;
    s_text[n] = '\0';
    xhw_file_close(f);
    xhw_logf("[DEBUG] debug.txt read (%u bytes)", (unsigned)n);
}

int xhw_debug_int(const char* name, int def) {
    const char* p;
    size_t len = strlen(name);
    if (!s_loaded) load();
    for (p = s_text; *p; ) {
        const char* line = p;
        const char* end = strchr(p, '\n');
        p = end ? end + 1 : p + strlen(p);
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '#' || strncmp(line, name, len) != 0) continue;
        line += len;
        while (*line == ' ' || *line == '\t') line++;
        if (*line != '=') continue;
        {
            int v = (int)strtol(line + 1, NULL, 0);
            xhw_logf("[DEBUG] %s = %d (debug.txt; default %d)", name, v, def);
            return v;
        }
    }
    return def;
}
