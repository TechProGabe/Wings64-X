/* stubs.c - every libultra OS/IO entry point the game calls that the port
 * does not implement yet (M2: the decomp links; nothing runs it). The list is
 * the linker's: these are exactly the symbols lld-link reported undefined
 * once the decomp, xbox/src/game and the platform layer were linked together
 * (docs/handoff.md, M2). Each stops the game with "TODO: <name>" through
 * pw64_fatal, so a call that arrives before its implementation is a clear
 * card and a WINGS64_FATAL log line, never a silent wrong answer.
 *
 * M3 replaces them with the OS shim (xbox/src/game/ultra/os_shim.c, PLAN.md
 * D5: threads, message queues, events, timers, PI, VI, SP dispatch, AI, SI,
 * EEPROM); move each out of here as it gets written. Signatures are the
 * decomp's own (include/libultra/PR). */
#include <ultra64.h>
#include <PR/os_internal.h>

#define TODO(name) pw64_fatal("TODO: " #name, 0)

/* ---- threads (os_thread.h) ---- */
void osCreateThread(OSThread* t, OSId id, void (*entry)(void*), void* arg, void* sp, OSPri pri) {
    TODO(osCreateThread);
}
void osStartThread(OSThread* t) {
    TODO(osStartThread);
}
void osSetThreadPri(OSThread* t, OSPri pri) {
    TODO(osSetThreadPri);
}
void osYieldThread(void) {
    TODO(osYieldThread);
}
OSThread* __osGetCurrFaultedThread(void) {
    TODO(__osGetCurrFaultedThread);
}

/* ---- messages and events (os_message.h) ---- */
void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msg, s32 count) {
    TODO(osCreateMesgQueue);
}
s32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    TODO(osSendMesg);
}
s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag) {
    TODO(osRecvMesg);
}
void osSetEventMesg(OSEvent e, OSMesgQueue* mq, OSMesg msg) {
    TODO(osSetEventMesg);
}

/* ---- system, interrupts, time (os_system.h, os_exception.h, os_reg.h, os_time.h) ---- */
#undef osInitialize
void osInitialize(void) {
    TODO(osInitialize);
}
OSIntMask osSetIntMask(OSIntMask mask) {
    TODO(osSetIntMask);
}
u32 osGetCount(void) {
    TODO(osGetCount);
}
int osSetTimer(OSTimer* t, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg) {
    TODO(osSetTimer);
}

/* ---- cache and address translation (os_cache.h, os_convert.h) ---- */
void osInvalDCache(void* p, s32 n) {
    TODO(osInvalDCache);
}
void osWritebackDCache(void* p, s32 n) {
    TODO(osWritebackDCache);
}
void osWritebackDCacheAll(void) {
    TODO(osWritebackDCacheAll);
}
/* Not a stub: a host pointer is its own physical address (patches/029). */
u32 osVirtualToPhysical(void* p) {
    return (u32)p;
}

/* ---- PI: the cartridge (os_pi.h) ---- */
void osCreatePiManager(OSPri pri, OSMesgQueue* cmdQ, OSMesg* cmdBuf, s32 cmdMsgCnt) {
    TODO(osCreatePiManager);
}
s32 osPiStartDma(OSIoMesg* mb, s32 priority, s32 direction, u32 devAddr, void* vAddr, u32 nbytes, OSMesgQueue* mq) {
    TODO(osPiStartDma);
}
s32 osPiReadIo(u32 devAddr, u32* data) {
    TODO(osPiReadIo);
}
s32 osPiRawReadIo(u32 devAddr, u32* data);
s32 osPiRawReadIo(u32 devAddr, u32* data) {
    TODO(osPiRawReadIo);
}

/* ---- VI: video (os_vi.h) ---- */
void osCreateViManager(OSPri pri) {
    TODO(osCreateViManager);
}
void osViSetMode(OSViMode* mode) {
    TODO(osViSetMode);
}
void osViSetEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount) {
    TODO(osViSetEvent);
}
void osViSetSpecialFeatures(u32 func) {
    TODO(osViSetSpecialFeatures);
}
void osViBlack(u8 active) {
    TODO(osViBlack);
}
void osViSwapBuffer(void* frameBufPtr) {
    TODO(osViSwapBuffer);
}
void* osViGetCurrentFramebuffer(void) {
    TODO(osViGetCurrentFramebuffer);
}
void* osViGetNextFramebuffer(void) {
    TODO(osViGetNextFramebuffer);
}

/* ---- SP: RSP tasks (sptask.h) ---- */
void osSpTaskLoad(OSTask* tp) {
    TODO(osSpTaskLoad);
}
void osSpTaskStartGo(OSTask* tp) {
    TODO(osSpTaskStartGo);
}
void osSpTaskYield(void) {
    TODO(osSpTaskYield);
}
OSYieldResult osSpTaskYielded(OSTask* tp) {
    TODO(osSpTaskYielded);
}

/* ---- AI: audio DAC (os_ai.h) ---- */
u32 osAiGetLength(void) {
    TODO(osAiGetLength);
}
s32 osAiSetFrequency(u32 frequency) {
    TODO(osAiSetFrequency);
}
s32 osAiSetNextBuffer(void* bufPtr, u32 size) {
    TODO(osAiSetNextBuffer);
}

/* ---- SI: controllers and EEPROM (os_cont.h, os_eeprom.h) ---- */
s32 osContInit(OSMesgQueue* mq, u8* bitpattern, OSContStatus* status) {
    TODO(osContInit);
}
s32 osContStartReadData(OSMesgQueue* mq) {
    TODO(osContStartReadData);
}
void osContGetReadData(OSContPad* pad) {
    TODO(osContGetReadData);
}
s32 osEepromProbe(OSMesgQueue* mq) {
    TODO(osEepromProbe);
}
s32 osEepromLongRead(OSMesgQueue* mq, u8 address, u8* buffer, int nbytes) {
    TODO(osEepromLongRead);
}
s32 osEepromLongWrite(OSMesgQueue* mq, u8 address, u8* buffer, int nbytes) {
    TODO(osEepromLongWrite);
}
