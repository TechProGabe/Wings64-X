/* layout_checks.c - compile-time checks of the struct layouts that raw ROM
 * copies, PW64_SWAP layouts and display-list strides depend on, evaluated
 * under the game triple (i686-pc-windows-gnu -mno-ms-bitfields: PLAN.md H1).
 * No code: a wrong layout fails the build. From wings64-dc's
 * src/dc/rom/layout_checks.c (based on Birdman64's pw64_layout_checks.c, MIT),
 * extended with every PW64_SWAP layout the patches use on a type a header
 * declares.
 *
 * PW64_SWAP checks its layout against sizeof(T) at run time too
 * (pw64_swap.c); the types declared inside a single .c file (demo.c RHDR and
 * RPKT, demo_attitude.c PPOS, spath.c PathTimeVal, texture.c UVBlockHeader)
 * are covered only by that run-time check. */
#include <ultra64.h>
#include <libc/stddef.h>
#include <PR/libaudio.h>
#include <PR/sp.h>
#include <uv_filesystem.h>
#include <uv_graphics.h>
#include <uv_memory.h>
#include <pw64_endian.h>
#include "app/level.h"
#include "app/snap.h"
#include "app/task.h"

/* ---- the ABI the patches were written for ---- */
_Static_assert(sizeof(void*) == 4 && sizeof(long) == 4 && sizeof(int) == 4, "ILP32 like the VR4300");
_Static_assert(sizeof(long long) == 8 && _Alignof(long long) == 8, "u64 8-aligned as on the VR4300 (-mno-ms-bitfields)");
_Static_assert(sizeof(double) == 8 && _Alignof(double) == 8, "f64 8-aligned as on the VR4300");
_Static_assert((unsigned char)-1 == 255 && (char)-1 > 0, "IDO chars are unsigned (-funsigned-char)");

/* ---- graphics ---- */
_Static_assert(sizeof(Gfx) == 8, "Gfx");
_Static_assert(sizeof(Vtx) == 16, "Vtx");
_Static_assert(sizeof(Mtx) == 64, "Mtx");
_Static_assert(sizeof(Mtx4F) == 64, "Mtx4F");
_Static_assert(_Alignof(Mtx) == 8, "Mtx keeps aligned(8) (the decomp's __attribute__ must not be empty: macros.h)");
_Static_assert(sizeof(Bitmap) == PW64_N64_BITMAP_SIZE, "Bitmap is the N64 16-byte record on ILP32 (patches/016)");

/* ---- bit-fields (patches/001-003) ---- */
_Static_assert(sizeof(UnkCommStruct) == 0x18, "UnkCommStruct (uv_filesystem.h bit-fields, patches/001)");
_Static_assert(sizeof(UnkPartStruct_Unk8) == 0x14, "UnkPartStruct_Unk8");
/* GCC (and clang with -mno-ms-bitfields) packs this like IDO (0x18) but
 * LSB-first; patches/003 serialises the IDO byte image. With MS bit-fields it
 * would not be 0x18. */
_Static_assert(sizeof(Unk8033F050) == 0x18, "Unk8033F050 photo record (patches/003)");

/* ---- patches/020: camera arrays sized to their real extent must not move the fields after them ---- */
_Static_assert(offsetof(UnkStruct_80204D94, unk250) == 0x250, "camera unk250");
_Static_assert(offsetof(UnkStruct_80204D94, unk298) == 0x298, "camera unk298");
_Static_assert(offsetof(UnkStruct_80204D94, unk2E0) == 0x2E0, "camera unk2E0");
_Static_assert(offsetof(UnkStruct_80204D94, unk328) == 0x328, "camera unk328");
_Static_assert(offsetof(UnkStruct_80204D94, unk370) == 0x370, "camera unk370 (patches/020)");

/* ---- every PW64_SWAP layout on a header-declared type (patches/013, 015) ----
 * The size is the layout's total (b 1, h 2, w 4, d 8). */
#define SWAP_LAYOUT(T, size, layout) _Static_assert(sizeof(T) == (size), #T " is " #size " bytes: PW64_SWAP layout " layout)
SWAP_LAYOUT(LevelAPTS, 0x20, "8w");
SWAP_LAYOUT(LevelBNUS, 0x1C, "7w");
SWAP_LAYOUT(LevelESND, 0x78, "22w 4b 5w 4b w");
SWAP_LAYOUT(LevelLPAD, 0x18, "5w 4b");
SWAP_LAYOUT(LevelTOYS, 0x10, "3w 4b");
SWAP_LAYOUT(LevelTPTS, 0x34, "4b 12w");
SWAP_LAYOUT(LevelWOBJ, 0x10, "3w 4b");
SWAP_LAYOUT(TaskBALS, 0x68, "3w 12b 2w 4b 8w 4b 8w");
SWAP_LAYOUT(TaskBTGT, 0x1C, "3w 3w 2b 2b");
SWAP_LAYOUT(TaskCNTG, 0x1C, "6w 4b");
SWAP_LAYOUT(TaskFALC, 0xAC, "4w 8b 37w");
SWAP_LAYOUT(TaskHOPD, 0x20, "7w 4b");
SWAP_LAYOUT(TaskHPAD, 0x40, "6w w 4b w 4b 5w 4b");
SWAP_LAYOUT(TaskLPAD, 0x30, "6w 4b 3w 4b 4b");
SWAP_LAYOUT(TaskLSTP, 0x24, "6w 4b 4b w");
SWAP_LAYOUT(TaskLWIN, 0x54, "20w 4b");
SWAP_LAYOUT(TaskOBSV, 0x10, "4w");
SWAP_LAYOUT(TaskObjUnk2C, 0x18, "6w");
SWAP_LAYOUT(TaskObjects_Unk10, 0x1C, "7w");
SWAP_LAYOUT(TaskPHTS, 0x14, "5w");
SWAP_LAYOUT(TaskTARG, 0x20, "6w 4b w");
SWAP_LAYOUT(TaskTHER, 0x28, "10w");
SWAP_LAYOUT(TaskTPAD, 0x30, "6w 4b 3w 4b w");
SWAP_LAYOUT(TaskRNGS, 0x84, "7w 4b 5w 4b 5w 4b w 4b 2w 4b 3w 4b 16b");
SWAP_LAYOUT(UVBlockCounts, 0x24, "w 16h");
SWAP_LAYOUT(Unk80225FBC_0x28_UnkC, 0x8, "4h");
SWAP_LAYOUT(Unk803599D0, 0x54, "21w");
SWAP_LAYOUT(UnkCommStruct, 0x18, "5w h 2b");
SWAP_LAYOUT(uvUnkTeraStruct, 0x18, "6w");
SWAP_LAYOUT(UnkPartStruct_Unk8, 0x14, "4w 2h");
SWAP_LAYOUT(UnkQuatStruct_Unk8, 0x18, "6w");
SWAP_LAYOUT(UnkTranslatStruct_Unk8, 0x10, "4w");
SWAP_LAYOUT(ALCMidiHdr, 0x44, "17w");
SWAP_LAYOUT(Vtx, 0x10, "6h 4b");
SWAP_LAYOUT(Mtx, 0x40, "32h");
SWAP_LAYOUT(Mtx4F, 0x40, "16w");
SWAP_LAYOUT(Gfx, 0x8, "2w");
/* texture.c swaps the first offsetof(UnkUVMD_24, unk20) bytes as "4b 6w h 2b" */
_Static_assert(offsetof(UnkUVMD_24, unk20) == 0x20, "UnkUVMD_24 head: PW64_SWAP layout 4b 6w h 2b");

/* ---- libaudio: the ROM loaders build these host structs (patches/015) ---- */
_Static_assert(sizeof(ALEnvelope) == 16, "ALEnvelope");
_Static_assert(sizeof(ALKeyMap) == 6, "ALKeyMap");
_Static_assert(offsetof(ALInstrument, soundArray) == 16, "ALInstrument header");
_Static_assert(offsetof(ALBank, instArray) == 12, "ALBank header");
