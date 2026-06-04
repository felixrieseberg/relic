/* USB-HID keyboard helpers used by plat_xbox.c. See xbox_kbd.c for details. */
#ifndef XBOX_KBD_H
#define XBOX_KBD_H
void xbox_kbd_init(void);
int xbox_kbd_poll(void);
int xbox_kbd_get(void);
void xbox_kbd_unget(int c);
int xbox_kbd_take_esc(void);
int xbox_kbd_present(void);
#endif
