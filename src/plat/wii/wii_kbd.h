/* SI + IOS keyboard helpers used by plat_wii.c. See wii_kbd.c for details. */
#ifndef WII_KBD_H
#define WII_KBD_H

/* Homebrew Channel layout: cwd, RELIC.CFG, sessions, RNG seed, Lua world. */
#define WII_APP_DIR "sd:/apps/relic"

void wii_kbd_init(void);
int wii_kbd_get(void);
int wii_kbd_take_esc(void);
int wii_kbd_present(void);
#endif
