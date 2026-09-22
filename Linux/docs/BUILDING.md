# Building

## Dependencies

The app is a single C++17 translation unit (`src/main.cpp` plus header-only
helpers) linked against GTK4, Cairo, Pango, GdkPixbuf, and libcurl.

### Debian / Ubuntu / Mint / Pop!_OS

```bash
sudo apt install build-essential pkg-config \
    libgtk-4-dev libcurl4-openssl-dev
```

### Fedora / RHEL / CentOS Stream

```bash
sudo dnf install gcc-c++ pkgconf-pkg-config \
    gtk4-devel libcurl-devel
```

### Arch / Manjaro

```bash
sudo pacman -S base-devel pkgconf gtk4 curl
```

GTK4 pulls in Cairo, Pango, and GdkPixbuf as dependencies automatically on
every distro above — they don't need separate packages.

## Build

```bash
make            # -> build/iwana-proxy
make run        # build + run
make clean      # remove build/
```

The `Makefile` shells out to `pkg-config` for all compiler/linker flags, so
it doesn't hardcode include/library paths — it should work unmodified on
any distro once the dev packages above are installed.

### Minimum versions

Built and tested against GTK 4.14 / Cairo 1.18 / Pango 1.52 / curl 8.5
(Ubuntu 24.04 "noble"). The code doesn't use anything especially new from
any of these — GTK 4.6+ (Ubuntu 22.04-era) should work, though it hasn't
been specifically tested.

## Running headless / in CI

The app is a normal GTK4 window, so it needs a display. For CI or a
container without one, run it under Xvfb:

```bash
Xvfb :99 -screen 0 1024x900x24 &
export DISPLAY=:99
./build/iwana-proxy
```

This is exactly how the screenshots in `docs/screenshots/` were produced,
and how every fix in `CHANGELOG.md` was actually verified rather than just
assumed to work — `import -window root screenshot.png` (ImageMagick)
against the Xvfb root window captures the same thing a human would see.

## Code style notes

- Comments in the source explain *why*, not *what* — especially around
  anything that was a real bug fix; the comment says what broke and why the
  current code avoids it, so the same mistake doesn't get reintroduced by a
  future edit. If you're fixing something non-obvious, leave that trail for
  the next person.
- No exceptions, no RTTI features used, no third-party C++ dependencies
  beyond the system libraries above — keep it that way unless there's a
  strong reason not to; this is a small app and the build should stay
  trivial to reproduce.
- `TextStyle` + `DrawText`/`DrawLabelCentered`/`DrawLabelEdgeAligned` are the
  three text-drawing primitives; see `docs/ARCHITECTURE.md` before adding a
  fourth one — there's almost certainly an existing one that fits.
