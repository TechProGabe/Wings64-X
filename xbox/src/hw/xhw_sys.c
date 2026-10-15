/* xhw_sys.c - time, threads, locks, memory, paths and logging on nxdk.
 * The interface is xbox/include/xhw.h; see docs/architecture.md. */
#include <hal/debug.h>
#include <hal/xbox.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
#include "xhw_internal.h"

/* ======================================================================
 * Logging: COM1 (xemu's lpc47m157, debug kits), a log-tail ring for the
 * crash/hang reports, and boot.log on the HDD (retail boards have no COM1).
 * ====================================================================== */
static inline unsigned char port_in(unsigned short p) {
    unsigned char v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void port_out(unsigned short p, unsigned char v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p));
}

/* Probe the 16550 scratch register once: on a retail board what an absent
 * port reads back is up to the board and modchip, and spinning on its LSR
 * would take ~0.1 s per byte (OpenCrossing-Xbox traps.md). */
static int s_com1 = -1;
static int com1_present(void) {
    if (s_com1 < 0) {
        port_out(0x3F8 + 7, 0x5A);
        s_com1 = port_in(0x3F8 + 7) == 0x5A;
        port_out(0x3F8 + 7, 0xA5);
        s_com1 = s_com1 && port_in(0x3F8 + 7) == 0xA5;
    }
    return s_com1;
}

static void com1_write(const char* s, size_t n) {
    size_t i;
    if (!com1_present()) return;
    for (i = 0; i < n; i++) {
        int spin = 100000;
        if (s[i] == '\n') {
            while (!(port_in(0x3F8 + 5) & 0x20) && --spin) {}
            port_out(0x3F8, '\r');
            spin = 100000;
        }
        while (!(port_in(0x3F8 + 5) & 0x20) && --spin) {}
        port_out(0x3F8, (unsigned char)s[i]);
    }
}

/* No locks: safe at any IRQL (crash reports inside a DPC). */
void xhw_com1_raw(const char* s, size_t n) { com1_write(s, n); }

#define TAIL_SIZE 4096
static char s_tail[TAIL_SIZE];
static volatile unsigned s_tail_pos;
static CRITICAL_SECTION s_log_cs;
static int s_log_cs_init;
static HANDLE s_bootlog = INVALID_HANDLE_VALUE;
static unsigned s_bootlog_bytes;
static int s_bootlog_dirty;       /* written since the last flush */
static uint64_t s_bootlog_flushed;
static int s_bootlog_part;        /* 0 boot.log, then 2 boot2.log, 3 boot3.log, 2, ... */
static HANDLE s_tracelog = INVALID_HANDLE_VALUE;
static unsigned s_tracelog_bytes;
static int s_tracelog_dirty;
/* boot.log keeps the first BOOTLOG_MAX bytes (boot, the first scenes); after
 * that the log goes on in boot2.log and boot3.log in turn, each restarted at
 * BOOTLOG_PART bytes, so the newest 2-4 MB before a late hang survive. */
#define BOOTLOG_MAX (4 * 1024 * 1024)
#define BOOTLOG_PART (2 * 1024 * 1024)
/* [DRAW] lines (-DXGX_DEBUG_TRACE, a few hundred KB a BACK press) go to
 * trace.log instead, restarted when it reaches TRACELOG_MAX. */
#define TRACELOG_MAX (64 * 1024 * 1024)

void xhw_flush_handle(HANDLE h) {
    IO_STATUS_BLOCK iosb;
    NtFlushBuffersFile(h, &iosb);
}

/* The folder boot.log is in: the save folder, or D:\ (next to default.xbe)
 * when the save folder can't be written, so a console whose saves, settings
 * and screenshots never appear still leaves a log (xhw_log_fallback). */
static const char* s_log_dir = "";
static unsigned s_log_fallback;   /* the save folder's error, 0 when boot.log is there */
#define LOG_MIN_FREE (1024 * 1024)   /* less free on E: than this counts as not writable */

static int e_has_room(void) {
    ULARGE_INTEGER free_b;
    return !GetDiskFreeSpaceExA("E:\\", &free_b, NULL, NULL) || free_b.QuadPart >= LOG_MIN_FREE;
}

unsigned xhw_log_fallback(void) { return s_log_fallback; }

void xhw_log_open_file(void) {
    char path[MAX_PATH], prev[MAX_PATH];
    s_log_dir = xhw_save_dir();
    snprintf(path, sizeof path, "%sboot.log", s_log_dir);
    /* the previous boot's log stays as boot_prev.log: a "Save and restart"
     * or a relaunch after a freeze would otherwise wipe the one that matters */
    snprintf(prev, sizeof prev, "%sboot_prev.log", s_log_dir);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) xhw_replace_file(path, prev);
    s_bootlog = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s_bootlog == INVALID_HANDLE_VALUE || !e_has_room()) {
        s_log_fallback = s_bootlog == INVALID_HANDLE_VALUE ? (unsigned)GetLastError() : ERROR_DISK_FULL;
        if (s_bootlog != INVALID_HANDLE_VALUE) {
            CloseHandle(s_bootlog);
            DeleteFileA(path);
        }
        s_log_dir = "D:\\";
        snprintf(path, sizeof path, "%sboot.log", s_log_dir);
        s_bootlog = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    /* the previous session's continuation files would read as this one's */
    snprintf(path, sizeof path, "%sboot2.log", s_log_dir);
    DeleteFileA(path);
    snprintf(path, sizeof path, "%sboot3.log", s_log_dir);
    DeleteFileA(path);
    snprintf(path, sizeof path, "%strace.log", s_log_dir);
    DeleteFileA(path);
}

/* Under the log lock: boot.log (or the current part) is full, go on in the
 * next part. */
static void bootlog_next_part(void) {
    char path[MAX_PATH];
    xhw_flush_handle(s_bootlog);
    CloseHandle(s_bootlog);
    s_bootlog_part = s_bootlog_part == 2 ? 3 : 2;
    snprintf(path, sizeof path, "%sboot%d.log", s_log_dir, s_bootlog_part);
    s_bootlog = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    s_bootlog_bytes = 0;
}

static void tracelog_write_locked(const char* s, size_t n) {
    DWORD w;
    if (s_tracelog == INVALID_HANDLE_VALUE) {
        char path[MAX_PATH];
        if (s_bootlog == INVALID_HANDLE_VALUE) return;   /* no save folder yet */
        snprintf(path, sizeof path, "%strace.log", s_log_dir);
        s_tracelog = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (s_tracelog == INVALID_HANDLE_VALUE) return;
    }
    if (s_tracelog_bytes + n > TRACELOG_MAX) {
        SetFilePointer(s_tracelog, 0, NULL, FILE_BEGIN);
        SetEndOfFile(s_tracelog);
        s_tracelog_bytes = 0;
    }
    WriteFile(s_tracelog, s, (DWORD)n, &w, NULL);
    s_tracelog_bytes += (unsigned)n;
    s_tracelog_dirty = 1;
}

/* Lines that must be on disk before anything else happens: a hang right
 * after them would otherwise take them along (the log was flushed at most
 * once a second, so a burst lost everything after its first line). */
static int log_line_urgent(const char* s, size_t n) {
    static const char* const k_tags[] = { "[SCENE]", "[GAME]", "[WARN]", "[MEM]", "[WDOG]", "[EEP]", "[FATAL]",
                                          "[CRASH]", "[BOOT]", "[ROM]", "[VIDEO]", "[NV2A] GPU", "[NV2A] flip", NULL };
    int i;
    for (i = 0; k_tags[i]; i++) {
        size_t k = strlen(k_tags[i]);
        if (n >= k && memcmp(s, k_tags[i], k) == 0) return 1;
    }
    return 0;
}

static void log_flush_locked(void) {
    if (s_tracelog_dirty) {   /* trace lines come in bursts (one frame's draws); the 1 Hz sync lands them */
        xhw_flush_handle(s_tracelog);
        s_tracelog_dirty = 0;
    }
    if (s_bootlog == INVALID_HANDLE_VALUE || !s_bootlog_dirty) return;
    xhw_flush_handle(s_bootlog);
    s_bootlog_dirty = 0;
    s_bootlog_flushed = xhw_time_ns();
}

static int s_log_no_com1;   /* set under the lock: the text already went to COM1 */

static int s_log_trace;   /* set under the lock: the current line is a [DRAW] line */

static void log_write_locked(const char* s, size_t n) {
    size_t i;
    if (s_log_trace) {   /* COM1 and trace.log only: keep boot.log and the report tail readable */
        if (!s_log_no_com1) com1_write(s, n);
        tracelog_write_locked(s, n);
        return;
    }
    for (i = 0; i < n; i++) s_tail[(s_tail_pos + i) % TAIL_SIZE] = s[i];
    s_tail_pos += (unsigned)n;
    if (!s_log_no_com1) com1_write(s, n);
    if (s_bootlog != INVALID_HANDLE_VALUE && s_bootlog_bytes >= (s_bootlog_part ? BOOTLOG_PART : BOOTLOG_MAX))
        bootlog_next_part();
    if (s_bootlog != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(s_bootlog, s, (DWORD)n, &w, NULL);
        s_bootlog_bytes += (unsigned)n;
        s_bootlog_dirty = 1;
        /* Every line while booting and every urgent one (scene changes,
         * warnings, faults); otherwise at most once a second here, and the
         * watchdog's 1 Hz tick (xhw_log_sync) flushes whatever is left, so
         * at most a second of routine lines can be lost. A flush per line
         * costs a disk write, and a chatty scene then runs at a few fps. */
        if (xhw_frame_count() < 600 || log_line_urgent(s, n) || xhw_time_ns() - s_bootlog_flushed > 1000000000ull)
            log_flush_locked();
    }
}

static void log_lock_init(void) {
    if (!s_log_cs_init) {
        InitializeCriticalSection(&s_log_cs);
        s_log_cs_init = 1;
    }
}

/* Flush pending lines without waiting for the lock (the watchdog, 1 Hz). */
void xhw_log_sync(void) {
    if (!s_log_cs_init || !TryEnterCriticalSection(&s_log_cs)) return;
    log_flush_locked();
    LeaveCriticalSection(&s_log_cs);
}

/* A line from a thread that must not block on the log lock (the watchdog):
 * waits up to ~0.5 s for it, then writes anyway. A thread stuck holding the
 * lock is exactly the case the report is for; an interleaved line is the
 * lesser evil. Returns 1 if the lock was taken. */
static int log_try(const char* line, int com1) {
    size_t n = strlen(line);
    int tries, locked = 0;
    log_lock_init();
    for (tries = 0; tries < 50 && !(locked = TryEnterCriticalSection(&s_log_cs)); tries++) Sleep(10);
    s_log_no_com1 = !com1;
    log_write_locked(line, n);
    if (n == 0 || line[n - 1] != '\n') log_write_locked("\n", 1);
    s_log_no_com1 = 0;
    log_flush_locked();
    if (locked) LeaveCriticalSection(&s_log_cs);
    return locked;
}

int xhw_log_try(const char* line) { return log_try(line, 1); }
int xhw_log_try_file(const char* text) { return log_try(text, 0); }

/* A console round's chain (xhw_launch_xbe): boot.log closed and renamed to
 * name, so the next boot's rotation (which keeps one previous log) leaves
 * it. The game serves no FTP, so the round's logs are pulled at the end. */
void xhw_log_keep(const char* name) {
    char from[MAX_PATH], to[MAX_PATH];
    log_lock_init();
    EnterCriticalSection(&s_log_cs);
    if (s_bootlog != INVALID_HANDLE_VALUE) {
        log_flush_locked();
        CloseHandle(s_bootlog);
        s_bootlog = INVALID_HANDLE_VALUE;
        snprintf(from, sizeof from, "%sboot.log", s_log_dir);
        snprintf(to, sizeof to, "%s%s", s_log_dir, name);
        xhw_replace_file(from, to);
    }
    LeaveCriticalSection(&s_log_cs);
}

/* One line, newline appended if missing, under one lock so lines from other
 * threads never land inside it. */
static void log_write(const char* s, size_t n, int newline) {
    log_lock_init();
    EnterCriticalSection(&s_log_cs);
    s_log_trace = n >= 6 && memcmp(s, "[DRAW]", 6) == 0;
    log_write_locked(s, n);
    if (newline && (n == 0 || s[n - 1] != '\n')) log_write_locked("\n", 1);
    s_log_trace = 0;
    LeaveCriticalSection(&s_log_cs);
}

/* COM1 only, one whole line: bulk output (screenshots) that would drown
 * boot.log, whose every line is flushed to disk. */
void xhw_log_com1_line(const char* line) {
    size_t n = strlen(line);
    log_lock_init();
    EnterCriticalSection(&s_log_cs);
    com1_write(line, n);
    if (n == 0 || line[n - 1] != '\n') com1_write("\n", 1);
    LeaveCriticalSection(&s_log_cs);
}

size_t xhw_log_tail(char* out, size_t cap) {
    unsigned end = s_tail_pos, len = end < TAIL_SIZE ? end : TAIL_SIZE, i;
    if (cap == 0) return 0;
    if (len > cap - 1) len = (unsigned)cap - 1;
    for (i = 0; i < len; i++) out[i] = s_tail[(end - len + i) % TAIL_SIZE];
    out[len] = '\0';
    return len;
}

void xhw_log(const char* line) {
    log_write(line, strlen(line), 1);
}

void xhw_logf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    log_write(buf, (size_t)n, 1);
}

void xhw_vlog_raw(const char* fmt, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n < 0) return;
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    log_write(buf, (size_t)n, 0);
}

/* ======================================================================
 * Time: xhw_clock.c
 * ====================================================================== */
void xhw_sleep_ms(uint32_t ms) { Sleep(ms); }
void xhw_yield(void) { SwitchToThread(); }

/* ======================================================================
 * Threads and locks
 * ====================================================================== */
struct xhw_mutex { CRITICAL_SECTION cs; };

xhw_mutex* xhw_mutex_create(void) {
    xhw_mutex* m = (xhw_mutex*)malloc(sizeof *m);
    if (m) InitializeCriticalSection(&m->cs);
    return m;
}
void xhw_mutex_lock(xhw_mutex* m) { EnterCriticalSection(&m->cs); }
void xhw_mutex_unlock(xhw_mutex* m) { LeaveCriticalSection(&m->cs); }

struct xhw_event { HANDLE h; };

xhw_event* xhw_event_create(void) {
    xhw_event* e = (xhw_event*)malloc(sizeof *e);
    if (e) e->h = CreateEventA(NULL, FALSE, FALSE, NULL);
    return e;
}
void xhw_event_signal(xhw_event* e) { SetEvent(e->h); }
int xhw_event_wait(xhw_event* e, uint32_t timeout_ms) {
    return WaitForSingleObject(e->h, timeout_ms) == WAIT_OBJECT_0;
}

uint32_t xhw_tls_alloc(void) { return (uint32_t)TlsAlloc(); }
void* xhw_tls_get(uint32_t slot) { return TlsGetValue((DWORD)slot); }
void xhw_tls_set(uint32_t slot, void* value) { TlsSetValue((DWORD)slot, value); }

typedef struct { void (*fn)(void*); void* arg; } ThreadStart;

static DWORD WINAPI thread_entry(LPVOID p) {
    ThreadStart s = *(ThreadStart*)p;
    free(p);
    xhw_crash_guard(s.fn, s.arg);
    return 0;
}

int xhw_thread_start(void (*fn)(void*), void* arg, int priority, uint32_t stack_bytes) {
    ThreadStart* s = (ThreadStart*)malloc(sizeof *s);
    HANDLE h;
    if (!s) return 0;
    s->fn = fn;
    s->arg = arg;
    h = CreateThread(NULL, stack_bytes ? stack_bytes : 64 * 1024, thread_entry, s, 0, NULL);
    if (!h) {
        free(s);
        return 0;
    }
    if (priority > 2) priority = 2;
    if (priority < -2) priority = -2;
    SetThreadPriority(h, priority);   /* THREAD_PRIORITY_LOWEST..HIGHEST = -2..2 */
    CloseHandle(h);
    return 1;
}

void xhw_thread_set_priority(int priority) { SetThreadPriority(GetCurrentThread(), priority); }

/* ======================================================================
 * Memory
 * ====================================================================== */
/* nxdk's malloc commits what it hands out (dlmalloc on NtAllocateVirtualMemory
 * MEM_COMMIT), so nothing here is demand-committed (Melee-X's MEM1/ARAM were). */
void xhw_commit(const void* p, uint32_t bytes) {
    (void)p;
    (void)bytes;
}
uint32_t xhw_lazy_committed_kb(void) { return 0; }

uint32_t xhw_mem_free_kb(void) {
    MM_STATISTICS st;
    memset(&st, 0, sizeof st);
    st.Length = sizeof st;
    return MmQueryStatistics(&st) >= 0 ? (uint32_t)(st.AvailablePages * 4) : 0;
}

void xhw_mem_log(const char* where) {
    MM_STATISTICS st;
    memset(&st, 0, sizeof st);
    st.Length = sizeof st;
    if (MmQueryStatistics(&st) >= 0)
        xhw_logf("[MEM] %-18s free %5u KB of %5u KB (image %u KB, virt %u KB, pool %u KB)", where,
                 (unsigned)(st.AvailablePages * 4), (unsigned)(st.TotalPhysicalPages * 4),
                 (unsigned)(st.ImagePagesCommitted * 4), (unsigned)(st.VirtualMemoryBytesCommitted / 1024),
                 (unsigned)(st.PoolPagesCommitted * 4));
}

/* ======================================================================
 * Paths
 * ====================================================================== */
const char* xhw_game_dir(void) { return "D:\\"; }
const char* xhw_save_dir(void) { return XHW_UDATA_DIR; }

/* A copy from the disc keeps its read-only attribute, and the file could
 * then be neither replaced nor rewritten: writable both before and after. */
int xhw_copy_file(const char* from, const char* to) {
    SetFileAttributesA(to, FILE_ATTRIBUTE_NORMAL);
    if (!CopyFileA(from, to, FALSE)) return 0;
    SetFileAttributesA(to, FILE_ATTRIBUTE_NORMAL);
    return 1;
}

/* nxdk's MoveFileA never replaces (ReplaceIfExists = FALSE). The kernel's
 * rename is asked to replace first; only if the rename itself is refused
 * (not when `from` can't be opened) is the destination deleted and the
 * rename tried again, so at worst `to` is missing and `from` holds the new
 * contents (XHW_REPLACE_LOST_TO; settings.c recovers from that).
 * rename_file: 1 renamed, 0 the rename failed, -1 `from` can't be opened. */
static int rename_file(const char* from, const char* to, BOOLEAN replace) {
    ANSI_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    FILE_RENAME_INFORMATION ri;
    HANDLE h;
    NTSTATUS st;
    RtlInitAnsiString(&name, from);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, ObDosDevicesDirectory(), NULL);
    if (!NT_SUCCESS(NtOpenFile(&h, DELETE | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT)))
        return -1;
    ri.ReplaceIfExists = replace;
    ri.RootDirectory = ObDosDevicesDirectory();
    RtlInitAnsiString(&ri.FileName, to);
    st = NtSetInformationFile(h, &iosb, &ri, sizeof ri, FileRenameInformation);
    NtClose(h);
    return NT_SUCCESS(st);
}

int xhw_replace_file(const char* from, const char* to) {
    int r = rename_file(from, to, TRUE), deleted = 0;
    if (r == 0) {
        SetFileAttributesA(to, FILE_ATTRIBUTE_NORMAL);
        deleted = DeleteFileA(to) != 0;
        r = rename_file(from, to, FALSE);
        xhw_logf("[FILE] %s -> %s: replace refused, %s and renamed (%s)", from, to,
                 deleted ? "deleted" : "not deleted", r == 1 ? "ok" : "failed");
    }
    if (to[0] && to[1] == ':') xhw_flush_volume(to[0]);   /* FATX caches directory entries */
    if (r == 1) return XHW_REPLACE_OK;
    return deleted ? XHW_REPLACE_LOST_TO : XHW_REPLACE_FAILED;
}

int xhw_mkdir(const char* path) { return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS; }

static void fill_entry(const WIN32_FIND_DATAA* fd, xhw_dir_entry* out) {
    snprintf(out->name, sizeof out->name, "%s", fd->cFileName);
    out->is_dir = (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out->size = fd->nFileSizeLow;
}

void* xhw_dir_first(const char* pattern, xhw_dir_entry* out) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    fill_entry(&fd, out);
    return (void*)h;
}

int xhw_dir_next(void* handle, xhw_dir_entry* out) {
    WIN32_FIND_DATAA fd;
    if (!FindNextFileA((HANDLE)handle, &fd)) {
        FindClose((HANDLE)handle);
        return 0;
    }
    fill_entry(&fd, out);
    return 1;
}

void* xhw_file_open(const char* path) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return h == INVALID_HANDLE_VALUE ? NULL : (void*)h;
}

/* In 1 MB pieces, so other I/O on the disk (the log) gets turns. */
int xhw_file_read(void* file, uint32_t off, void* dst, uint32_t len) {
    uint8_t* d = (uint8_t*)dst;
    LONG hi = 0;
    if (SetFilePointer((HANDLE)file, (LONG)off, &hi, FILE_BEGIN) == INVALID_SET_FILE_POINTER &&
        GetLastError() != NO_ERROR)
        return 0;
    while (len) {
        DWORD want = len > (1u << 20) ? (1u << 20) : len, got = 0;
        if (!ReadFile((HANDLE)file, d, want, &got, NULL) || got == 0) return 0;
        d += got;
        len -= got;
    }
    return 1;
}

uint32_t xhw_file_size(void* file) { return (uint32_t)GetFileSize((HANDLE)file, NULL); }
void xhw_file_close(void* file) {
    if (file) CloseHandle((HANDLE)file);
}

/* <path>.tmp written and flushed, read back and compared, then renamed over
 * <path> (xhw_replace_file): a full E: or a power cut leaves the old file. */
int xhw_write_file_atomic(const char* path, const void* data, uint32_t len) {
    static unsigned char check[4096];
    char tmp[MAX_PATH];
    HANDLE h;
    DWORD w = 0, got;
    uint32_t off;
    int ok;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(h, data, len, &w, NULL) && w == len;
    xhw_flush_handle(h);
    CloseHandle(h);
    if (ok) {
        h = CreateFileA(tmp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        ok = h != INVALID_HANDLE_VALUE && GetFileSize(h, NULL) == len;
        for (off = 0; ok && off < len; off += got) {
            DWORD want = len - off > sizeof check ? sizeof check : len - off;
            ok = ReadFile(h, check, want, &got, NULL) && got == want &&
                 memcmp(check, (const unsigned char*)data + off, want) == 0;
        }
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    if (!ok || xhw_replace_file(tmp, path) != XHW_REPLACE_OK) {
        DeleteFileA(tmp);
        return 0;
    }
    return 1;
}

/* pdclib's FILE starts with the kernel file handle (_PDCLIB_fd_t is void*). */
void xhw_flush(void* stdio_file) {
    FILE* f = (FILE*)stdio_file;
    HANDLE h;
    if (!f) return;
    fflush(f);
    h = *(HANDLE*)f;
    if (h && h != INVALID_HANDLE_VALUE) xhw_flush_handle(h);
}

/* Flush FATX's cached directory entries of a whole volume (after a rename). */
void xhw_flush_volume(char drive) {
    char path[] = "\\??\\X:";
    ANSI_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    path[4] = drive;
    RtlInitAnsiString(&name, path);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (NT_SUCCESS(NtOpenFile(&h, GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              FILE_SYNCHRONOUS_IO_NONALERT))) {
        NtFlushBuffersFile(h, &iosb);
        NtClose(h);
    }
}

/* ======================================================================
 * 64-bit integer helpers the game triple (i686-pc-windows-gnu) calls by
 * their libgcc names. Built here, with nxdk's triple, the divisions below
 * become nxdk's __alldiv / __aulldiv.
 * ====================================================================== */
long long __divdi3(long long a, long long b) { return a / b; }
unsigned long long __udivdi3(unsigned long long a, unsigned long long b) { return a / b; }
long long __moddi3(long long a, long long b) { return a % b; }
unsigned long long __umoddi3(unsigned long long a, unsigned long long b) { return a % b; }
