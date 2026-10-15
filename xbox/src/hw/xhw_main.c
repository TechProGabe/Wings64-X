/* xhw_main.c - Xbox entry point (from Melee-X's xhw_main.c).
 *
 * Boot: hold the RAM above 64 MB, mount E:, create the save folder, open
 * boot.log, log the previous exit, start the watchdog, calibrate the clock
 * log, show the splash, find and verify the user's ROM next to default.xbe (D:\), then
 * hand over to the game side (wx_boot), which never returns. */
#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <nxdk/mount.h>
#include <pbkit/pbkit.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdio.h>
#include <string.h>

#include "xhw.h"
#include "xhw_internal.h"
#include "xrom.h"

static char s_exit_why[96];   /* xhw_exit_reason: written to lastexit.txt on the way out */

void xhw_error_screen(const char* title, const char* const* lines) {
    int i;
    xhw_watchdog_disable();
    pb_show_debug_screen();
    debugClearScreen();
    debugPrint("\n\n    " XHW_TITLE "\n\n    %s\n\n", title);
    for (i = 0; lines && lines[i]; i++) debugPrint("    %s\n", lines[i]);
    debugPrint("\n\n    Press any button or wait to return to the dashboard.\n");
}

/* Waits `secs` or until a button is pressed (one that was not already held
 * when the card went up), then goes to the dashboard. */
static void __attribute__((noreturn)) wait_then_dashboard(int secs) {
    DWORD t0 = GetTickCount();
    int armed = 0;
    while ((int)(GetTickCount() - t0) < secs * 1000) {
        xhw_pad p;
        int i, any = 0;
        xhw_pad_poll();
        for (i = 0; i < 4; i++)
            if (xhw_pad_get(i, &p) && (p.buttons || p.lt > 128 || p.rt > 128)) any = 1;
        if (!any) armed = 1;
        else if (armed) break;
        Sleep(30);
    }
    xhw_quit_to_dashboard();
}

void xhw_error_card(const char* title, const char* const* lines, int secs) {
    char why[96];
    int i;
    xhw_logf("[BOOT] card: %s", title);
    for (i = 0; lines && lines[i]; i++)
        if (lines[i][0]) xhw_logf("[BOOT]   %s", lines[i]);
    /* a caller that already set the exit reason (the ROM check) keeps it */
    if (!s_exit_why[0]) {
        snprintf(why, sizeof why, "card: %s", title);
        xhw_exit_reason(why);
    }
    xhw_splash_release();
    xhw_error_screen(title, lines);
    wait_then_dashboard(secs);
}

void xhw_fatal(const char* title, const char* msg) {
    const char* lines[2] = { msg, NULL };
    xhw_logf("[FATAL] %s: %s", title, msg);
    xhw_error_card(title, lines, 15);
}

/* XBE header: base address at +0x104, image size at +0x10C (the image is
 * mapped at 0x10000). The crash reporter lists stack words in this range. */
static void read_image_range(void) {
    const unsigned char* xbe = (const unsigned char*)0x00010000;
    xhw_image_base = *(const unsigned int*)(xbe + 0x104);
    xhw_image_end = xhw_image_base + *(const unsigned int*)(xbe + 0x10C);
}

/* How the previous boot ended: each way out of the XBE that runs code
 * writes lastexit.txt in the save folder, and the next boot logs it and
 * deletes it. No file: the console was switched off or reset, or the XBE
 * killed. Failures are ignored. */
#define EXIT_FILE XHW_UDATA_DIR "lastexit.txt"

void xhw_exit_reason(const char* why) { snprintf(s_exit_why, sizeof s_exit_why, "%s", why); }

void xhw_exit_write(const char* why) {
    char line[128];
    DWORD w;
    int n;
    HANDLE h;
    if (!why) {   /* the game went on after all */
        DeleteFileA(EXIT_FILE);
        return;
    }
    n = snprintf(line, sizeof line, "%s, retrace %u\r\n", why, xhw_frame_count());
    h = CreateFileA(EXIT_FILE, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
    WriteFile(h, line, (DWORD)n, &w, NULL);
    xhw_flush_handle(h);
    CloseHandle(h);
}

static void log_previous_exit(void) {
    char buf[128];
    DWORD got = 0;
    HANDLE h = CreateFileA(EXIT_FILE, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        xhw_logf("[BOOT] previous exit: not recorded (switched off, reset or killed)");
        return;
    }
    if (!ReadFile(h, buf, sizeof buf - 1, &got, NULL)) got = 0;
    CloseHandle(h);
    DeleteFileA(EXIT_FILE);
    while (got && (buf[got - 1] == '\n' || buf[got - 1] == '\r')) got--;
    buf[got] = '\0';
    xhw_logf("[BOOT] previous exit: %s", got ? buf : "(empty record)");
}

/* Saves, settings.ini and screenshots all go to the save folder; a console
 * where none of them appear (a full E:, a folder that can't be made) logs
 * why, and boot.log is then next to default.xbe (xhw_log_open_file). */
static void log_save_folder(void) {
    ULARGE_INTEGER free_b, total_b;
    unsigned err = xhw_log_fallback();
    if (GetDiskFreeSpaceExA("E:\\", &free_b, &total_b, NULL))
        xhw_logf("[BOOT] E: %u MB free of %u MB", (unsigned)(free_b.QuadPart >> 20),
                 (unsigned)(total_b.QuadPart >> 20));
    else
        xhw_logf("[BOOT] E: free space unknown (error %u)", (unsigned)GetLastError());
    if (!err) {   /* boot.log is there: can a new file be made and written next to it? */
        HANDLE h = CreateFileA(XHW_UDATA_DIR "probe.tmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                               NULL);
        DWORD w = 0;
        int ok = h != INVALID_HANDLE_VALUE && WriteFile(h, "ok\r\n", 4, &w, NULL) && w == 4;
        unsigned e = ok ? 0 : (unsigned)GetLastError();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        DeleteFileA(XHW_UDATA_DIR "probe.tmp");
        xhw_logf("[BOOT] save folder write test: %s (error %u)", ok ? "ok" : "FAILED", e);
        return;
    }
    xhw_logf("[BOOT] can't write " XHW_UDATA_ROOT " (error %u): this log is D:\\boot.log; saves, "
             "settings.ini and screenshots won't be kept", err);
    xhw_notice("Can't write to E:\\UDATA\\57580001 (E: full?).", "Saves, settings and screenshots won't be kept.");
}

/* The game side (xbox/src/game/wx_boot.c): gets the verified, normalised ROM
 * (z64 byte order, 8 MB), loads the settings, sets the video mode and runs
 * the game. Never returns. A weak default keeps the platform layer linkable
 * on its own: at M1 it shows what the ROM check found. */
void wx_boot(const uint8_t* rom, uint32_t size);
__attribute__((weak)) void wx_boot(const uint8_t* rom, uint32_t size) {
    const xrom_info* r = xhw_rom_info();
    char l1[80], l2[80], l3[80], l4[80], l5[80];
    const char* lines[8];
    (void)rom;
    snprintf(l1, sizeof l1, "File: D:\\%s (%u bytes)", r->file, (unsigned)size);
    snprintf(l2, sizeof l2, "Title \"%s\"  code %s  region %c  version %u", r->title, r->code, (char)r->region,
             (unsigned)r->version);
    snprintf(l3, sizeof l3, "CRC %08x %08x   byte order %s", (unsigned)r->crc1, (unsigned)r->crc2,
             r->order == XROM_ORDER_Z64 ? "z64" : r->order == XROM_ORDER_V64 ? "v64" : "n64");
    snprintf(l4, sizeof l4, "SHA-1 %s", r->sha1_hex);
    snprintf(l5, sizeof l5, "Matches the known %s dump.", r->label);
    lines[0] = l1; lines[1] = l2; lines[2] = l3; lines[3] = l4; lines[4] = "";
    lines[5] = l5; lines[6] = "The game is not linked yet (milestone M1)."; lines[7] = NULL;
    xhw_error_card("ROM verified: US v1.0", lines, 30);
}

static void main_body(void* arg) {
    (void)arg;
    xhw_ram_lock64();   /* before anything allocates (PLAN.md D2) */
    xhw_set_game_thread();
    if (!nxIsDriveMounted('E') && !nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\"))
        xhw_logf("[BOOT] could not mount E: (saves disabled)");
    CreateDirectoryA("E:\\UDATA", NULL);
    CreateDirectoryA(XHW_UDATA_ROOT, NULL);
    xhw_log_open_file();
    xhw_logf("[BOOT] " XHW_TITLE " (" __DATE__ " " __TIME__ ")%s", XHW_TEST_BUILD ? " test build" : "");
    log_save_folder();
    read_image_range();
    xhw_logf("[BOOT] image %08x-%08x %.*s, stack %p", xhw_image_base, xhw_image_end, (int)XeImageFileName[0].Length,
             XeImageFileName[0].Buffer, (void*)&arg);
    log_previous_exit();
    xhw_ram_lock64_log();
    xhw_code_repair();   /* before the clock reads the TSC and before any game code runs */
    xhw_watchdog_start();
    xhw_clock_calibrate();
    xhw_mem_log("boot");
    xhw_splash_show();
    xhw_autopad_load();
    {
        xrom_info rom_info;
        const uint8_t* rom = xhw_rom_load_verified(&rom_info);   /* or a card, then the dashboard */
        wx_boot(rom, rom_info.size);
    }
    xhw_quit_to_dashboard();
}

int main(void) {
    xhw_crash_guard(main_body, NULL);
    return 0;
}

/* Until the audio driver exists (M6) there is nothing to stop. */
__attribute__((weak)) void xhw_audio_shutdown(void) {}

void xhw_quit_to_dashboard(void) {
    xhw_exit_write(s_exit_why[0] ? s_exit_why : "quit to the dashboard");
    xhw_audio_shutdown();
    xhw_pad_shutdown();
    XLaunchXBE(NULL);
    for (;;) Sleep(1000);
}

/* Relaunch this XBE by the kernel's own path for it
 * (\Device\Harddisk0\Partition6\...\default.xbe, \Device\CdRom0\...):
 * XLaunchXBE takes \Device\ paths as they are, while a DOS path like
 * D:\default.xbe would become \??\D:;default.xbe in the launch data. It
 * quick-reboots and doesn't come back; it returns only when the path has
 * no folder in it, and then the console reboots. */
static void __attribute__((noreturn)) launch(char* path) {
    xhw_exit_write(s_exit_why[0] ? s_exit_why : "relaunch");
    xhw_audio_shutdown();
    xhw_pad_shutdown();
    XLaunchXBE(path);
    HalReturnToFirmware(HalRebootRoutine);
    for (;;) Sleep(1000);
}

/* A full reboot (to the dashboard): resets the GPU too. */
void xhw_reboot_cold(void) {
    xhw_exit_write(s_exit_why[0] ? s_exit_why : "cold reboot");
    HalReturnToFirmware(HalRebootRoutine);
    for (;;) Sleep(1000);
}

void xhw_reboot_self(void) {
    char path[300];
    const ANSI_STRING* img = &XeImageFileName[0];
    snprintf(path, sizeof path, "%.*s", (int)img->Length, img->Buffer);
    xhw_logf("[BOOT] restart: %s", path);
    launch(path);
}

/* The folder this XBE was launched from; 0 if there is none */
int xhw_image_folder(char* out, int size) {
    const ANSI_STRING* img = &XeImageFileName[0];
    int n = (int)img->Length, end, start;
    for (end = n - 1; end > 0 && img->Buffer[end] != '\\'; end--) {}
    for (start = end - 1; start > 0 && img->Buffer[start - 1] != '\\'; start--) {}
    if (end <= start || start <= 0 || end - start >= size) return 0;
    memcpy(out, img->Buffer + start, (size_t)(end - start));
    out[end - start] = '\0';
    return 1;
}

/* F:\ and E:\ as the kernel names them (XLaunchXBE wants \Device\ paths) */
void xhw_launch_xbe(const char* dos_path) {
    char path[300], line[340];
    const char* part = (dos_path[0] | 0x20) == 'f' ? "Partition6" : (dos_path[0] | 0x20) == 'e' ? "Partition1" : NULL;
    if (part && dos_path[1] == ':')
        snprintf(path, sizeof path, "\\Device\\Harddisk0\\%s%s", part, dos_path + 2);
    else
        snprintf(path, sizeof path, "%s", dos_path);
    snprintf(line, sizeof line, "[BOOT] next: %s", path);
    xhw_log_try(line);
    {
        char folder[64];
        if (xhw_image_folder(folder, sizeof folder)) {
            snprintf(line, sizeof line, "boot_%s.log", folder);
            xhw_log_keep(line);
        }
    }
    xhw_exit_reason("next XBE of a console round");
    if (strcmp(dos_path, "dashboard") == 0) xhw_quit_to_dashboard();
    launch(path);
}
