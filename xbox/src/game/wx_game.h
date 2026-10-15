/* wx_game.h - the game side's own glue (xbox/src/game), used by the libultra
 * shim and the hooks the patches call. Game triple; plain C types only. */
#ifndef WX_GAME_H
#define WX_GAME_H
#include <stdint.h>

/* boot.c: the verified ROM image (z64 byte order) held in RAM (PLAN.md D2) */
const uint8_t* wx_rom_ptr(uint32_t dev_addr, uint32_t nbytes);   /* NULL when outside the ROM */

/* pad.c: the four ports as N64 pads (PLAN.md D8). `pads` is an OSContPad[4]. */
struct OSContPad;
void wx_pad_read(void* pads);
void wx_pad_retrace(void);
int wx_pad_stick_raw_pct(void);   /* the raw left stick's tilt, 0..100 (the settings panel's readout) */
uint32_t wx_pad_retraces(void);   /* retraces counted by the VI thread (held-direction repeat) */
int wx_pad_back_pressed(void);
void wx_pad_quiesce(void);
void wx_pad_rumble(int strength, uint32_t ms);   /* player 1, at the next controller read (setting permitting) */       /* before leaving the XBE: the controllers are taken for good (no VI polling) */    /* BACK newly pressed on port 1, still down, not seen before */

/* eeprom.c: the 16 Kbit EEPROM as E:\UDATA\57580001\pilotwings64.eep */
void wx_eeprom_load(void);
int wx_eeprom_read(uint32_t offset, uint8_t* buf, uint32_t n);
int wx_eeprom_write(uint32_t offset, const uint8_t* buf, uint32_t n);

/* audio.c: the RSP audio task and the AI's buffers (M6) */
struct OSTask;
void wx_audio_task(void* task);
void wx_audio_rate(uint32_t hz);
void wx_audio_buffer(void* buf, uint32_t bytes);

/* settings.c: settings.ini (PLAN.md 3.4) */
typedef struct {
    int output;          /* XHW_OUT_* */
    int progressive;
    int widescreen;      /* 0 off, 1 on, 2 auto */
    int fps;
    int screenshots;
    int stick_deadzone;  /* percent, 0..60 */
    int tex_filter;      /* 0 N64 (the game's per-texture choice), 1 sharp (point sampling) */
    int rumble;          /* 1: the controller rumbles on a crash */
    char video_confirmed[8];
} WxSettings;
extern WxSettings g_wx_settings;
void wx_settings_load(void);
int wx_settings_save(void);
/* The FPS counter: the renderer's 5x7 one (nv2a.c, Melee-X's), or with
 * -DWX_FPS_GAME_FONT=1 wings64-dc's game-font one (settings_menu.c). Called
 * at boot after xgfx_init and after a change of g_wx_settings.fps. */
void wx_settings_apply_fps(void);
/* Widescreen (wings64_widescreen.h has wx_settings_apply_widescreen): 1 when
 * the setting (Auto resolved for this video mode) is 16:9 */
int wx_settings_widescreen_on(void);
int wx_settings_fps_game_font(void);   /* 1: settings_menu.c draws the counter */

#endif
