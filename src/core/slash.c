#include <string.h>
#include "slash.h"
#include "scroll.h"

void slash_help(const slash_cmd *cmds, int ncmds)
{
    int i;
    for (i = 0; i < ncmds; i++)
        if (cmds[i].help[0])
            scroll_printf("  /%-8s %-7s  %s\n", cmds[i].name, cmds[i].args,
                          cmds[i].help);
}

int slash_dispatch(const slash_cmd *cmds, int ncmds, const char *line)
{
    int i, n;
    const char *arg;
    if (line[0] != '/') return -1;
    if (line[1] == 0) {
        slash_help(cmds, ncmds);
        return 0;
    }
    line++;
    for (n = 0; line[n] && line[n] != ' '; n++) {}
    arg = line[n] ? line + n + 1 : "";
    while (*arg == ' ')
        arg++;
    for (i = 0; i < ncmds; i++)
        if ((int)strlen(cmds[i].name) == n
            && strncmp(line, cmds[i].name, (size_t)n) == 0)
            return cmds[i].fn(arg);
    scroll_printf("unknown command: /%.*s\n", n, line);
    slash_help(cmds, ncmds);
    return 0;
}
