/* init.cpp -- PID 1. relicos *is* init: it owns the boot sequence (mounts,
 * modules, /data, net, API key) and the kernel-cmdline dispatch that used to
 * live in a busybox /init script. busybox stays in the image as a library we
 * shell out to for the fiddly bits (modprobe, udhcpc, mke2fs); the control
 * flow and process 1 are ours. */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

int run_ui(const char *fbdev);
int run_once(const char *fbdev, const char *prompt);

/* ---- helpers ----------------------------------------------------------- */
static void off(void) { sync(); reboot(RB_POWER_OFF); _exit(0); }

static int slurp(const char *path, char *buf, int cap) {
    int fd = open(path, O_RDONLY); if (fd < 0) return -1;
    int n = (int)read(fd, buf, cap - 1); close(fd);
    if (n < 0) n = 0; buf[n] = 0;
    while (n && (buf[n-1] == '\n' || buf[n-1] == ' ')) buf[--n] = 0;
    return n;
}

static int spew(const char *path, const char *s) {
    int fd = open(path, O_WRONLY); if (fd < 0) return -1;
    write(fd, s, strlen(s)); close(fd); return 0;
}

static int sh(const char *cmd) {                 /* busybox is the libc here */
    int rc = system(cmd);
    return (rc == -1) ? -1 : WEXITSTATUS(rc);
}

static int has(const char *hay, const char *needle) {
    return strstr(hay, needle) != 0;
}

/* "relicos.foo=BAR" -> copy BAR into out; 1 if found. */
static int kv(const char *cmdline, const char *key, char *out, int cap) {
    const char *p = cmdline;
    size_t kl = strlen(key);
    while ((p = strstr(p, key)) && p != cmdline && p[-1] != ' ') p += kl;
    if (!p) return 0;
    p += kl;
    int i = 0;
    while (*p && *p != ' ' && i < cap - 1) out[i++] = *p++;
    out[i] = 0;
    return 1;
}

static void mnt(const char *src, const char *tgt, const char *fs) {
    mkdir(tgt, 0755);
    if (mount(src, tgt, fs, 0, 0) != 0 && errno != EBUSY)
        fprintf(stderr, "mount %s -> %s (%s): %m\n", src, tgt, fs);
}

/* ---- boot -------------------------------------------------------------- */
static void boot_mounts(void) {
    static const char *dirs[] = {
        "/sbin","/usr/bin","/usr/sbin","/tmp","/root","/data"};
    for (const char *d : dirs) mkdir(d, 0755);
    mnt("proc",  "/proc", "proc");
    mnt("sysfs", "/sys",  "sysfs");
    mnt("dev",   "/dev",  "devtmpfs");
    mkdir("/dev/pts", 0755);
    mount("devpts", "/dev/pts", "devpts", 0, 0);
    /* system() needs /bin/sh; bootstrap it before --install fills the rest. */
    symlink("/bin/busybox", "/bin/sh");
    sh("/bin/busybox --install -s");
    setenv("PATH", "/data/bin:/usr/bin:/bin:/sbin:/usr/sbin", 1);
}

static void boot_modules(void) {
    sh("modprobe -qa virtio_net virtio_input virtio-gpu virtio_blk simpledrm");
    struct utsname u; uname(&u);
    static const char *mods[] = {
        "drivers/input/evdev", "drivers/input/mousedev",
        "drivers/firmware/qemu_fw_cfg",
        "lib/crc16", "crypto/crc32c_generic",
        "fs/mbcache", "fs/jbd2/jbd2", "fs/ext4/ext4"};
    char cmd[256];
    for (const char *m : mods) {
        snprintf(cmd, sizeof cmd,
                 "insmod /lib/modules/%s/kernel/%s.ko 2>/dev/null", u.release, m);
        sh(cmd);
    }
}

static void boot_data(void) {
    struct stat st;
    if (stat("/dev/vda", &st) != 0 || !S_ISBLK(st.st_mode)) return;
    if (mount("/dev/vda", "/data", "ext4", 0, 0) != 0) {
        sh("mke2fs -q /dev/vda");
        mount("/dev/vda", "/data", "ext4", 0, 0);
    }
}

static void boot_net(const char *cmdline) {
    sh("ip link set lo up");
    if (has(cmdline, "relicos.nonet")) return;
    DIR *d = opendir("/sys/class/net");
    if (!d) return;
    struct dirent *e; char cmd[192];
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "lo")) continue;
        snprintf(cmd, sizeof cmd, "ip link set %s up", e->d_name); sh(cmd);
        snprintf(cmd, sizeof cmd,
                 "udhcpc -i %s -q -n -t 3 -T 1 -s /etc/udhcpc.script 2>/dev/null",
                 e->d_name);
        if (sh(cmd) == 0) break;
    }
    closedir(d);
}

static void boot_key(void) {
    /* fw_cfg only -- /RELIC.CFG is read by core via plat_cfg_get(). */
    char buf[256];
    if (slurp("/sys/firmware/qemu_fw_cfg/by_name/opt/relicos.key/raw",
              buf, (int)sizeof buf) > 0)
        setenv("ANTHROPIC_API_KEY", buf, 1);
}

static void hide_cursor(void) {
    spew("/sys/class/graphics/fbcon/cursor_blink", "0");
    int fd = open("/dev/tty0", O_WRONLY);
    if (fd >= 0) { write(fd, "\033[?25l", 6); close(fd); }
}

/* ---- self-tests (relicos.test=...) ------------------------------------- */
static const char CC_HELLO[] =
    "#include <stdio.h>\n#include <linux/fb.h>\n"
    "int main(void){printf(\"hello from tcc, FBIOGET=%lx\\n\","
    "(long)FBIOGET_VSCREENINFO);return 0;}\n";
static const char CC_ROS[] =
    "#include <relicos.h>\n"
    "int main(void){ros_fb c={0};rect r=ros_place(&c,10,10);(void)ros_open;"
    "(void)ros_put;(void)ros_fill;(void)ros_window;return r.x;}\n";

static int wfile(const char *p, const char *s) {
    FILE *f = fopen(p, "w"); if (!f) return -1;
    fputs(s, f); fclose(f); return 0;
}

static void selftest(const char *which) {
    int ok = 0;
    if (!strcmp(which, "smoke")) {
        ok = 1; puts("RELICOS_SMOKE_PASS");
    } else if (!strcmp(which, "net")) {
        ok = sh("nslookup api.anthropic.com >/dev/null 2>&1") == 0;
        puts(ok ? "RELICOS_NET_PASS" : "RELICOS_NET_FAIL");
    } else if (!strcmp(which, "relic")) {
        ok = sh("cd /root && /bin/relic --nettest 2>&1 | tee /tmp/nt.log "
                "&& grep -q 'TLS OK' /tmp/nt.log") == 0;
        puts(ok ? "RELICOS_RELIC_PASS" : "RELICOS_RELIC_FAIL");
    } else if (!strcmp(which, "cc")) {
        wfile("/tmp/hello.c", CC_HELLO);
        wfile("/tmp/ros.c",   CC_ROS);
        ok = sh("tcc -run /tmp/hello.c && tcc -c -o /tmp/ros.o /tmp/ros.c") == 0;
        puts(ok ? "RELICOS_CC_PASS" : "RELICOS_CC_FAIL");
    } else if (!strcmp(which, "data")) {
        ok = sh("mountpoint -q /data") == 0;
        if (ok) {
            char b[16]; int n = 0;
            if (slurp("/data/boots", b, (int)sizeof b) > 0) n = atoi(b);
            char s[16]; snprintf(s, sizeof s, "%d\n", n + 1);
            wfile("/data/boots", s);
            printf("RELICOS_DATA_PASS boots=%d\n", n + 1);
        } else puts("RELICOS_DATA_FAIL (not mounted)");
    } else if (!strcmp(which, "fb")) {
        sh("ls -l /dev/fb* /dev/dri/* 2>&1");
        ok = access("/dev/fb0", F_OK) == 0;
        puts(ok ? "RELICOS_FB_PASS" : "RELICOS_FB_FAIL");
    } else {
        printf("RELICOS_TEST_FAIL unknown '%s'\n", which);
    }
    fflush(stdout);
    off();
}

/* ---- entry ------------------------------------------------------------- */
int pid1_main(void) {
    boot_mounts();
    boot_modules();
    boot_data();

    char cmdline[512] = "";
    slurp("/proc/cmdline", cmdline, (int)sizeof cmdline);

    boot_net(cmdline);
    boot_key();

    char v[128];
    if (kv(cmdline, "relicos.model=", v, (int)sizeof v))
        setenv("RELICOS_MODEL", v, 1);

    printf("RELICOS_BOOT_OK pid1=relicos cmdline=%s\n", cmdline);
    fflush(stdout);

    if (kv(cmdline, "relicos.test=", v, (int)sizeof v))
        selftest(v);                                   /* never returns */

    if (kv(cmdline, "relicos.run=", v, (int)sizeof v)) {
        if (!strcmp(v, "relic")) { execl("/bin/relic", "relic", (char*)0); _exit(127); }
        if (!strcmp(v, "ui"))    { hide_cursor(); return run_ui("/dev/fb0"); }
        if (!strcmp(v, "once"))  {
            hide_cursor();
            char p[1024] = "hello";
            slurp("/sys/firmware/qemu_fw_cfg/by_name/opt/relicos.prompt/raw",
                  p, (int)sizeof p);
            return run_once("/dev/fb0", p);
        }
    }
    /* No directive: drop to a shell (dev convenience). */
    execl("/bin/sh", "sh", (char*)0);
    _exit(127);
}
