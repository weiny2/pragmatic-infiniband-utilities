INCLUDES=-Iinclude \
	-I$(includedir)/infiniband \
	-I$(includedir)

if DEBUG
DBGFLAGS = -ggdb -D_DEBUG_
else
DBGFLAGS =
endif

ACLOCAL_AMFLAGS = -I config

AM_CFLAGS = -Wall $(DBGFLAGS)

lib_LTLIBRARIES = libibedgeconf.la

sbin_PROGRAMS = src/simple_rdma \
					src/rdma_cm_query \
					src/slurm_topology \
					src/ibgraphfabric \
					src/iblinkconffromfabric \
					src/ibparseedgeconf

mySCRIPTS = scripts/ibgetgid \
				scripts/ibgetlid \
				scripts/ibtraceroute \
				scripts/ibcheckverbs \
				scripts/ibtrackerrors \
				scripts/ibcheckfabric \
				scripts/ibnodesinmcast \
				scripts/ibbouncelinks.pl \
				scripts/voltaire-create-switch-map.pl \
				scripts/voltaire-get-guids.exp \
				scripts/qlogic-create-switch-map.pl \
				scripts/qlogic-get-guids.exp \
				scripts/ibgetname \
				scripts/check_wiring_peloton.pl \
				scripts/check_wiring_coastal.pl \
				scripts/lfts_sort.pl \
				scripts/ibcreateswitchmap.sh \
				scripts/ibhcacounters \
				scripts/PIUHostlist.pm

man_MANS = man/ibbouncelinks.8 \
				man/voltaire-create-switch-map.8 \
				man/qlogic-create-switch-map.8 \
				man/ibcheckverbs.8 \
				man/slurm_topology.8 \
				man/ibtrackerrors.8 \
				man/ibhcacounters.8 \
				man/ibsrp.8 \
				man/ibparseedgeconf.8

sbin_SCRIPTS = $(mySCRIPTS)

src_simple_rdma_SOURCES = src/simple_rdma.c
src_simple_rdma_CFLAGS = -DOSM_VENDOR_INTF_OPENIB
src_simple_rdma_LDFLAGS = -losmvendor -lopensm -losmcomp -libmad -lrdmacm

src_rdma_cm_query_SOURCES = src/rdma_cm_query.c
src_rdma_cm_query_CFLAGS = -DOSM_VENDOR_INTF_OPENIB
src_rdma_cm_query_LDFLAGS = -losmvendor -lopensm -losmcomp -libmad -lrdmacm

src_slurm_topology_SOURCES = src/slurm_topology.c src/hostlist.c src/hostlist.h
src_slurm_topology_LDFLAGS = -losmcomp -libmad -libnetdisc $(LIBGENDERS)

src_ibgraphfabric_SOURCES = src/ibgraphfabric.c src/hostlist.c src/hostlist.h
src_ibgraphfabric_LDFLAGS = -losmcomp -libmad -libnetdisc

src_iblinkconffromfabric_SOURCES = src/iblinkconffromfabric.c
src_iblinkconffromfabric_LDFLAGS = -losmcomp -libmad -libnetdisc

libibedgeconf_la_SOURCES = src/ibedgeconf.c
libibedgeconf_la_CFLAGS = -I/usr/include/libxml2 -Wall $(DBGFLAGS)
libibedgeconf_la_LDFLAGS = -version-info $(ibedgeconf_api_version) \
	-export-dynamic $(libibedgeconf_version_script) \
	-libmad -lxml2 -lz -lm
libibedgeconf_la_DEPENDENCIES = src/libibedgeconf.map

libibedgeconfincludedir = $(includedir)/infiniband

libibedgeconfinclude_HEADERS = include/infiniband/ibedgeconf.h

src_ibparseedgeconf_SOURCES = src/ibparseedgeconf.c
src_ibparseedgeconf_CFLAGS = -I$(includedir)
src_ibparseedgeconf_LDFLAGS = -libedgeconf


EXTRA_DIST = \
	META \
	ChangeLog \
	pragmatic-infiniband-utilities.spec \
	$(man_MANS) \
	$(mySCRIPTS) \
	scripts \
	sysconf/ibsrp.conf \
	etc/pragmaticIB.conf \
	chassis_edgeconf \
	src/libibedgeconf.map \
	libibedgeconf.ver

install-exec-hook:
	$(top_srcdir)/config/install-sh -m 755 -d $(DESTDIR)/$(sysconfdir)/init.d
	$(top_srcdir)/config/install-sh -m 755 $(top_builddir)/scripts/ibsrp $(DESTDIR)/$(sysconfdir)/init.d/ibsrp
	$(top_srcdir)/config/install-sh -m 755 $(top_builddir)/sysconf/ibsrp.conf $(DESTDIR)/$(sysconfdir)/sysconfig/ibsrp.conf
	$(top_srcdir)/config/install-sh -m 644 $(top_builddir)/etc/pragmaticIB.conf $(DESTDIR)/$(sysconfdir)/pragmaticIB/pragmaticIB.conf
	for file in $(top_builddir)/chassis_edgeconf/*.xml; do \
		$(top_srcdir)/config/install-sh -m 644 $$file $(DESTDIR)/$(sysconfdir)/pragmaticIB/; \
	done

