/* SPDX-License-Identifier: MPL-2.0 */
/* LUOGU (锣鼓): the 锣鼓经 of Peking opera and the 京胡's 过门, to learn the percussion by and to sing to.
 * This header is the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c).
 * (Started from fm1-base's template, by Charles Vestal.) */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "LUOGU"
#define APP_SPLASH_RGB RGB(214, 48, 40)          /* the red of a 戏台's pillars */
#define APP_BLOCK 256u                           /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                                /* velocity from the keys (lib/touch.h): strokes soft and hard */
#define APP_BIG_FONT FONT_B                      /* the panel's own screens (its calibration) in 16 px, not 32: LUOGU
                                                  * has its own splash, and the 20 KB is the 彩蛋's */

/* the store: the settings, and the panel's calibration (the platform's). An object's number is in its record: keep
 * them (a new object goes last) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved settings (or defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
#define APP_SPLASH
void app_splash(void);                   /* the logo, while the platform starts (app.c; tools/logo.py) */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */
#define APP_LOOP
void app_loop(void);                     /* every pass of the main loop: the practice's judging */

/* the host simulator's checks (host/plat_host.c "expect"), and its reboot (what the app's RAM forgets) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_REBOOT
void app_host_reboot(void);
