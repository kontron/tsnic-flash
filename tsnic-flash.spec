Name:          tsnic-flash
Version:       %VERSION%
Release:       %SNAPSHOT%%{?dist}
Summary:       tsnic firmware update tool
License:       BSD License (two clause)
Source:        %SRC_PACKAGE_NAME%.tar.gz
BuildRequires: gcc
BuildRequires: libpciaccess-devel
Requires:      libpciaccess


%description
tsnic firmware update tool

%prep
%autosetup -n %SRC_PACKAGE_NAME%

%build

%install
%{make_install}

%files
/usr/sbin/tsnic-flash
