/* wx_boot.c - the game side's entry: wx_boot_game hands the CPU to the
 * decomp's bootproc (kernel/system.c), which creates the kernel thread and
 * the rest of libultra's world.
 *
 * M2: linked, not called. Nothing calls wx_boot_game yet (the platform's
 * weak wx_boot in xhw_main.c still ends on its card); the link keeps it with
 * -include:_wx_boot_game (xbox/CMakeLists.txt) and it references bootproc,
 * so the whole decomp is resolved by the link (every undefined symbol is an
 * error) as wings64-dc's main.c does with its bootproc pointer. M3 calls it
 * once the ROM is verified and the OS shim exists. */
#include <ultra64.h>

void bootproc(void* arg);
void wings64_rdram_init(void);

/* volatile: the reference must survive optimisation and -opt:ref */
void (*volatile wx_game_entry)(void*) = bootproc;

void wx_boot_game(void) {
    wings64_rdram_init();
    wx_game_entry(NULL);
}
