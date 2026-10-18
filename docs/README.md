# vlz — a minimal libvlc media player

vlz is a small C program that links against `libvlc.so.5` and reproduces the
startup contract observed in the stock VLC 3.0.24 launcher on Debian trixie,
then drives a queue-based playback loop with stdin commands.

## Build

```sh
make
```

Requires only `libvlc.so.5` (package `libvlc5` on Debian).  No development
headers or `vlc.pc` are needed — the required libvlc subset is declared by
hand in `vlz.c` and verified against the installed library's exports.

## Usage

```
vlz [vlz options] [libvlc options] [--] file|URL ...

vlz options:
  -h, --help        show this help
      --version     show vlz and libvlc versions
      --no-control  do not read commands from stdin
      --volume=N    initial volume (0-100)
      --verbose     print per-step diagnostics

Any other option (e.g. --intf=dummy, --aout=dummy, --network-caching=1000)
is forwarded verbatim to libvlc_new(), as the stock vlc launcher does.

stdin commands (when --no-control is not given):
  pause, p        toggle pause
  stop, s         stop current item
  next, n         skip to next item
  seek <sec>      seek to absolute seconds
  seek +<sec>     seek forward relative seconds
  vol <0-100>     set volume (deferred until an audio output exists)
  status          print one-line state line
  quit, q         request shutdown
  help            list commands
```

## Headless smoke tests

```sh
./mktestdata.sh          # generates testdata/tone.wav (2s) and tone2.wav
./vlz --version
./vlz --intf=dummy --aout=dummy --no-control testdata/tone.wav </dev/null
printf 'status\nq\n' | ./vlz --intf=dummy --aout=dummy testdata/tone.wav testdata/tone2.wav
./vlz --intf=dummy --aout=dummy --no-control testdata/missing.wav </dev/null; echo $?
```

All tests run with `--intf=dummy --aout=dummy` because the test environment is
headless.

## REA inspection of `/usr/bin/vlc`

The design of vlz was derived from a static Ghidra 12.1.4 analysis of the
installed VLC binary:

- **Target**: `/usr/bin/vlc` (VLC 3.0.24 Vetinari, Debian trixie, ELF x86-64,
  sha256 `6bdfe78688863035d4f59d5462c6b39af18e7321b38f6ce56a7bf267fbc87ea0`)
- **REA provider**: Ghidra 12.1.4
- **libvlc link target**: `/usr/lib/x86_64-linux-gnu/libvlc.so.5.6.1`
- **All libvlc symbols used by vlz verified present** with `nm -D` (312
  `libvlc_*` exports)

### Evidence IDs

- `ev_fc3a691afdc3788cf991c1237d880a9f7ff8e8e8573aaec1ac58bdaa1a96d7e4` — binary overview
- `ev_ba1d3441c6528239ddc0a7e149e001f9bab22e54cde2763da4059fc10873d793` — procedure list
- `ev_13aacd96e6a89bd17d9f0ac0c9a35e3b5a9a4f6fdf6cd3c3422fdedff16a1c78` — string table
- `ev_d9220e899e18dd5325f380b551171712cc421fe7d30e20e80497aa465e7f8601` — `main` decompilation (`FUN_00101280` @ 0x101280)
- `ev_9d2089a8ca2688e194c7539598087eedb68c2372e025376aecc1a4c284bd9cc5` — `libvlc_audio_get_volume` decompilation (ABI verification)

### Observed startup contract (mirrored by vlz)

1. `signal(SIGPIPE, SIG_IGN)`; `signal(SIGCHLD, SIG_DFL)`
2. `unsetenv("DESKTOP_STARTUP_ID")`
3. Root refusal (`geteuid()==0`)
4. `setlocale(LC_ALL, "")`
5. `isatty(STDERR_FILENO)` gated version banner
6. Block `{SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGPIPE, SIGCHLD}` via `pthread_sigmask`
7. `libvlc_new(argc, argv_with("--no-ignore-config" prepended))`
8. `libvlc_set_exit_handler(inst, exit_handler, NULL)` where handler does `pthread_kill(main, SIGTERM)`
9. `libvlc_set_app_id(inst, "org.VideoLAN.VLC", version, "vlc")`
10. `libvlc_set_user_agent(inst, "VLC media player", "VLC/3.0.24")`
11. `libvlc_add_intf(inst, "hotkeys,none")`
12. `libvlc_add_intf(inst, "globalhotkeys,none")`
13. `libvlc_add_intf(inst, NULL)` — treated as fatal if it fails
14. `libvlc_playlist_play(-1, 0, 0)` (legacy API)
15. Main thread: `sigwait({SIGHUP, SIGINT, SIGQUIT, SIGTERM})` after dropping
    `SIGCHLD` from the mask and dropping `SIGHUP` if inherited `SIG_IGN`
16. Shutdown: `signal(SIGINT, SIG_IGN); signal(SIGALRM, handler); alarm(3)`

### Byte-confirmed string constants in VLC's binary

| address range | string |
|---------------|--------|
| 0x102017–0x102029 | `--no-ignore-config` |
| 0x10202a–0x10203e | `--media-library` |
| 0x10203f–0x102048 | `--dbus` |
| 0x102049–0x10204c | `vlc` |
| 0x10204d–0x102065 | `org.VideoLAN.VLC` |
| 0x102066–0x102073 | `VLC/3.0.24` |
| 0x102074–0x102088 | `VLC media player` |
| 0x102089–0x102095 | `globalhotkeys,none` |
| 0x102097 | `""` (locale argument to `setlocale`) |

## Design deviations from inspected VLC [DEVIATION]

| area | VLC launcher | vlz |
|------|--------------|-----|
| argv prepended to libvlc | `--no-ignore-config`, `--media-library`, `--dbus` | only `--no-ignore-config` |
| default interface failure | fatal (exit 1) | warning, continue |
| SIGALRM handler | `signal(SIGINT, SIG_DFL)` — waits for another Ctrl-C | `signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL); _exit(2)` — force exit after 3s grace |
| volume API | VLC 2.x instance-based | VLC 3.0 media-player-based (see below) |

### Volume API ABI fix

Ghidra decompilation of the installed `libvlc.so.5` (`ev_9d2089a8...`)
showed that `libvlc_audio_get_volume` and `libvlc_audio_set_volume` in
VLC 3.0 take a `libvlc_media_player_t *`, not a `libvlc_instance_t *`.
The VLC 2.x instance-based signatures (still used by many examples) cause
libvlc to read `mp->input.p_resource` from offset 0x60 of the wrong object,
which is NULL, and crash inside `input_resource_HoldAout()` →
`pthread_mutex_lock(0x50)`.  vlz declares and calls the VLC 3.0
media-player prototypes; this was the only code change required to avoid the
crash.

Because VLC 3.0's `libvlc_audio_set_volume(mp, v)` returns -1 ("No active
audio output") when no input is playing, vlz defers the requested volume
(`--volume` or `vol` command) in `g_want_volume` and the control thread
retries it until an audio output appears.  This is a deliberate [DEVIATION]
from the inspected launcher's behavior (which didn't expose volume at all).

## Project structure

- `vlz.c` — complete source with hand-written libvlc declarations
- `Makefile` — `gcc -O2 -Wall -Wextra -std=gnu11 -l:libvlc.so.5 -lpthread -ldl`
- `mktestdata.sh` — generates test tones with ffmpeg/sox
- `testdata/` — generated WAV files (git-ignored)

## License

MIT — see `vlz.c` header for the REA provenance notice.