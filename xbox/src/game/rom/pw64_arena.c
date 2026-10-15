/* pw64_arena.c - bump arena for deserialised ROM structs (the audio banks,
 * patches/015). From wings64-dc's src/dc/rom/pw64_arena.c.
 *
 * DIVERGES: Birdman64 carves this out of the unused N64 code window
 * 0x802000A0..0x80380000; wings64-dc and this port use a zeroed static pool in
 * .bss. Size: the deserialised music + SFX banks are ~2x their ctl files
 * (16 KB + 26 KB); M6 measures it (pw64_arena_used). */
#include <ultra64.h>
#include <pw64_endian.h>

#define ARENA_SIZE (192 * 1024)
static u8 sArena[ARENA_SIZE] __attribute__((aligned(32)));
static u32 sArenaCur;

void* pw64_arena_alloc(u32 size, u32 align) {
    u32 p = (sArenaCur + (align - 1)) & ~(align - 1);
    if (p + size > ARENA_SIZE || p + size < p) {
        pw64_fatal("pw64_arena_alloc: arena full", size);
    }
    sArenaCur = p + size;
    /* .bss is zeroed at boot and nothing is ever freed, so the block is already zero. */
    return &sArena[p];
}

u32 pw64_arena_used(void) {
    return sArenaCur;
}
