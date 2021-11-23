Name:       mapplauncherd
Summary:    Application launcher for fast startup
Version:    4.2.3
Release:    1
License:    LGPLv2+
URL:        https://github.com/sailfishos/mapplauncherd
Source0:    %{name}-%{version}.tar.bz2
Source1:    booster-cgroup-mount.service
Requires:   systemd-user-session-targets
Requires:   sailjail
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
Requires(pre):  sailfish-setup
BuildRequires:  extra-cmake-modules
BuildRequires:  pkgconfig(libshadowutils)
BuildRequires:  pkgconfig(systemd)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(libcap)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  cmake
Provides:   meegotouch-applauncherd > 3.0.3
Obsoletes:   meegotouch-applauncherd <= 3.0.3

%description
Application invoker and launcher daemon that speed up
application startup time and share memory. Provides also
functionality to launch applications as single instances.


%package devel
Summary:    Development files for launchable applications
Requires:   %{name} = %{version}-%{release}
Provides:   meegotouch-applauncherd-devel > 3.0.3
Obsoletes:  meegotouch-applauncherd-devel <= 3.0.3

%description devel
Development files for creating applications that can be launched
using mapplauncherd.

%package cgroup
Summary:    Service files for booster cgroup mount
Requires:   %{name} = %{version}-%{release}

%description cgroup
Scripts and services files for application launcher to mount
booster cgroup at startup.

%prep
%autosetup -n %{name}-%{version}

%build
export BUILD_TESTS=1
export MEEGO=1
unset LD_AS_NEEDED

rm -f CMakeCache.txt
%cmake
%make_build

%install
%make_install

# Don't use %exclude, remove at install phase
rm -f %{buildroot}/usr/share/fala_images/fala_qml_helloworld

mkdir -p %{buildroot}%{_userunitdir}/user-session.target.wants
ln -s ../booster-generic.service %{buildroot}%{_userunitdir}/user-session.target.wants/

mkdir -p %{buildroot}%{_datadir}/mapplauncherd/privileges.d

install -D -m 0755 %{SOURCE1} %{buildroot}%{_unitdir}/booster-cgroup-mount.service
mkdir -p %{buildroot}%{_unitdir}/multi-user.target.wants
ln -s ../booster-cgroup-mount.service %{buildroot}%{_unitdir}/multi-user.target.wants/

install -D -m 0755 scripts/booster-cgroup-mount %{buildroot}/usr/lib/startup/booster-cgroup-mount

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%license COPYING.LESSER
%dir %{_datadir}/mapplauncherd
%dir %{_datadir}/mapplauncherd/privileges.d
%{_bindir}/invoker
%{_bindir}/single-instance
%{_libdir}/libapplauncherd.so.*
%attr(2755, root, privileged) %{_libexecdir}/mapplauncherd/booster-generic
%{_userunitdir}/booster-generic.service
%{_userunitdir}/booster-generic@.service
%{_userunitdir}/user-session.target.wants/booster-generic.service

%files devel
%defattr(-,root,root,-)
%{_includedir}/applauncherd/*
%{_libdir}/libapplauncherd.so
%{_libdir}/pkgconfig/*.pc

%files cgroup
%{_unitdir}/booster-cgroup-mount.service
%{_unitdir}/multi-user.target.wants/booster-cgroup-mount.service
# Intentionally hardcoded so that this always lives in the same place
%dir /usr/lib/startup
/usr/lib/startup/booster-cgroup-mount
