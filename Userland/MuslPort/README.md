# musl Port (ImplusOS)

This directory contains a **minimal musl port layer** for ImplusOS userland.

## Scope

Current port scope is intentionally small and focused on making the full musl
archive buildable on ImplusOS:

- dedicated musl architecture: `musl/arch/implus`
- Linux syscall number to ImplusOS syscall translation:
  `musl/src/internal/implus_syscall.c`
- safe `errno` fallback for ImplusOS build:
  `musl/src/errno/__errno_location.c`

These are enough to validate that musl source files can run on the ImplusOS
syscall ABI (`SYSCALL` instruction on x86_64) without Linux.

## ABI Mapping

ImplusOS syscall numbers are defined in:

- `Kernel/Syscall/Syscall_Main.h`

musl-facing mapping for this port is:

- `Userland/MuslPort/include/bits/syscall.h`

## Current Limitations

This is not a full musl target yet. Missing pieces include:

- full `pthread`/TLS integration
- complete signal semantics compatible with musl expectations
- VM contracts needed by full musl allocator/runtime path
- dynamic linker/loader integration (`ldso`)
- broad syscall coverage beyond the minimal set above

## Test App

Build musl first:

- `make -f Makefile.musl musl_lib`

Then build the test app:

- `make -C Userland/Application/UserApps/com_ImplusOS_musl_test`

The test app writes a verification file:

- `/Userland/musl_port_test.txt`
