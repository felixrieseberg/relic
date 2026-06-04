#pragma once
#include <functional>
#include <string>

struct agent_cb {
    std::function<void(const char *msg)>  status;     /* progress line */
    std::function<void(const char *text)> say;        /* assistant plain text */
    std::function<void(const char *name, const char *summary)> tool; /* per tool call */
};

/* 0 ok, <0 error (status() will have been called with the message). */
int  agent_init(const char *conv_path, int fb_w, int fb_h, int canvas_h);
int  agent_turn(const char *user_text, const agent_cb &cb);
void agent_reset(void);
/* set_to=nullptr -> getter only. Returns current model id. */
const char *agent_model(const char *set_to);
/* Render the sub-agent registry into out; returns bytes written. */
int agent_list(char *out, int cap);
/* Render the canvas window registry into out; returns bytes written. */
int agent_windows(char *out, int cap);
void agent_windows_clear(void);

