Name:       buteo-sync-plugin-caldav
Summary:    Syncs calendar data from CalDAV services
Version:    0.1.26
Release:    1
Group:      System/Libraries
License:    LGPLv2.1
URL:        https://github.com/nemomobile/buteo-sync-plugin-caldav
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(mlite5)
BuildRequires:  pkgconfig(libsignon-qt5)
BuildRequires:  pkgconfig(libsailfishkeyprovider)
BuildRequires:  pkgconfig(libmkcal-qt5)
BuildRequires:  pkgconfig(libkcalcoren-qt5)
BuildRequires:  pkgconfig(buteosyncfw5)
BuildRequires:  pkgconfig(accounts-qt5)
BuildRequires:  pkgconfig(signon-oauth2plugin)
Requires: buteo-syncfw-qt5-msyncd
Requires: mkcal-qt5

%description
A Buteo plugin which syncs calendar data from CalDAV services

%files
%defattr(-,root,root,-)
#out-of-process-plugin
/usr/lib/buteo-plugins-qt5/oopp/caldav-client
#in-process-plugin
#/usr/lib/buteo-plugins-qt5/libcaldav-client.so
%config %{_sysconfdir}/buteo/profiles/client/caldav.xml
%config %{_sysconfdir}/buteo/profiles/sync/caldav-sync.xml

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 "DEFINES+=BUTEO_OUT_OF_PROCESS_SUPPORT"
make %{?jobs:-j%jobs}

%pre
rm -f /home/nemo/.cache/msyncd/sync/client/caldav.xml
rm -f /home/nemo/.cache/msyncd/sync/caldav-sync.xml

%install
rm -rf %{buildroot}
%qmake5_install

%post
echo "if you manually installed the package, you should invoke 'systemctl --user daemon-reload' and then 'systemctl --user restart msyncd'" || :
