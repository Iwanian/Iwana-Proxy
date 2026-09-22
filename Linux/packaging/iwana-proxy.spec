Name:           iwana-proxy
Version:        1.0.0
Release:        1%{?dist}
Summary:        Telegram MTProto proxy finder, tester, and connector

License:        Proprietary
URL:            https://github.com/Iwanian
Source0:        %{name}-%{version}.tar.gz

BuildArch:      x86_64
Requires:       gtk4, libcurl, gdk-pixbuf2

%description
Iwana Proxy fetches, tests, and connects to Telegram MTProto proxies.
Includes latency/jitter/speed testing, favorites, and fa/en/ru localization.

%prep
%setup -q

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/share/applications
mkdir -p %{buildroot}/usr/share/icons/hicolor/256x256/apps
cp -a usr/bin/iwana-proxy %{buildroot}/usr/bin/iwana-proxy
cp -a usr/share/applications/iwana-proxy.desktop %{buildroot}/usr/share/applications/iwana-proxy.desktop
cp -a usr/share/icons/hicolor/256x256/apps/iwana-proxy.png %{buildroot}/usr/share/icons/hicolor/256x256/apps/iwana-proxy.png

%files
/usr/bin/iwana-proxy
/usr/share/applications/iwana-proxy.desktop
/usr/share/icons/hicolor/256x256/apps/iwana-proxy.png

%changelog
* Sun Sep 06 2026 Iwanian <noreply@iwanian.io> - 1.0.0-1
- Initial Linux release (GTK4 + Cairo + Pango port)
