## Makefile.am -- Process this file with automake to produce Makefile.in
sbin_PROGRAMS = octopuslb-admin octopuslb-server
octopuslb_admin_SOURCES = src/admin.c src/octopus.h
octopuslb_server_SOURCES = src/octopus.c src/octopus.h 
sysconf_DATA = octopuslb.conf
man1_MANS = man/octopuslb-admin.1 man/octopuslb-server.1
EXTRA_DIST = src/algorithms.c src/config.c src/octopus.c src/init.c src/octopus.h src/monitor.c src/signals.c src/logging.c src/connect.c
EXTRA_DIST += octopuslb.conf
EXTRA_DIST += README TODO COPYRIGHT CHANGELOG extras/octopuslb.initd extras/octopuslb.fedora.spec extras/octopuslb.rhel.spec extras/octopuslb.logrotated extras/octopuslb.service
EXTRA_DIST += man/octopuslb-admin.1 man/octopuslb-server.1

