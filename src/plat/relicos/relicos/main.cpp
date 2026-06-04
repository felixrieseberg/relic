#include <cstring>
#include <cstdio>
#include <unistd.h>

int run_ui(const char *fbdev);
int run_once(const char *fbdev, const char *prompt);
int run_agent(const char *prompt);
int pid1_main(void);

int main(int argc, char **argv) {
    /* When the kernel starts us as /init, we own the boot sequence. With
     * argv flags, we were re-exec'd as a subprocess (sub-agent etc). */
    if (getpid() == 1 && argc <= 1) return pid1_main();
    const char *fbdev = "/dev/fb0";
    const char *once  = nullptr;
    const char *agent = nullptr;
    int ui = 0;
    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--ui"))                    ui = 1;
        else if (!strcmp(argv[i], "--once")  && i + 1 < argc) once  = argv[++i];
        else if (!strcmp(argv[i], "--agent") && i + 1 < argc) agent = argv[++i];
        else if (!strcmp(argv[i], "--fb")    && i + 1 < argc) fbdev = argv[++i];
    }
    if (agent) return run_agent(agent);
    if (once)  return run_once(fbdev, once);
    if (ui)    return run_ui(fbdev);
    fprintf(stderr,
            "usage: relicos --ui | --once PROMPT | --agent PROMPT [--fb DEV]\n");
    return 1;
}
