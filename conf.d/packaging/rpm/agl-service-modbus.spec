%define _prefix /opt/AGL
%define __cmake cmake
%define _app_prefix %{_prefix}/%{name}

%if 0%{?fedora_version}
%define debug_package %{nil}
%endif

Name: agl-service-modbus
Version: 1.0
Release: 1%{?dist}
Summary: Binding to serve an API connected to modbus hardware
Group:   Development/Libraries/C and C++
License:  Apache-2.0
URL: https://github.com/iotbzh/modbus-agl-binding
Source: %{name}-%{version}.tar.gz
Source2: %{name}.service
BuildRequires:  cmake
BuildRequires:  agl-cmake-apps-module
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig(json-c)
BuildRequires:  pkgconfig(lua) >= 5.3
BuildRequires:  agl-app-framework-binder-devel
BuildRequires:  agl-libafb-helpers-devel
BuildRequires:  agl-libappcontroller-devel
BuildRequires:  pkgconfig(libsystemd) >= 222
BuildRequires:  pkgconfig(libmodbus) >= 3.1.6
BuildRequires:  libtool
Requires:       agl-app-framework-binder

%description
%{name} Binding to serve an API connected to modbus hardware.

%package devel
Requires:       %{name} = %{version}
Provides:       pkgconfig(%{name}) = %{version}
Summary:        AGL modbus Binding devel
%description devel
AGL modbus Binding devel package.

%package -n modbus-plugin-kingpigeon
Summary: AGL plugin-kingpigeon
Group:          Development/Libraries/C and C++
Requires:       %{name} = %{version}
Conflicts:		plugin-raymarine-anemo
%description -n modbus-plugin-kingpigeon
plugin-kingpigeon subpackage.

%package -n modbus-plugin-raymarine-anemo
Summary: AGL  plugin-raymarine-anemo
Group:          Development/Libraries/C and C++
Requires:       %{name} = %{version}
Conflicts:		plugin-kingpigeon
%description -n modbus-plugin-raymarine-anemo
plugin-raymarine-anemo subpackage.

%prep
%autosetup -p 1


%build
#hack because of libmodbus warning conversions

%define builddir %{_builddir}/%{name}-%{version}/build

%if 0%{?fedora_version}
mkdir -p %{builddir}
pushd %{builddir}
%endif

%define __global_compiler_flags -O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -Wp,-D_GLIBCXX_ASSERTIONS -fexceptions -fstack-protector-strong -grecord-gcc-switches %{_hardened_cflags} %{_annotated_cflags}
%cmake -DVERSION=%{version} -DCMAKE_BUILD_TYPE=RELEASE -DCMAKE_INSTALL_PREFIX=%{_prefix} -DCONTROL_CONFIG_PATH=%{_prefix}/%{name}/etc ..

%if 0%{?fedora_version}
popd
%endif

%make_build -C %{builddir}

%install
%make_install -C %{builddir}
install -d %{?buildroot}

install -d %{?buildroot}%{_userunitdir}
install -m 0644 %{SOURCE2} %{?buildroot}%{_userunitdir}
install -d %{?buildroot}%{_libdir}
#example file configuration
install -d %{?buildroot}/%{_sysconfdir}/%{name}
install -m 0644 %{?buildroot}/%{_prefix}/%{name}/etc/control-modbus_kingpigeon-config.json %{?buildroot}/%{_prefix}/%{name}/etc/control-modbus.json.sample

%files
%defattr(-,root,root)
%dir %{_prefix}/%{name}
%dir %{_prefix}/%{name}/etc
%dir %{_prefix}/%{name}/lib
%dir %{_prefix}/%{name}/lib/plugins

%dir %{_prefix}/%{name}/htdocs
%{_prefix}/%{name}/htdocs/*
%{_prefix}/%{name}/lib/afb-modbus.so
%{_userunitdir}/%{basename: %SOURCE2}

%dir %{_prefix}/%{name}/var
%{_prefix}/%{name}/etc/control-modbus.json.sample

%files devel
%{_libdir}/pkgconfig/*.pc
%{_includedir}/*.h

%files -n modbus-plugin-kingpigeon
%{_prefix}/%{name}/etc/control-modbus_kingpigeon-config.json
%{_prefix}/%{name}/lib/plugins/kingpigeon.ctl.so

%files -n modbus-plugin-raymarine-anemo
%{_prefix}/%{name}/etc/control-modbus_raymarine-anemometer-config.json
%{_prefix}/%{name}/lib/plugins/raymarine-anemometer.ctl.so

%changelog
* Mon Jan 13 2020 Ronan <ronan.lemartret@iot.bzh> - 1.0
- init packaging agl-service-modbus version 1.0
