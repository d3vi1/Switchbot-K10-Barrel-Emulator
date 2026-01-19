Name:           k10-barrel-emulator
Version:        0.1.0
Release:        1%{?dist}
Summary:        SwitchBot K10 barrel emulator (skeleton)

License:        GPL-3.0-or-later
URL:            https://github.com/d3vi1/Switchbot-K10-Barrel-Emulator
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  cmake
BuildRequires:  make
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  systemd-rpm-macros

Requires:       bluez
Requires:       dbus
Requires:       systemd-libs

%{?systemd_requires}

%description
A minimal RPM skeleton for the SwitchBot K10 barrel emulator.

%prep
%setup -q

%build
%cmake .
%cmake_build

%install
%cmake_install

install -D -m 0644 config/k10-barrel-emulator.toml \
    %{buildroot}%{_sysconfdir}/k10-barrel-emulator/config.toml
install -D -m 0644 packaging/systemd/k10-barrel-emulator.service \
    %{buildroot}%{_unitdir}/k10-barrel-emulator.service

%files
%doc README.md docs/ARCHITECTURE.md
%config(noreplace) %{_sysconfdir}/k10-barrel-emulator/config.toml
%{_bindir}/k10-barrel-emulatord
%{_bindir}/k10-barrel-emulatorctl
%{_unitdir}/k10-barrel-emulator.service

%post
%systemd_post k10-barrel-emulator.service

%preun
%systemd_preun k10-barrel-emulator.service

%postun
%systemd_postun_with_restart k10-barrel-emulator.service

%changelog
* Mon Jan 19 2026 Razvan <razvan@example.com> - 0.1.0-1
- Initial RPM skeleton
