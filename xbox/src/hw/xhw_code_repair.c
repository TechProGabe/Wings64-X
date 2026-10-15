/* xhw_code_repair.c - puts the game's code back the way default.xbe has it
 * (OpenCrossing-Xbox's xbox_code_repair.c, MIT; PLAN.md D5 "upgraded CPUs").
 *
 * OpenCrossing GitHub #2/#3: on CPU-upgraded consoles (a 1 GHz Coppermine
 * and a 1.4 GHz Tualatin, two BIOS setups) something loaded before the
 * title rewrites `0f 31` (rdtsc) as `cd 2e` (int 2Eh) in its code, by
 * signature, presumably to slow down games that time themselves with the
 * TSC. In OpenCrossing's .text it matched a pair that wasn't an rdtsc at
 * all and the game faulted. Any `0f 31` byte pair in our .text is exposed
 * the same way.
 *
 * Nothing in .text is ours to have changed after load (no relocations; the
 * kernel's import thunks are in .rdata), so at boot every byte that differs
 * from the file is put back; the one rdtsc the port has (xhw_clock.c's boot
 * measurement) is better restored too. A big difference means something
 * else (the wrong file on D:): that is logged and left alone.
 * Kill switches: -DXHW_CODE_REPAIR=0 (not built), -DXHW_CODE_REPAIR=2
 * (check and log only). */
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdlib.h>
#include <string.h>
#include "xhw.h"
#include "xhw_internal.h"

#ifndef XHW_CODE_REPAIR
#define XHW_CODE_REPAIR 1
#endif

#if XHW_CODE_REPAIR
#define XBE_PATH "D:\\default.xbe"
#define CHUNK 65536
#define MAX_FIX 64    /* more than this and it isn't a patcher */
#define LOG_FIX 8

static struct {
    unsigned va;
    unsigned char ram, disk;
} s_fix[MAX_FIX];

/* one byte of code. The kernel maps .text read-only and CR0.WP makes that
 * hold in ring 0 too (MmSetAddressProtect on it faulted the write in xemu),
 * so WP is cleared around the store, with interrupts off. */
static void put_byte(unsigned va, unsigned char v) {
    unsigned cr0, tmp;
    __asm__ volatile("pushfl\n\t"
                     "cli\n\t"
                     "movl %%cr0, %0\n\t"
                     "movl %0, %1\n\t"
                     "andl $0xfffeffff, %1\n\t"
                     "movl %1, %%cr0\n\t"
                     "movb %b3, (%2)\n\t"
                     "movl %0, %%cr0\n\t"
                     "popfl"
                     : "=&r"(cr0), "=&r"(tmp)
                     : "r"(va), "q"(v)
                     : "memory", "cc");
}

/* the .text section header, from the image header the kernel loaded */
static const XBE_SECTION_HEADER* text_section(void) {
    const XBE_FILE_HEADER* h = (const XBE_FILE_HEADER*)0x00010000;
    const XBE_SECTION_HEADER* s = h->PointerToSectionTable;
    unsigned i;
    for (i = 0; i < h->NumberOfSections; i++, s++)
        if (s->SectionName && strcmp((const char*)s->SectionName, ".text") == 0) return s;
    return NULL;
}

#ifdef XHW_CODE_REPAIR_TEST
/* test builds (xemu, a stock console): make the #3 patch ourselves, in the
 * one place it hit, so the boot shows the repair and the new town survives */
static void fake_patch(const XBE_SECTION_HEADER* s) {
    static const unsigned char sig[] = { 0x75, 0x0f, 0x31, 0xf6, 0x83, 0xf8, 0x01, 0x83, 0xd6, 0x0b };
    const unsigned char* p = (const unsigned char*)s->VirtualAddress;
    unsigned k;
    for (k = 0; k + sizeof sig <= s->FileSize; k++) {
        if (memcmp(p + k, sig, sizeof sig) != 0) continue;
        put_byte(s->VirtualAddress + k + 1, 0xcd);
        put_byte(s->VirtualAddress + k + 2, 0x2e);
        xhw_logf("[BOOT] code check test: patched %08x like the #3 BIOS\n", s->VirtualAddress + k + 1);
        return;
    }
    xhw_logf("[BOOT] code check test: signature not found\n");
}
#endif

void xhw_code_repair(void) {
    const XBE_SECTION_HEADER* s = text_section();
    DWORD t0 = GetTickCount(), got;
    HANDLE h;
    unsigned char* buf;
    unsigned off, n, diff = 0, i;
    int ok = 1;

    if (!s) {
        xhw_logf("[BOOT] code check: no .text section in the image header\n");
        return;
    }
#ifdef XHW_CODE_REPAIR_TEST
    fake_patch(s);
#endif
    h = CreateFileA(XBE_PATH, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        xhw_logf("[BOOT] code check: could not open %s (error %lu)\n", XBE_PATH, (unsigned long)GetLastError());
        return;
    }
    buf = malloc(CHUNK);
    if (!buf || SetFilePointer(h, (LONG)s->FileAddress, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        free(buf);
        CloseHandle(h);
        return;
    }
    n = s->FileSize;
    for (off = 0; off < n; off += CHUNK) {
        unsigned len = n - off < CHUNK ? n - off : CHUNK, k;
        const unsigned char* ram = (const unsigned char*)(s->VirtualAddress + off);
        if (!ReadFile(h, buf, len, &got, NULL) || got != len) {
            ok = 0;
            break;
        }
        if (memcmp(ram, buf, len) == 0) continue;
        for (k = 0; k < len; k++) {
            if (ram[k] == buf[k]) continue;
            if (diff < MAX_FIX) {
                s_fix[diff].va = s->VirtualAddress + off + k;
                s_fix[diff].ram = ram[k];
                s_fix[diff].disk = buf[k];
            }
            diff++;
        }
    }
    free(buf);
    CloseHandle(h);

    if (!ok) {
        xhw_logf("[BOOT] code check: read of %s failed at .text+%x (error %lu)\n", XBE_PATH, off,
                  (unsigned long)GetLastError());
        return;
    }
    if (!diff) {
        xhw_logf("[BOOT] code check: .text matches %s (%lu ms)\n", XBE_PATH, (unsigned long)(GetTickCount() - t0));
        return;
    }
    if (diff > MAX_FIX) {
        xhw_logf("[BOOT] code check: %u bytes of .text differ from %s, first %08x; not a patcher, left alone\n", diff,
                  XBE_PATH, s_fix[0].va);
        return;
    }
    if ((XHW_CODE_REPAIR != 2))
        for (i = 0; i < diff; i++) put_byte(s_fix[i].va, s_fix[i].disk);
    xhw_logf("[BOOT] code check: %u bytes of .text were changed in memory after load (BIOS patcher?), %s (%lu ms)\n",
              diff, (XHW_CODE_REPAIR != 2) ? "put back" : "left alone (XHW_CODE_REPAIR=2)",
              (unsigned long)(GetTickCount() - t0));
    for (i = 0; i < diff && i < LOG_FIX; i++)
        xhw_logf("[BOOT]   %08x: %02x, file %02x%s\n", s_fix[i].va, s_fix[i].ram, s_fix[i].disk,
                  (XHW_CODE_REPAIR != 2) && *(volatile unsigned char*)s_fix[i].va != s_fix[i].disk
                      ? " (write did not stick)" : "");
}
#else
void xhw_code_repair(void) {}
#endif
