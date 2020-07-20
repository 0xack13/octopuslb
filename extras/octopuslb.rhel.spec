Summary: TCP/IP Load Balancer
Name: octopuslb
Version: 1.14
Release: 1%{?dist}
License: GPLv2+
Group: System Environment/Daemons
URL: http://octopuslb.sourceforge.net/
Source: http://downloads.sourceforge.net/%{name}/%{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{name}.tar.gz
BuildRequires: net-snmp-devel >= 5.0

%description
Octopus is an extremely fast TCP load balancer with extensions 
for HTTP to allow balancing based on URI. Features include: server health 
checks and load polling, dynamic configuration, and the ability to 
carbon copy incoming requests. 

%prep
%setup -q

%build
%configure
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
make DESTDIR=%{buildroot} INSTALL="install -p" CP="cp -p" install

mkdir -p %{buildroot}/%{_initrddir}
install -m 755 %{_builddir}/%{buildsubdir}/extras/octopuslb.initd %{buildroot}/%{_initrddir}/octopuslb
mkdir -p %{buildroot}/%{_sysconfdir}/octopuslb
mv %{buildroot}/%{_sysconfdir}/octopuslb.conf %{buildroot}/%{_sysconfdir}/octopuslb/
mkdir -p %{buildroot}/%{_sysconfdir}/logrotate.d
install -m 644 %{_builddir}/%{buildsubdir}/extras/octopuslb.logrotated %{buildroot}/%{_sysconfdir}/logrotate.d/octopuslb
mkdir -p %{buildroot}/%{_localstatedir}/run/octopuslb
mkdir -p %{buildroot}/%{_localstatedir}/log/octopuslb

%{__sed} -i 's/\/var\/log/\/var\/log\/octopuslb/g' %{buildroot}/%{_sysconfdir}/octopuslb/octopuslb.conf

%clean
rm -rf %{buildroot}

%post
/sbin/chkconfig --add octopuslb

%preun
if [ $1 = 0 ]; then
	service octopuslb stop > /dev/null 2>&1
	/sbin/chkconfig --del octopuslb
fi

%files
%defattr(-,root,root,-)

%doc README CHANGELOG COPYRIGHT TODO AUTHORS COPYING

%dir %{_sysconfdir}/octopuslb

%{_sbindir}/octopuslb-server
%{_sbindir}/octopuslb-admin

%{_localstatedir}/log/octopuslb
%{_localstatedir}/run/octopuslb

%{_initrddir}/octopuslb
%config(noreplace) %{_sysconfdir}/logrotate.d/octopuslb
%config(noreplace) %{_sysconfdir}/octopuslb/octopuslb.conf

%{_mandir}/man1/*

%changelog
* Tue Nov 29 2011 Alistair Reay <alreay1@gmail.com> 1.14-1
- Upstream bump - bugfixes and minor enhancement to config file parsing

* Fri Sep 16 2011 Alistair Reay <alreay1@gmail.com> 1.13-1
- Ustream bump - bugfixes only

* Fri May 13 2011 Alistair Reay <alreay1@gmail.com> 1.12-1
- Ustream bump - bugfixes only

* Mon Mar 7 2011 Alistair Reay <alreay1@gmail.com> 1.11-2
- Removed buildrequires for 'net-snmp-libs'
- Changed buildroot to use %{} variables and removed literal from %setup section 

* Mon Feb 28 2011 Alistair Reay <alreay1@gmail.com> 1.11-1
- After fedora community feedback - changed RPM name from 'octopus' to 'octopuslb'
- Changed binary names as well as installation directories

* Tue Nov 30 2010 Alistair Reay <alreay1@gmail.com> 1.10-1
- initial configuration for Fedora 14
