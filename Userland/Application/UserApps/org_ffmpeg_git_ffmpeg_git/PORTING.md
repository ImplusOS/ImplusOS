# ImplusOS FFmpeg Porting Notes

This directory now contains:

- a native ImplusOS demo app (`main.c`)
- a local app `Makefile`
- a packaged demo resource

The supporting libc/POSIX work added in this change includes:

- buffered-free `FILE*` wrappers on top of existing fd syscalls
- POSIX-style file APIs: `open`, `creat`, `read`, `write`, `close`, `lseek`, `pipe`, `dup`, `dup2`
- directory APIs: `opendir`, `readdir`, `closedir`
- stat/time APIs: `stat`, `fstat`, `gettimeofday`, `time`, `nanosleep`
- socket entry points for the existing networking syscalls
- `mmap` compatibility backed by the existing user mmap syscall
- basic pthread single-thread stubs for non-threaded builds
- additional libc helpers such as `calloc`, `realloc`, `qsort`, `bsearch`, `strtoul`

What is still missing for a full upstream FFmpeg port:

- generated FFmpeg config headers for an ImplusOS target
- a build recipe selecting a minimal non-threaded / non-X11 / non-ALSA feature set
- more libc surface area expected by FFmpeg configure and selected codecs/formats
- stronger `poll/select/ioctl/pthread` semantics
- device/input/video backends specific to ImplusOS windowing and display APIs

The current demo verifies that the new compatibility layer links and runs as a native
user app, and it is intended to be the base for the next stage of the FFmpeg port.
