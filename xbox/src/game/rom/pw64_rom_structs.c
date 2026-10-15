/* pw64_rom_structs.c - ROM-layout loaders for kernel/app structs with pointer
 * fields, whose host layout differs from the N64 one so PW64_SWAP cannot be
 * used in place (patches/016). Each reads the big-endian N64 layout from RAM
 * (a scratch copy of the file block) and fills host structs.
 *
 * Ported from Birdman64 (https://github.com/NoahVl/Birdman64, MIT, (c) 2026
 * Birdman64 contributors) crates/pw64-game/native/src by wings64-dc
 * (src/dc/rom/pw64_rom_structs.c); unchanged for the Xbox (ILP32, so the host
 * Bitmap is the N64's 16 bytes: layout_checks.c). */
#include <ultra64.h>
#include <PR/sp.h>
#include <pw64_endian.h>

static u16 be16(const u8* p) {
    return (u16)((p[0] << 8) | p[1]);
}

static u32 be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/* N64 Bitmap (sp.h): s16 width, width_img, s, t; u32 buf; s16 actualHeight,
 * LUToffset = 16 bytes. `buf` keeps the raw 32-bit value (font.c stores an
 * IMAG index there and patches it into a pointer afterwards). */
void pw64_load_bitmaps(Bitmap* dst, const void* src, s32 count) {
    const u8* p = src;
    s32 i;

    for (i = 0; i < count; i++, p += PW64_N64_BITMAP_SIZE) {
        dst[i].width = (s16)be16(p + 0);
        dst[i].width_img = (s16)be16(p + 2);
        dst[i].s = (s16)be16(p + 4);
        dst[i].t = (s16)be16(p + 6);
        dst[i].buf = PW64_PTR(be32(p + 8));
        dst[i].actualHeight = (s16)be16(p + 12);
        dst[i].LUToffset = (s16)be16(p + 14);
    }
}
