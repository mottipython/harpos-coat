Name:           harpos-coat
Version:        %{version}
Release:        1%{?dist}
Summary:        Harpo's Coat, a cartoon screensaver of Harpo Marx and his coat
License:        MIT
Source0:        harpos-coat-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
Harpo's Coat, a cartoon screensaver: Harpo Marx on a vaudeville stage pulls one
thing after another out of his long coat (a candle burning at both ends, a cup of
hot coffee, a fish, a blowtorch, a live duck, a ladder far too long for any coat),
shows each one off, and tosses it onto a growing heap. Now and then he honks.

On Wayland desktops it starts by itself after five minutes idle.
harpos-coat-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/harpos-coat

%files
%license LICENSE
%doc README.md
%{_bindir}/harpos-coat
%{_bindir}/harpos-coat-screensaver
%{_datadir}/applications/harpos-coat.desktop
%dir %{_datadir}/screensaver-service
%dir %{_datadir}/screensaver-service/savers
%{_datadir}/screensaver-service/savers/harpos-coat.desktop
%{_datadir}/icons/hicolor/256x256/apps/harpos-coat.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/harpos-coat-screensaver.desktop
