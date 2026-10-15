/* ultra_data.c - libultra data symbols the game references (compiled with the
 * game flags). From wings64-dc's src/dc/ultra/ultra_data.c; values per
 * Birdman64's stubs.rs and the N64 boot state. The VI mode table is filled
 * by the VI shim in M3 (PLAN.md D5). */
#include <ultra64.h>

u32 osMemSize = 0x400000;      /* 4 MB: PW64 runs without the Expansion Pak */
s32 osTvType = OS_TV_NTSC;     /* the US release */
OSViMode osViModeTable[56];    /* M3: only indexed for osViSetMode */

/* RSP microcode images: the game only takes their addresses and sizes for
 * task headers, which the Xbox never sends to an RSP (display lists are
 * interpreted on the CPU, audio command lists by the audio HLE). */
long long int rspbootTextStart[1], rspbootTextEnd[1];
long long int gspFast3DTextStart[1], gspFast3DTextEnd[1];
long long int gspFast3DDataStart[1], gspFast3DDataEnd[1];
long long int gspF3DEX_fifoTextStart[1], gspF3DEX_fifoDataStart[1];   /* referenced on a path PW64 never takes (seen at -O0) */
long long int aspMainTextStart[1], aspMainTextEnd[1];
long long int aspMainDataStart[1], aspMainDataEnd[1];
