/*
 * vlz - a small libvlc-driven media player.
 *
 * This program was reconstructed from a static REA/Ghidra inspection of the
 * installed VLC launcher (/usr/bin/vlc, VLC 3.0.24 Vetinari, Debian trixie,
 * ELF x86-64, sha256 6bdfe78688863035d4f59d5462c6b39af18e7321b38f6ce56a7bf267fbc87ea0).
 *
 * The inspection established that the stock `vlc` binary is a thin launcher:
 * its main() performs a fixed startup contract and then drives libvlc.so.5
 * through a small, stable C API.  vlz reproduces the observed parts of that
 * contract (root refusal, isatty-gated banner, signal-mask discipline,
 * exit-handler handshake, libvlc argv injection) and adds its own queue-based
 * playback control loop and identity.
 *
 * No libvlc development headers are installed on this system, so the required
 * subset of the ABI-stable libvlc API is declared below and linked directly
 * against libvlc.so.5 (all symbols verified present with nm -D).
 *
 * Findings this file is derived from are marked [REA]; design choices that
 * differ from the inspected binary are marked [DEVIATION] and listed in
 * README.md.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define VLZ_VERSION "0.1.0"
#define VLZ_APP_ID  "dev.VLZ.player"

/* ------------------------------------------------------------------ */
/* libvlc API subset (verified exported by nm -D on libvlc.so.5)        */
/* ------------------------------------------------------------------ */

typedef struct libvlc_instance_t libvlc_instance_t;
typedef struct libvlc_media_t libvlc_media_t;
typedef struct libvlc_media_player_t libvlc_media_player_t;
typedef int64_t libvlc_time_t;

/* Values match the libvlc_state_t enum shipped since VLC 0.9 (stable ABI). */
typedef enum libvlc_state_t {
    libvlc_NothingSpecial = 0,
    libvlc_Opening        = 1,
    libvlc_Buffering      = 2,
    libvlc_Playing        = 3,
    libvlc_Paused         = 4,
    libvlc_Stopped        = 5,
    libvlc_Ended          = 6,
    libvlc_Error          = 7
} libvlc_state_t;

/* instance lifecycle and identity - the exact set observed in /usr/bin/vlc */
extern libvlc_instance_t *libvlc_new(int argc, const char *const *argv);
extern void               libvlc_release(libvlc_instance_t *inst);
extern const char        *libvlc_get_version(void);
extern const char        *libvlc_get_changeset(void);
extern void               libvlc_set_exit_handler(libvlc_instance_t *inst,
                                                   void (*cb)(void *), void *data);
extern void               libvlc_set_app_id(libvlc_instance_t *inst, const char *id,
                                             const char *version, const char *icon);
extern void               libvlc_set_user_agent(libvlc_instance_t *inst,
                                                 const char *name, const char *http);
extern int                libvlc_add_intf(libvlc_instance_t *inst, const char *name);
extern const char        *libvlc_errmsg(void);

/* media */
extern libvlc_media_t *libvlc_media_new_path(libvlc_instance_t *inst, const char *path);
extern libvlc_media_t *libvlc_media_new_location(libvlc_instance_t *inst, const char *mrl);
extern void            libvlc_media_add_option(libvlc_media_t *m, const char *opts);
extern void            libvlc_media_release(libvlc_media_t *m);
extern libvlc_state_t  libvlc_media_get_state(libvlc_media_t *m);

/* media player */
extern libvlc_media_player_t *libvlc_media_player_new(libvlc_instance_t *inst);
extern void            libvlc_media_player_release(libvlc_media_player_t *p);
extern void            libvlc_media_player_set_media(libvlc_media_player_t *p,
                                                      libvlc_media_t *m);
extern int             libvlc_media_player_play(libvlc_media_player_t *p);
extern void            libvlc_media_player_pause(libvlc_media_player_t *p);
extern void            libvlc_media_player_stop(libvlc_media_player_t *p);
extern int             libvlc_media_player_is_playing(libvlc_media_player_t *p);
extern libvlc_state_t  libvlc_media_player_get_state(libvlc_media_player_t *p);
extern libvlc_time_t   libvlc_media_player_get_time(libvlc_media_player_t *p);
extern int             libvlc_media_player_set_time(libvlc_media_player_t *p,
                                                     libvlc_time_t t);
extern libvlc_time_t   libvlc_media_player_get_length(libvlc_media_player_t *p);
extern float           libvlc_media_player_get_rate(libvlc_media_player_t *p);
extern int             libvlc_media_player_set_rate(libvlc_media_player_t *p, float r);

/* audio.  In VLC 3.0 these take the media player, not the instance: the
 * instance-based prototypes of VLC 2.x are gone.  Declaring them with
 * libvlc_instance_t * makes libvlc read mp->input.p_resource from offset
 * 0x60 of the wrong object, which is NULL, and crash inside
 * input_resource_HoldAout() -> pthread_mutex_lock(0x50).  Confirmed against
 * upstream 3.0.x lib/audio.c and the decompilation of the installed
 * libvlc.so.5 (ev_9d2089a8ca2688e194c7539598087eedb68c2372e025376aecc1a4c284bd9cc5). */
extern int libvlc_audio_get_volume(libvlc_media_player_t *p);
extern int libvlc_audio_set_volume(libvlc_media_player_t *p, int volume);

/* ------------------------------------------------------------------ */
/* globals shared with the control thread                              */
/* ------------------------------------------------------------------ */

static libvlc_instance_t     *g_inst;
static libvlc_media_player_t *g_player;
static libvlc_media_t        *g_cur_media;   /* retained ref for state queries */
static pthread_t              g_main_thread;

static const char **g_queue;                /* media items from argv */
static int    g_queue_len;
static int    g_index;                      /* current item */

static volatile int g_quit;                 /* control thread must exit      */
static volatile int g_shutdown;             /* shutdown sequence in progress */
static volatile int g_manual_stop;          /* user issued `stop`            */
static volatile int g_failures;             /* items that ended in error     */
static volatile int g_control = 1;          /* read commands from stdin      */
static volatile int g_want_volume = -1;     /* volume to apply once an aout
                                             * exists: VLC 3.0's
                                             * libvlc_audio_set_volume() takes
                                             * a media player and returns -1
                                             * while no input is active       */
static int          g_verbose;

static const char *state_name(libvlc_state_t s)
{
    switch (s) {
    case libvlc_NothingSpecial: return "idle";
    case libvlc_Opening:        return "opening";
    case libvlc_Buffering:      return "buffering";
    case libvlc_Playing:        return "playing";
    case libvlc_Paused:         return "paused";
    case libvlc_Stopped:        return "stopped";
    case libvlc_Ended:          return "ended";
    case libvlc_Error:          return "error";
    default:                    return "?";
    }
}

/* Observed exit-handler shape in /usr/bin/vlc (FUN_00101750):
 *   pthread_kill(main_thread, SIGTERM)
 * which unblocks the main thread waiting in sigwait().  [REA] */
static void vlz_exit_handler(void *unused)
{
    (void)unused;
    pthread_kill(g_main_thread, SIGTERM);
}

/* Observed SIGALRM handler in /usr/bin/vlc (FUN_00101740):
 *   signal(SIGINT, SIG_DFL)
 * i.e. after the 3-second shutdown grace period a further Ctrl-C force-kills.
 * [REA] vlz additionally force-exits from this handler so that a hung
 * shutdown can never block a script. [DEVIATION]
 */
static void vlz_alrm_handler(int sig)
{
    (void)sig;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    _exit(2);
}

static void request_shutdown(void)
{
    if (g_shutdown || g_quit)
        return;
    g_quit = 1;
    pthread_kill(g_main_thread, SIGTERM);
}

/* ------------------------------------------------------------------ */
/* playback queue                                                      */
/* ------------------------------------------------------------------ */

static void play_item(int idx)
{
    const char *item = g_queue[idx];
    char        resolved[4096];
    const char *path = item;

    /* Anything with a scheme goes to media_new_location, otherwise it is a
     * filesystem path (made absolute when possible, like a careful player). */
    if (strstr(item, "://") == NULL && realpath(item, resolved) != NULL)
        path = resolved;

    libvlc_media_t *m = strstr(item, "://")
                            ? libvlc_media_new_location(g_inst, item)
                            : libvlc_media_new_path(g_inst, path);
    if (m == NULL) {
        fprintf(stderr, "[vlz] cannot open %s: %s\n",
                item, libvlc_errmsg() ? libvlc_errmsg() : "unknown error");
        g_failures++;
        return;
    }

    if (g_cur_media)
        libvlc_media_release(g_cur_media);
    g_cur_media = m;                       /* keep our own reference */
    g_manual_stop = 0;

    libvlc_media_player_set_media(g_player, m);
    if (libvlc_media_player_play(g_player) != 0) {
        fprintf(stderr, "[vlz] cannot play %s: %s\n",
                item, libvlc_errmsg() ? libvlc_errmsg() : "unknown error");
        g_failures++;
        return;
    }
    fprintf(stderr, "[vlz] playing (%d/%d): %s\n",
            idx + 1, g_queue_len, item);
}

/* Advance to the next queue item, or request shutdown when the queue is done.
 * Called only from the control thread. */
static void advance_queue(const char *why)
{
    if (g_cur_media) {
        fprintf(stderr, "[vlz] %s: %s\n", why, g_queue[g_index]);
        libvlc_media_release(g_cur_media);
        g_cur_media = NULL;
    }
    g_index++;
    if (g_index >= g_queue_len) {
        fprintf(stderr, "[vlz] queue finished\n");
        request_shutdown();
        return;
    }
    play_item(g_index);
}

/* Apply the pending volume once an audio output exists.  VLC 3.0's
 * libvlc_audio_set_volume() works on the media player's aout and fails with
 * -1 ("No active audio output") while nothing is playing, so the request is
 * remembered and retried from the control thread. */
static void try_apply_volume(void)
{
    int v = g_want_volume;
    if (v < 0)
        return;
    if (libvlc_audio_set_volume(g_player, v) >= 0) {
        if (g_verbose)
            fprintf(stderr, "[vlz] volume set to %d\n", v);
        g_want_volume = -1;
    }
}

static void cmd_status(void)
{
    libvlc_state_t st = g_cur_media ? libvlc_media_player_get_state(g_player)
                                    : libvlc_NothingSpecial;
    libvlc_time_t  t  = libvlc_media_player_get_time(g_player);
    libvlc_time_t  l  = libvlc_media_player_get_length(g_player);
    int            v  = libvlc_audio_get_volume(g_player);
    if (v < 0)                            /* no aout yet: report the request */
        v = g_want_volume;
    printf("state=%s time=%.2fs length=%.2fs rate=%.2f volume=%d\n",
           state_name(st), t / 1000.0, l / 1000.0,
           libvlc_media_player_get_rate(g_player), v);
}

static void handle_command(char *line)
{
    while (*line == ' ' || *line == '\t')
        line++;
    line[strcspn(line, "\r\n")] = '\0';
    if (*line == '\0')
        return;

    if (!strcmp(line, "q") || !strcmp(line, "quit")) {
        fprintf(stderr, "[vlz] quitting\n");
        request_shutdown();
    } else if (!strcmp(line, "p") || !strcmp(line, "pause")) {
        if (libvlc_media_player_get_state(g_player) == libvlc_Paused)
            libvlc_media_player_play(g_player);
        else
            libvlc_media_player_pause(g_player);
    } else if (!strcmp(line, "s") || !strcmp(line, "stop")) {
        g_manual_stop = 1;
        libvlc_media_player_stop(g_player);
    } else if (!strcmp(line, "n") || !strcmp(line, "next")) {
        g_manual_stop = 1;
        libvlc_media_player_stop(g_player);
        advance_queue("skipped");
    } else if (!strcmp(line, "status")) {
        cmd_status();
    } else if (!strncmp(line, "seek ", 5)) {
        const char *arg = line + 5;
        int relative = (arg[0] == '+' || arg[0] == '-');
        double v = strtod(arg, NULL);
        libvlc_time_t base = relative ? libvlc_media_player_get_time(g_player) : 0;
        libvlc_time_t target = base + (libvlc_time_t)(v * 1000.0);
        if (target < 0)
            target = 0;
        libvlc_media_player_set_time(g_player, target);
    } else if (!strncmp(line, "vol ", 4)) {
        int v = atoi(line + 4);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        g_want_volume = v;
        try_apply_volume();
        if (g_want_volume >= 0 && g_verbose)
            fprintf(stderr, "[vlz] no audio output yet, volume %d pending\n", v);
    } else if (!strcmp(line, "help")) {
        puts("commands: pause(p) stop(s) next(n) seek <sec>|+<sec> vol <0-100>"
             " status quit(q)");
    } else {
        fprintf(stderr, "[vlz] unknown command: %s (try help)\n", line);
    }
}

/* ------------------------------------------------------------------ */
/* control thread: queue monitor + optional stdin command loop         */
/* ------------------------------------------------------------------ */

static void *control_thread(void *unused)
{
    (void)unused;
    char line[512];

    while (!g_quit) {
        /* -- pending volume: retries until an aout exists ------------- */
        try_apply_volume();

        /* -- queue monitor ------------------------------------------------ */
        if (g_cur_media && !g_manual_stop && !g_quit) {
            libvlc_state_t st = libvlc_media_player_get_state(g_player);
            if (st == libvlc_Ended || st == libvlc_Error) {
                if (st == libvlc_Error) {
                    const char *err = libvlc_errmsg();
                    fprintf(stderr, "[vlz] playback error on %s: %s\n",
                            g_queue[g_index], err ? err : "unknown error");
                    g_failures++;
                }
                advance_queue(st == libvlc_Error ? "failed" : "finished");
                continue;
            }
        }

        /* -- stdin command loop ------------------------------------------ */
        if (!g_control) {
            struct timeval tv = {0, 200000};    /* 200 ms */
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(STDIN_FILENO, &rfds);
            select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
            continue;
        }

        fd_set rfds;
        struct timeval tv = {0, 200000};
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int r = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (r > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
            if (fgets(line, sizeof line, stdin) == NULL) {
                /* EOF or read error: keep playing, just stop reading. */
                g_control = 0;
                continue;
            }
            handle_command(line);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* usage                                                               */
/* ------------------------------------------------------------------ */

static void usage(FILE *out)
{
    fprintf(out,
        "VLZ " VLZ_VERSION " - a libvlc media player\n"
        "usage: vlz [vlz options] [libvlc options] [--] file|URL ...\n"
        "\n"
        "vlz options:\n"
        "  -h, --help        show this help\n"
        "      --version     show vlz and libvlc versions\n"
        "      --no-control  do not read commands from stdin\n"
        "      --volume=N    initial volume (0-100)\n"
        "      --verbose     print per-step diagnostics\n"
        "\n"
        "Any other option (e.g. --intf=dummy, --aout=dummy, --network-caching=1000)\n"
        "is forwarded verbatim to libvlc, as the stock vlc launcher does.\n"
        "\n"
        "stdin commands: pause(p) stop(s) next(n) seek <sec> vol <0-100> status quit(q)\n");
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    /* ---- argument split ------------------------------------------------ */
    const char **libvlc_opts = calloc((size_t)argc + 2, sizeof *libvlc_opts);
    const char **media       = calloc((size_t)argc + 1, sizeof *media);
    int   n_opts = 0, n_media = 0;
    int   initial_volume = -1;
    int   no_control = 0;
    int   parse_opts = 1;

    if (!libvlc_opts || !media) {
        fprintf(stderr, "vlz: out of memory\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (parse_opts && !strcmp(a, "--")) {
            parse_opts = 0;
        } else if (parse_opts && (!strcmp(a, "-h") || !strcmp(a, "--help"))) {
            usage(stdout);
            return 0;
        } else if (parse_opts && !strcmp(a, "--version")) {
            /* The stock launcher prints its banner only when stderr is a
             * tty [REA]; --version is explicit, so print unconditionally. */
            printf("VLZ " VLZ_VERSION " (backend %s, changeset %s)\n",
                   libvlc_get_version(), libvlc_get_changeset());
            return 0;
        } else if (parse_opts && !strcmp(a, "--no-control")) {
            no_control = 1;
        } else if (parse_opts && !strcmp(a, "--verbose")) {
            g_verbose = 1;
        } else if (parse_opts && !strncmp(a, "--volume=", 9)) {
            initial_volume = atoi(a + 9);
            if (initial_volume < 0) initial_volume = 0;
            if (initial_volume > 100) initial_volume = 100;
        } else if (parse_opts && a[0] == '-' && a[1] == '-' && a[2] != '\0') {
            libvlc_opts[n_opts++] = a;       /* forwarded to libvlc_new() */
        } else if (a[0] == '-' && a[1] != '\0' && parse_opts) {
            fprintf(stderr, "vlz: unknown option: %s\n", a);
            usage(stderr);
            return 2;
        } else {
            media[n_media++] = a;
        }
    }

    if (n_media == 0) {
        fprintf(stderr, "vlz: no media given\n");
        usage(stderr);
        return 2;
    }

    /* ---- startup contract, mirroring the inspected /usr/bin/vlc main ---- */

    signal(SIGPIPE, SIG_IGN);        /* [REA] observed first instructions  */
    signal(SIGCHLD, SIG_DFL);        /* [REA]                             */
    unsetenv("DESKTOP_STARTUP_ID");  /* [REA] dlsym'd unsetenv wrapper     */

    if (geteuid() == 0) {            /* [REA] root refusal                 */
        fprintf(stderr,
            "vlz: not supposed to be run as root.\n"
            "Media playback as root creates root-owned state and widens the\n"
            "attack surface of decoders and network access. Run vlz as your\n"
            "normal user instead.\n");
        return 1;
    }

    setlocale(LC_ALL, "");           /* [REA] dlsym'd setlocale(6, "")     */

    g_main_thread = pthread_self();

    if (isatty(STDERR_FILENO)) {     /* [REA] isatty(2) gated version banner */
        fprintf(stderr, "VLZ %s (backend %s, revision %s)\n",
                VLZ_VERSION, libvlc_get_version(), libvlc_get_changeset());
    }

    /* Block the same signal set the inspected launcher blocks before it
     * starts playback: {SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGPIPE, SIGCHLD}.
     * Threads created later inherit this mask, so termination signals reach
     * main only through sigwait() or pthread_kill(). [REA] */
    sigset_t blockset, waitset;
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGHUP);
    sigaddset(&blockset, SIGINT);
    sigaddset(&blockset, SIGQUIT);
    sigaddset(&blockset, SIGTERM);
    sigaddset(&blockset, SIGPIPE);
    sigaddset(&blockset, SIGCHLD);
    pthread_sigmask(SIG_SETMASK, &blockset, NULL);

    /* ---- libvlc instance ------------------------------------------------ */
    /* The inspected launcher prepends {"--no-ignore-config", "--media-library",
     * "--dbus"} to the user's arguments before libvlc_new().  vlz prepends
     * only --no-ignore-config: the media library and DBus control are VLC
     * product features, not playback requirements. [DEVIATION] */
    const char **vargs = calloc((size_t)n_opts + 3, sizeof *vargs);
    if (!vargs) {
        fprintf(stderr, "vlz: out of memory\n");
        return 1;
    }
    int n_vargs = 0;
    vargs[n_vargs++] = "--no-ignore-config";
    vargs[n_vargs++] = "--video-title=VLZ Media Player";
    for (int i = 0; i < n_opts; i++)
        vargs[n_vargs++] = libvlc_opts[i];

    g_inst = libvlc_new(n_vargs, vargs);
    if (g_inst == NULL) {
        fprintf(stderr, "vlz: cannot start backend: %s\n",
                libvlc_errmsg() ? libvlc_errmsg() : "unknown error");
        return 1;
    }

    libvlc_set_exit_handler(g_inst, vlz_exit_handler, NULL);            /* [REA] */
    libvlc_set_app_id(g_inst, VLZ_APP_ID, VLZ_VERSION, "VLZ");          /* [REA] */
    libvlc_set_user_agent(g_inst, "VLZ Media Player", "VLZ/" VLZ_VERSION); /* [REA] */

    /* VLC 3.0 sets volume on the media player's audio output, which does not
     * exist before playback starts, so an initial volume is recorded here and
     * applied by the control thread once an aout appears. */
    if (initial_volume >= 0)
        g_want_volume = initial_volume;

    /* The inspected launcher loads "hotkeys,none", then "globalhotkeys,none",
     * then the default interface, and treats a failure of the last one as
     * fatal (exit 1).  vlz keeps the hotkeys interfaces but treats a missing
     * default interface (common on headless hosts) as a warning. [DEVIATION] */
    libvlc_add_intf(g_inst, "hotkeys,none");
    libvlc_add_intf(g_inst, "globalhotkeys,none");
    if (libvlc_add_intf(g_inst, NULL) != 0)
        fprintf(stderr, "[vlz] no default interface available, continuing"
                        " without a graphical UI\n");

    /* ---- player and queue ---------------------------------------------- */
    g_queue = media;
    g_queue_len = n_media;
    g_index = 0;
    g_control = !no_control;

    g_player = libvlc_media_player_new(g_inst);
    if (!g_player) {
        fprintf(stderr, "vlz: cannot create media player\n");
        return 1;
    }

    pthread_t ctl;
    if (pthread_create(&ctl, NULL, control_thread, NULL) != 0) {
        fprintf(stderr, "vlz: cannot start control thread: %s\n",
                strerror(errno));
        return 1;
    }

    play_item(0);
    if (!g_cur_media && g_queue_len > 0 && g_failures > 0) {
        /* first item failed outright - advance_queue() may already have
         * moved on; if the queue drained without playing, fall through to
         * the wait loop which will exit immediately via pthread_kill. */
        if (g_index >= g_queue_len)
            goto shutdown;
    }

    /* ---- main-thread wait ---------------------------------------------- */
    /* The inspected launcher blocks {SIGHUP, SIGINT, SIGQUIT, SIGTERM} in
     * sigwait() while libvlc plays, after (a) dropping SIGCHLD from the
     * blocked set so children are reaped and (b) not waiting on SIGHUP when
     * its disposition was inherited as SIG_IGN.  [REA] */
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGHUP);
    sigaddset(&blockset, SIGINT);
    sigaddset(&blockset, SIGQUIT);
    sigaddset(&blockset, SIGTERM);
    sigaddset(&blockset, SIGPIPE);
    pthread_sigmask(SIG_SETMASK, &blockset, NULL);   /* unblocks SIGCHLD */

    sigemptyset(&waitset);
    sigaddset(&waitset, SIGHUP);
    sigaddset(&waitset, SIGINT);
    sigaddset(&waitset, SIGQUIT);
    sigaddset(&waitset, SIGTERM);

    {
        struct sigaction old_hup;
        if (sigaction(SIGHUP, NULL, &old_hup) == 0 &&
            old_hup.sa_handler == SIG_IGN)
            sigdelset(&waitset, SIGHUP);
    }

    int sig = 0;
    sigwait(&waitset, &sig);

shutdown:
    /* Shutdown grace policy of the inspected launcher: SIGINT ignored,
     * SIGALRM armed for 3 seconds so the process cannot hang forever.
     * [REA] vlz's handler force-exits instead of waiting for yet another
     * Ctrl-C. [DEVIATION] */
    g_shutdown = 1;
    signal(SIGINT, SIG_IGN);
    signal(SIGALRM, vlz_alrm_handler);
    alarm(3);

    g_quit = 1;
    if (g_player)
        libvlc_media_player_stop(g_player);
    pthread_join(ctl, NULL);

    if (g_cur_media)
        libvlc_media_release(g_cur_media);
    if (g_player)
        libvlc_media_player_release(g_player);
    libvlc_release(g_inst);
    free(vargs);
    free(libvlc_opts);
    free(media);
    alarm(0);

    if (g_failures)
        fprintf(stderr, "[vlz] exiting with %d failed item(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
