lib_LTLIBRARIES = libmemcachedslab.la
libmemcachedslab_la_LDFLAGS = -version-info 1:0:0

libmemcachedslab_la_SOURCES = slabs.c slabs.h \
                              items.c items.h \
                              assoc.c assoc.h \
                              @top_srcdir@/server/util.c \
                              @top_srcdir@/server/util.h \
                              slab_engine.c slab_engine.h \
                              config_parser.c config_parser.h

CLEANFILES=

if DTRACE_INSTRUMENT_OBJ
libmemcachedslab_la_DEPENDENCIES = libslab_dtrace_probes.lo
libmemcachedslab_la_LIBADD = libslab_dtrace_probes.lo
CLEANFILES += memcached_dtrace.o memcached_debug_dtrace.o
endif

memcached_dtrace.h: memcached_dtrace.d
	${DTRACE} -h -s memcached_dtrace.d 
	sed -e 's,void \*,const void \*,g' memcached_dtrace.h | \
            sed -e 's,char \*,const char \*,g' | tr '\t' ' ' > mmc_dtrace.tmp
	mv mmc_dtrace.tmp memcached_dtrace.h

memcached_dtrace.o: $(memcached_OBJECTS)
	$(DTRACE) $(DTRACEFLAGS) -G -o memcached_dtrace.o -s ${srcdir}/memcached_dtrace.d $(memcached_OBJECTS)

memcached_debug_dtrace.o: $(memcached_debug_OBJECTS)
	$(DTRACE) $(DTRACEFLAGS) -G -o memcached_debug_dtrace.o -s ${srcdir}/memcached_dtrace.d $(memcached_debug_OBJECTS)

# So libtool doesn't support dtrace, but just copy one of the existing
# lo-file and replace the file name ;-) 
libslab_dtrace_probes.lo: libslab_dtrace_probes.o assoc.lo
	sed "s,assoc,assoc,g" assoc.lo > libslab_dtrace_probes.lo

libslab_dtrace_probes.o: $(libmemcachedslab_la_OBJECTS)
	$(DTRACE) $(DTRACEFLAGS) -o .libs/libslab_dtrace_probes.o -G -s ${srcdir}/memcached_dtrace.d `grep pic_object *.lo | cut -f 2 -d\' | grep -v non_pic_object | grep -v =none`
	$(DTRACE) $(DTRACEFLAGS) -o memcached_probes.o -G -s ${srcdir}/memcached_dtrace.d `grep non_pic_object *.lo | cut -f 2 -d\' | grep -v =none`
