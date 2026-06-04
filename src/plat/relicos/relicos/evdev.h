#pragma once

enum {
    EVK_NONE  = 0,
    EVK_ENTER = -1,
    EVK_BKSP  = -2,
    EVK_ESC   = -3,
    EVK_TAB   = -4,
};

/* Returns fd, or -1. Scans /dev/input/event* for a device with KEY capability. */
int  ev_open_keyboard(void);
/* Blocking read of one logical keystroke. Returns printable char (>0),
 * one of EVK_* (<0), or 0 if the event was uninteresting (caller should loop). */
int  ev_read_key(int fd);
/* Non-blocking: returns next queued logical key or 0 if none pending. */
int  ev_poll_key(int fd);
/* Block up to timeout_ms for the next logical key; 0 on timeout. */
int  ev_wait_key(int fd, int timeout_ms);
/* Exclusive grab on/off. on=1: only this fd sees events; on=0: shared. */
void ev_grab(int fd, int on);
void ev_close(int fd);
