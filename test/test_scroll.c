#include "t.h"
#include "../src/core/scroll.h"

/* plat stubs (scroll_view isn't exercised here) */
int plat_getkey(void) { return 0; }
int plat_con_raw(int on)
{
    (void)on;
    return 0;
}
int plat_con_size(int *r, int *c)
{
    (void)r;
    (void)c;
    return 0;
}
int plat_con_clear(void) { return 0; }

/* swallow stdout from scroll_out so test output stays clean */
static void mute(const char *s, int n)
{
    (void)s;
    (void)n;
}

int main(void)
{
    t_group("scroll");

    scroll_set_sink(mute);

    /* basic line splitting */
    scroll_out("one\ntwo\nthree\n", 14);
    t_int(scroll_lines(), 3, "out: \\n splits lines");

    /* partial line counts as one until newline'd */
    scroll_out("abc", 3);
    t_int(scroll_lines(), 4, "out: partial line counts");
    scroll_out("def\n", 4);
    t_int(scroll_lines(), 4, "out: completing partial doesn't add");

    /* hard-wrap at 80 cols -> one 90-char line becomes 2 */
    {
        char buf[92];
        memset(buf, 'x', 90);
        buf[90] = '\n';
        scroll_out(buf, 91);
        t_int(scroll_lines(), 6, "out: 90 cols hard-wraps to 2");
    }

    /* fill past capacity -> capped */
    {
        int i, cap = scroll_capacity();
        for (i = 0; i < cap + 100; i++)
            scroll_out("z\n", 2);
        t_int(scroll_lines(), cap, "out: capped at capacity");
    }

    /* scroll_view returns 0 with no console */
    t_int(scroll_view(), 0, "view: no-op without console");

    /* scroll_init resizes (and resets) the ring */
    scroll_init(64);
    t_int(scroll_capacity(), 64, "init: capacity set");
    t_int(scroll_lines(), 0, "init: ring reset");
    {
        int i;
        for (i = 0; i < 100; i++)
            scroll_out("y\n", 2);
        t_int(scroll_lines(), 64, "init: new cap enforced");
    }
    scroll_init(0);
    t_int(scroll_capacity(), 16, "init: 0 clamps to minimum");

    /* scroll_printf goes through the same path */
    scroll_init(64);
    scroll_printf("a%d\nb\n", 1);
    t_int(scroll_lines(), 2, "printf: formats + splits");

    return t_done();
}
