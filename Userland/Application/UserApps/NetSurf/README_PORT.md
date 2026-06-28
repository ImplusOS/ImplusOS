# NetSurf ImplusOS Port Harness

This directory contains the ImplusOS-side port harness for the official NetSurf framebuffer frontend. Keep the official checkout unmodified under `Source/netsurf/`, or point `NETSURF_CHECKOUT` at another clean official checkout.

Tracked files here are allowed port inputs:

- `Userland/Service/NetSurf/Makefile.config.implusos`: external NetSurf framebuffer configuration.
- `Userland/Service/NetSurf/MbedTlsShim/`: entropy and monotonic-time hooks for unmodified mbedTLS.
- `Userland/Service/NetSurf/cmake/curl-implusos-cache.cmake`: cross-compile cache for unmodified curl.
- `Userland/Service/NetSurf/pkgconfig/`: pkg-config files for bundled zlib/libpng.
- `Userland/Service/SDL12_Compat/`: minimal SDL 1.2 compatibility library used by libnsfb.
- `Choices`: runtime choices for the staged app, including the CA bundle path.
- `netsurf_start.c`: ImplusOS `_start` wrapper that runs C constructors and then calls NetSurf `main()`.
- `Makefile`: builds mbedTLS 3.6, curl, NetSurf libraries, NetSurf `nsfb`, and stages the app.

`Root/` is a generated staging prefix.  It is intentionally ignored by git and
can be recreated from vendored sources with:

```sh
make -C Userland/Service/NetSurf clean_root ARCH=x86_64
make -C Userland/Service/NetSurf root ARCH=x86_64
```

The generated prefix also installs third-party license notices under
`Root/share/licenses/`.

HTTPS is provided by NetSurf's existing curl fetcher using libcurl with the mbedTLS backend. NetSurf is built with `NETSURF_USE_CURL=YES` and `NETSURF_USE_OPENSSL=NO`; CA verification uses the staged `res/ca-certificates.crt` bundle referenced by `Choices`.

NetSurf's generated framebuffer `Messages` resources are gzip-compressed in the official checkout. The staging recipe expands `res/**/Messages` to plain text because the local `gzopen` shim is only a plain-text reader.

The libnsfb SDL surface writes 32bpp pixels as XRGB. `Userland/Service/SDL12_Compat` marks updated pixels opaque before calling `window_damage()` so the ImplusOS WM receives ARGB backing-store pixels instead of transparent RGB.

Useful commands:

```sh
make -C Userland/Application/UserApps/NetSurf verify_checkout ARCH=x86_64
make -C Userland/Application/UserApps/NetSurf all ARCH=x86_64
make app_build ARCH=x86_64
make ARCH=x86_64 image
make image_livecd ARCH=x86_64
```

The staged app is written to `Build/x86_64/Userland/UserApps/NetSurf/`, and the ISO payload copy is under `Build/x86_64/InstallPayload/root/Userland/UserApps/NetSurf/`.
