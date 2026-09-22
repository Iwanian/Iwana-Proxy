# Packaging

## AppImage (primary distribution format)

Requires [`linuxdeploy`](https://github.com/linuxdeploy/linuxdeploy) and its
GTK plugin on `PATH`.

```bash
make appimage
```

This runs `packaging/build_appimage.sh`, which stages `build/iwana-proxy`
plus `packaging/iwana-proxy.desktop` and the app icon into an `AppDir`, then
calls `linuxdeploy --plugin gtk --output appimage`. The GTK plugin bundles
GTK4/Cairo/Pango/GdkPixbuf/GLib themselves into the AppImage, so the result
runs on a target system that only has the "base system" libraries every
Linux desktop already ships (glibc, X11 client libs, fontconfig) — it does
**not** depend on the target having GTK4 installed at all.

Manually, the equivalent is:

```bash
mkdir -p build/AppDir/usr/{bin,share/applications,share/icons/hicolor/256x256/apps}
cp build/iwana-proxy build/AppDir/usr/bin/
cp packaging/iwana-proxy.desktop build/AppDir/usr/share/applications/
cp assets/iwana_icon_256.png build/AppDir/usr/share/icons/hicolor/256x256/apps/iwana-proxy.png

linuxdeploy --appdir build/AppDir \
    --executable build/AppDir/usr/bin/iwana-proxy \
    --desktop-file build/AppDir/usr/share/applications/iwana-proxy.desktop \
    --icon-file assets/iwana_icon_256.png \
    --plugin gtk \
    --output appimage
```

## `.deb`

```bash
mkdir -p packaging/deb/DEBIAN packaging/deb/usr/bin \
         packaging/deb/usr/share/applications \
         packaging/deb/usr/share/icons/hicolor/256x256/apps
cp build/iwana-proxy packaging/deb/usr/bin/
cp packaging/iwana-proxy.desktop packaging/deb/usr/share/applications/
cp assets/iwana_icon_256.png packaging/deb/usr/share/icons/hicolor/256x256/apps/iwana-proxy.png
# packaging/deb/DEBIAN/control already has Package/Version/Depends set
dpkg-deb --build --root-owner-group packaging/deb build/iwana-proxy_1.0.0_amd64.deb
```

`Depends:` is `libgtk-4-1, libcurl4t64 | libcurl4, libgdk-pixbuf-2.0-0,
libc6` — the same libraries almost any GTK4 app on the system already
depends on.

## `.rpm`

```bash
mkdir -p ~/rpmbuild/{SPECS,SOURCES,BUILD,RPMS,SRPMS}
# stage build/iwana-proxy + .desktop + icon into a versioned source tree,
# tar it into ~/rpmbuild/SOURCES/iwana-proxy-1.0.0.tar.gz, then:
cp packaging/iwana-proxy.spec ~/rpmbuild/SPECS/
rpmbuild -bb ~/rpmbuild/SPECS/iwana-proxy.spec
```

`rpmbuild` auto-detects the shared-library dependencies (`libgtk-4.so.1`,
`libcurl.so.4`, …) from the binary itself; `Requires:` in the spec only
needs the human-readable package names for documentation purposes.

## Multi-architecture (arm64, riscv64, …)

GTK4's *development* packages for non-x86_64 architectures live on
distro-specific "ports" mirrors (e.g. Ubuntu's `ports.ubuntu.com`) that
many locked-down build environments — sandboxed CI, restricted-network
build servers — simply can't reach, even when a cross-*compiler* installs
fine. Cross-compiling without those dev packages means there's nothing to
link against.

The reliable fix is to build **natively inside an emulated container** for
the target architecture, so `apt-get` there reaches the real
architecture-specific mirror and the output is a genuinely linked, native
binary — not a relabeled x86_64 one.

### Locally, via Docker + QEMU

```bash
# One-time setup: register QEMU binfmt handlers with Docker
docker run --privileged --rm tonistiigi/binfmt --install all

# Linux/macOS:
bash packaging/docker/build-cross.sh
# Windows (PowerShell):
.\packaging\docker\build-cross.ps1
# Windows (cmd.exe):
packaging\docker\build-cross.bat
```

Each script drives `packaging/docker/Dockerfile.multiarch` under
`docker buildx build --platform linux/<arch>`, producing a `.deb`, `.rpm`,
and a raw `iwana-proxy-<arch>` binary per architecture under
`build/cross/<arch>/`.

**LoongArch64** needs the Dockerfile's base image bumped from
`debian:bookworm-slim` to `debian:sid` (or a pinned
`snapshot.debian.org` date) first — LoongArch support landed after
bookworm. See the comment at the top of `Dockerfile.multiarch`.

### In CI

`.github/workflows/build-multiarch.yml` runs the same build via
`docker/setup-qemu-action` + `docker/setup-buildx-action` on GitHub's own
runners (which have full, unrestricted network access), for `amd64`,
`arm64`, and `riscv64`. Trigger it from the Actions tab
("Build multi-arch packages" → *Run workflow*) or by pushing a `v*` tag;
download the per-architecture `.deb`/`.rpm` from the run's artifacts.
