/* os_shim.c - the libultra OS/IO surface Pilotwings 64 calls, on the Xbox
 * kernel (PLAN.md D5), from wings64-dc's src/dc/ultra/os_shim.c.
 * Compiled with the decomp's headers and flags (the game triple); the
 * kernel is reached only through xhw.h.
 *
 * Model: every N64 thread is a real kernel thread at a fixed priority level
 * in N64 order (the Xbox kernel preempts strictly by priority, as libultra
 * did); message queues block for real under one lock (libultra's
 * "interrupts off"), each blocked thread on its own event; the VI event
 * comes from the retrace (xhw_wait_vblank: pbkit's vblank, or 60.000 Hz
 * from the ACPI timer); cart DMA is a copy from the verified ROM image held
 * in RAM (D2); graphics tasks run synchronously in osSpTaskStartGo (the
 * Fast3D interpreter draws and presents), then SP/DP done are posted.
 * Time is the ACPI timer, never an assumed CPU clock. */
#include <ultra64.h>
#include <PR/os_internal.h>
#include <string.h>

#include "xhw.h"
#include "../wx_game.h"
#include "../../gfx/f3d_interp.h"

#define MAX_THREADS 16
#define THREAD_STACK (128 * 1024)

/* ------------------------------------------------------------------ lock */

/* One recursive lock stands for libultra's interrupt mask: every queue,
 * timer and event table change happens under it. */
static xhw_mutex* s_lock;
static void lock(void) {
    if (!s_lock) s_lock = xhw_mutex_create();
    xhw_mutex_lock(s_lock);
}
static void unlock(void) { xhw_mutex_unlock(s_lock); }

/* ---------------------------------------------------------------- threads */

/* N64 priorities (0..255) onto the kernel's levels 2..14, keeping PW64's
 * order distinct: idle 0, app 10, kernel 12, audio 110, scheduler 127
 * (OS_PRIORITY_APPMAX), PI manager 150, render/fault 250, VI manager 254.
 * The watchdog sits above them all at 15. */
static int level_of(OSPri pri) {
    if (pri <= 0) return 2;
    if (pri <= 10) return 8;
    if (pri <= 12) return 9;
    if (pri <= 110) return 10;
    if (pri <= 127) return 11;
    if (pri <= 150) return 12;
    if (pri <= 250) return 13;
    return 14;
}

typedef struct {
    OSThread* t;
    xhw_thread* kt;
    xhw_event* ev;              /* this thread's wake-up */
    const void* volatile wait;  /* the queue (or timer kick) it sleeps on, NULL when running */
    void (*entry)(void*);
    void* arg;
} Thr;
static Thr s_thr[MAX_THREADS];
static int s_nthr;

static Thr* find_thread(OSThread* t) {
    int i;
    for (i = 0; i < s_nthr; i++)
        if (s_thr[i].t == t) return &s_thr[i];
    return NULL;
}

/* The calling thread's record; threads the shim didn't start (the boot
 * thread, the VI and timer threads) get one on first use. */
static Thr* self(void) {
    xhw_thread* me = xhw_thread_self();
    int i;
    for (i = 0; i < s_nthr; i++)
        if (s_thr[i].kt == me && me) return &s_thr[i];
    lock();
    if (s_nthr == MAX_THREADS) pw64_fatal("os_shim: too many threads", MAX_THREADS);
    i = s_nthr++;
    memset(&s_thr[i], 0, sizeof s_thr[i]);
    if (!me) {
        xhw_thread_adopt_self(8);
        me = xhw_thread_self();
    }
    s_thr[i].kt = me;
    s_thr[i].ev = xhw_event_create();
    unlock();
    return &s_thr[i];
}

/* under the lock: wake every thread sleeping on `obj` (they recheck) */
static void wake_all(const void* obj) {
    int i;
    for (i = 0; i < s_nthr; i++)
        if (s_thr[i].wait == obj && s_thr[i].ev) xhw_event_signal(s_thr[i].ev);
}

/* under the lock: sleep until wake_all(obj) or the timeout; returns with the lock held */
static void sleep_on(Thr* me, const void* obj, uint32_t timeout_ms) {
    me->wait = obj;
    unlock();
    xhw_event_wait(me->ev, timeout_ms);
    lock();
    me->wait = NULL;
}

static void thread_entry(void* p) {
    Thr* d = (Thr*)p;
    d->entry(d->arg);
    /* an N64 thread that returns just stops */
    for (;;) xhw_sleep_ms(1000);
}

void osCreateThread(OSThread* t, OSId id, void (*entry)(void*), void* arg, void* sp, OSPri pri) {
    Thr* d;
    (void)sp;   /* DIVERGES: the game's stacks (4-16 KB) are not used; every thread gets a 128 KB kernel stack */
    lock();
    d = find_thread(t);
    if (!d) {
        if (s_nthr == MAX_THREADS) pw64_fatal("osCreateThread: too many threads", (u32)id);
        d = &s_thr[s_nthr++];
        memset(d, 0, sizeof *d);
        d->ev = xhw_event_create();
    }
    d->t = t;
    d->entry = entry;
    d->arg = arg;
    t->id = id;
    t->priority = pri;
    t->state = OS_STATE_STOPPED;
    unlock();
}

void osStartThread(OSThread* t) {
    Thr* d = find_thread(t);
    if (!d) pw64_fatal("osStartThread: unknown thread", (u32)(uintptr_t)t);
    if (d->kt) return;
    t->state = OS_STATE_RUNNABLE;
    d->kt = xhw_thread_spawn(thread_entry, d, level_of(t->priority), THREAD_STACK);
    if (!d->kt) pw64_fatal("osStartThread: thread create failed", (u32)t->id);
    xhw_logf("[OS] thread %d started, N64 priority %d -> level %d", (int)t->id, (int)t->priority,
             level_of(t->priority));
    /* a higher-priority thread runs at once: the kernel preempts on its own */
}

void osSetThreadPri(OSThread* t, OSPri pri) {
    Thr* d = t ? find_thread(t) : self();
    if (!d) return;
    if (d->t) d->t->priority = pri;
    if (d->kt) xhw_thread_set_level(d->kt, level_of(pri));
}

OSPri osGetThreadPri(OSThread* t) {
    Thr* d = t ? find_thread(t) : self();
    return d && d->t ? d->t->priority : 0;
}

void osYieldThread(void) { xhw_yield(); }
OSThread* __osGetCurrFaultedThread(void) { return NULL; }
OSId osGetThreadId(OSThread* t) {
    Thr* d = t ? find_thread(t) : self();
    return d && d->t ? d->t->id : 0;
}

/* --------------------------------------------------------------- messages */

void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msg, s32 count) {
    mq->mtqueue = NULL;
    mq->fullqueue = NULL;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msg;
}

static s32 mesg_put(OSMesgQueue* mq, OSMesg msg, s32 flag, int jam) {
    Thr* me = flag == OS_MESG_BLOCK ? self() : NULL;
    lock();
    while (mq->validCount >= mq->msgCount) {
        if (flag == OS_MESG_NOBLOCK) {
            unlock();
            return -1;
        }
        sleep_on(me, &mq->fullqueue, XHW_INFINITE);
    }
    if (jam) {
        mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
        mq->msg[mq->first] = msg;
    } else {
        mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
    }
    mq->validCount++;
    wake_all(&mq->mtqueue);
    unlock();
    return 0;
}

s32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) { return mesg_put(mq, msg, flag, 0); }
s32 osJamMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) { return mesg_put(mq, msg, flag, 1); }

s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag) {
    Thr* me = flag == OS_MESG_BLOCK ? self() : NULL;
    lock();
    while (mq->validCount == 0) {
        if (flag == OS_MESG_NOBLOCK) {
            unlock();
            return -1;
        }
        sleep_on(me, &mq->mtqueue, XHW_INFINITE);
    }
    if (msg) *msg = mq->msg[mq->first];
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    wake_all(&mq->fullqueue);
    unlock();
    return 0;
}

/* ----------------------------------------------------------------- events */

static struct { OSMesgQueue* mq; OSMesg msg; } s_events[OS_NUM_EVENTS];

void osSetEventMesg(OSEvent e, OSMesgQueue* mq, OSMesg msg) {
    if (e < OS_NUM_EVENTS) {
        s_events[e].mq = mq;
        s_events[e].msg = msg;
    }
}
static void post_event(OSEvent e) {
    if (s_events[e].mq) osSendMesg(s_events[e].mq, s_events[e].msg, OS_MESG_NOBLOCK);
}

OSIntMask osSetIntMask(OSIntMask m) {
    (void)m;
    return OS_IM_ALL;
}

/* ------------------------------------------------------------- time/timers */

/* The N64 CPU counter runs at 46.875 MHz: 46.875 counts a microsecond. From
 * the ACPI timer (xhw_clock.c), whatever CPU the console has. */
static u64 now_count(void) { return xhw_time_us() * 46875ull / 1000ull; }
u32 osGetCount(void) { return (u32)now_count(); }
OSTime osGetTime(void) { return (OSTime)now_count(); }
void osSetTime(OSTime t) { (void)t; }   /* DIVERGES: the counter is the ACPI timer's; PW64 never sets it */

#define MAX_TIMERS 8
static struct { OSTimer* t; u64 due; } s_timers[MAX_TIMERS];
static int s_timer_kick;   /* the timer thread sleeps on its address */
static int s_timer_up;

static void timer_thread(void* arg) {
    Thr* me = self();
    (void)arg;
    lock();
    for (;;) {
        u64 now = now_count(), next = ~0ull;
        int i;
        uint32_t ms;
        for (i = 0; i < MAX_TIMERS; i++) {
            OSTimer* t = s_timers[i].t;
            if (!t) continue;
            if (s_timers[i].due <= now) {
                osSendMesg(t->mq, t->msg, OS_MESG_NOBLOCK);
                if (t->interval) s_timers[i].due += t->interval;
                else s_timers[i].t = NULL;
            }
            if (s_timers[i].t && s_timers[i].due < next) next = s_timers[i].due;
        }
        ms = next == ~0ull ? XHW_INFINITE : (uint32_t)((next - now) * 1000ull / 46875000ull) + 1;
        sleep_on(me, &s_timer_kick, ms);
    }
}

int osSetTimer(OSTimer* t, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg) {
    int i, slot = -1;
    if (!s_timer_up) {
        s_timer_up = 1;
        xhw_thread_spawn(timer_thread, NULL, 14, 32 * 1024);
    }
    t->interval = interval;
    t->value = countdown;
    t->mq = mq;
    t->msg = msg;
    lock();
    for (i = 0; i < MAX_TIMERS; i++)
        if (s_timers[i].t == NULL || s_timers[i].t == t) {
            slot = i;
            break;
        }
    if (slot < 0) pw64_fatal("osSetTimer: out of timers", MAX_TIMERS);
    s_timers[slot].t = t;
    s_timers[slot].due = now_count() + (countdown ? countdown : interval);
    wake_all(&s_timer_kick);
    unlock();
    return 0;
}

int osStopTimer(OSTimer* t) {
    int i;
    lock();
    for (i = 0; i < MAX_TIMERS; i++)
        if (s_timers[i].t == t) s_timers[i].t = NULL;
    unlock();
    return 0;
}

/* ------------------------------------------------------------ cache/addr */

/* Cart DMA is a CPU copy and the interpreter reads RAM directly: no N64
 * cache maintenance. Addresses are host pointers (os_convert.h is the
 * identity on the Xbox). */
void osWritebackDCache(void* p, s32 n) {
    (void)p;
    (void)n;
}
void osWritebackDCacheAll(void) {}
void osInvalDCache(void* p, s32 n) {
    (void)p;
    (void)n;
}
void osInvalICache(void* p, s32 n) {
    (void)p;
    (void)n;
}
u32 osVirtualToPhysical(void* p) { return (u32)(uintptr_t)p; }

/* ---------------------------------------------------------------------- VI */

static OSMesgQueue* s_vi_mq;
static OSMesg s_vi_msg;
static u32 s_vi_every = 1, s_retrace;
static void* s_vi_cur;
static void* s_vi_next;

static void vi_thread(void* arg) {
    (void)arg;
    for (;;) {
        xhw_wait_vblank();
        s_vi_cur = s_vi_next;   /* the swap latches at the retrace */
        s_retrace++;
        if (s_vi_mq && s_retrace % s_vi_every == 0) osSendMesg(s_vi_mq, s_vi_msg, OS_MESG_NOBLOCK);
        wx_pad_retrace();       /* pad.c: held-direction repeat counts retraces (settings panel) */
    }
}

void osCreateViManager(OSPri pri) {
    static int up;
    (void)pri;
    if (up) return;
    up = 1;
    xhw_thread_spawn(vi_thread, NULL, 14, 32 * 1024);
}
void osViSetEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount) {
    s_vi_mq = mq;
    s_vi_msg = msg;
    s_vi_every = retraceCount ? retraceCount : 1;
}
void osViSwapBuffer(void* fb) {
    s_vi_next = fb;
    if (!s_vi_cur) s_vi_cur = fb;
}
void* osViGetCurrentFramebuffer(void) { return s_vi_cur; }
void* osViGetNextFramebuffer(void) { return s_vi_next; }
void osViSetMode(OSViMode* m) { (void)m; }   /* the video mode is xhw_video's (D10) */
void osViBlack(u8 on) { (void)on; }
void osViSetSpecialFeatures(u32 f) { (void)f; }
void osViSetXScale(f32 s) { (void)s; }
void osViSetYScale(f32 s) { (void)s; }
u32 osViGetCurrentField(void) { return s_retrace & 1; }
u32 wx_vi_retraces(void) { return s_retrace; }

/* ---------------------------------------------------------------------- PI */

void osCreatePiManager(OSPri pri, OSMesgQueue* cmdQ, OSMesg* cmdBuf, s32 cmdMsgCnt) {
    (void)pri;
    (void)cmdQ;
    (void)cmdBuf;
    (void)cmdMsgCnt;
}

static u32 s_pi_bad;

s32 osPiStartDma(OSIoMesg* mb, s32 pri, s32 dir, u32 devAddr, void* vAddr, u32 nbytes, OSMesgQueue* mq) {
    const u8* src;
    (void)pri;
    if (dir != OS_READ) pw64_fatal("osPiStartDma: write to cart", devAddr);
    src = wx_rom_ptr(devAddr, nbytes);
    if (src) {
        memcpy(vAddr, src, nbytes);
    } else {
        memset(vAddr, 0, nbytes);
        if (s_pi_bad++ < 16)
            xhw_logf("[OS] PI DMA outside the ROM: dev %08x, %u bytes (read as zeros)", (unsigned)devAddr,
                     (unsigned)nbytes);
    }
    mb->hdr.retQueue = mq;
    mb->dramAddr = vAddr;
    mb->devAddr = devAddr;
    mb->size = nbytes;
    if (mq) osSendMesg(mq, (OSMesg)mb, OS_MESG_NOBLOCK);
    return 0;
}

s32 osEPiStartDma(OSPiHandle* h, OSIoMesg* mb, s32 dir) {
    (void)h;
    return osPiStartDma(mb, 0, dir, mb->devAddr, mb->dramAddr, mb->size, mb->hdr.retQueue);
}

/* Every caller stores the word and then reads it as bytes (_uvMediaCopy's
 * unaligned head, bootproc's debug-area probe): on the big-endian N64 that
 * byte image is the ROM's byte order, so the word is copied raw
 * (wings64-dc traps.md). */
static s32 pi_read_word(u32 devAddr, u32* data) {
    const u8* p = wx_rom_ptr(devAddr & ~3u, 4);
    if (p) memcpy(data, p, 4);
    else *data = 0;
    return 0;
}
s32 osPiReadIo(u32 devAddr, u32* data) { return pi_read_word(devAddr, data); }
s32 osPiRawReadIo(u32 devAddr, u32* data) { return pi_read_word(devAddr, data); }

/* ---------------------------------------------------------------------- SP */

static u32 s_gfx_tasks, s_aud_tasks;

void osSpTaskLoad(OSTask* task) { (void)task; }
void osSpTaskStartGo(OSTask* task) {
    if (task->t.type == M_GFXTASK) {
        s_gfx_tasks++;
        f3d_run(task->t.data_ptr);
        post_event(OS_EVENT_SP);
        post_event(OS_EVENT_DP);
    } else {
        s_aud_tasks++;
        wx_audio_task(task);   /* audio.c: the RSP audio HLE (M6); until then the list is dropped */
        post_event(OS_EVENT_SP);
    }
}
void osSpTaskYield(void) {}
OSYieldResult osSpTaskYielded(OSTask* task) {
    (void)task;
    return 0;
}
u32 wx_gfx_tasks(void) { return s_gfx_tasks; }

/* ---------------------------------------------------------------------- AI */

/* DIVERGES: a virtual AI DAC (wings64-dc's) until the AC97 output is wired
 * (M6): audio_manager.c paces its frames by the DAC's backlog, so the DAC
 * drains at the AI rate in wall-clock time and music tempo tracks real
 * time. A late audio thread finds it overdrawn: the deficit is carried for
 * up to AI_CATCHUP_MAX frames, then dropped and counted as an underrun. */
#define AI_CATCHUP_MAX 4
static u32 s_ai_rate = 22050;
static s32 s_ai_pending;     /* samples queued and not yet "played" */
static s32 s_ai_queued;      /* the last queued buffer (the AI's "next") */
static u64 s_ai_last_us, s_ai_frac;
static u32 s_ai_underruns;
static u64 s_ai_samples;

static void ai_drain(void) {
    u64 now = xhw_time_us(), n;
    if (!s_ai_last_us) s_ai_last_us = now;
    s_ai_frac += (now - s_ai_last_us) * s_ai_rate;
    s_ai_last_us = now;
    n = s_ai_frac / 1000000ull;
    s_ai_frac -= n * 1000000ull;
    s_ai_pending -= n > 0x10000000ull ? 0x10000000 : (s32)n;
}

s32 osAiSetFrequency(u32 f) {
    s_ai_rate = f;
    wx_audio_rate(f);
    return (s32)f;
}
s32 osAiSetNextBuffer(void* buf, u32 size) {
    ai_drain();
    if (s_ai_pending < -AI_CATCHUP_MAX * 368) {
        if (s_ai_samples) s_ai_underruns++;
        s_ai_pending = 0;
    }
    s_ai_queued = (s32)(size >> 2);
    s_ai_pending += s_ai_queued;
    s_ai_samples += size >> 2;
    wx_audio_buffer(buf, size);   /* audio.c: to the AC97 pump (M6) */
    return 0;
}
u32 osAiGetLength(void) {
    ai_drain();
    /* the N64 AI: the buffer playing now, not the queued next one (wings64-dc traps.md) */
    if (s_ai_pending <= 0) return 0;
    return (u32)(s_ai_pending > s_ai_queued ? s_ai_pending - s_ai_queued : s_ai_pending) << 2;
}
u32 osAiGetStatus(void) { return 0; }
u32 wx_ai_underruns(void) { return s_ai_underruns; }

/* ---------------------------------------------------------------------- SI */

static OSContPad s_pads[MAXCONTROLLERS];

s32 osContInit(OSMesgQueue* mq, u8* bitpattern, OSContStatus* status) {
    u8 bits = 0;
    int i;
    (void)mq;
    wx_pad_read(s_pads);
    for (i = 0; i < MAXCONTROLLERS; i++) {
        int present = i == 0 || s_pads[i].errno == 0;   /* port 1 always reports (autopad, hot-plug) */
        status[i].type = CONT_TYPE_NORMAL;
        status[i].status = 0;
        status[i].errno = present ? 0 : CONT_NO_RESPONSE_ERROR;
        if (present) bits |= 1 << i;
    }
    *bitpattern = bits;
    return 0;
}
s32 osContStartReadData(OSMesgQueue* mq) {
    (void)mq;
    wx_pad_read(s_pads);
    post_event(OS_EVENT_SI);
    return 0;
}
void osContGetReadData(OSContPad* pads) { memcpy(pads, s_pads, sizeof s_pads); }
s32 osContStartQuery(OSMesgQueue* mq) {
    (void)mq;
    post_event(OS_EVENT_SI);
    return 0;
}
void osContGetQuery(OSContStatus* status) {
    int i;
    for (i = 0; i < MAXCONTROLLERS; i++) {
        status[i].type = CONT_TYPE_NORMAL;
        status[i].status = 0;
        status[i].errno = i == 0 || s_pads[i].errno == 0 ? 0 : CONT_NO_RESPONSE_ERROR;
    }
}

/* ------------------------------------------------------------------ EEPROM */

/* The 16 Kbit image as E:\UDATA\57580001\pilotwings64.eep, in the byte
 * order emulators use (the bytes as the cartridge holds them), so a
 * Project64/Mupen save moves both ways (eeprom.c). */
s32 osEepromProbe(OSMesgQueue* mq) {
    (void)mq;
    wx_eeprom_load();
    return EEPROM_TYPE_16K;
}
s32 osEepromLongRead(OSMesgQueue* mq, u8 address, u8* buffer, int nbytes) {
    (void)mq;
    return wx_eeprom_read(address * 8u, buffer, (u32)nbytes) ? 0 : -1;
}
s32 osEepromLongWrite(OSMesgQueue* mq, u8 address, u8* buffer, int nbytes) {
    (void)mq;
    return wx_eeprom_write(address * 8u, buffer, (u32)nbytes) ? 0 : -1;
}
s32 osEepromRead(OSMesgQueue* mq, u8 address, u8* buffer) { return osEepromLongRead(mq, address, buffer, 8); }
s32 osEepromWrite(OSMesgQueue* mq, u8 address, u8* buffer) { return osEepromLongWrite(mq, address, buffer, 8); }

/* -------------------------------------------------------------------- init */

void osInitialize(void) { self(); }
