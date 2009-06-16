sbin_PROGRAMS = memcached
BUILT_SOURCES=

memcached_SOURCES = memcached.c memcached.h \
                    hash.c hash.h \
                    slabs.c slabs.h \
                    items.c items.h \
                    assoc.c assoc.h \
                    thread.c daemon.c \
                    stats.c stats.h \
                    util.c util.h \
                    trace.h cache.h

if BUILD_CACHE
memcached_SOURCES += cache.c
endif

if BUILD_SOLARIS_PRIVS
memcached_SOURCES += solaris_priv.c
endif

memcached_LDADD=
memcached_DEPENDENCIES=
CLEANFILES=

if BUILD_DTRACE
BUILT_SOURCES += memcached_dtrace.h
CLEANFILES += memcached_dtrace.h
endif

if DTRACE_INSTRUMENT_OBJ
memcached_LDADD += memcached_dtrace.o
memcached_DEPENDENCIES += memcached_dtrace.o
CLEANFILES += memcached_dtrace.o
endif

memcached_dtrace.h: memcached_dtrace.d
	${DTRACE} -h -s memcached_dtrace.d 
	sed -e 's,void \*,const void \*,g' memcached_dtrace.h | \
            sed -e 's,char \*,const char \*,g' | tr '\t' ' ' > mmc_dtrace.tmp
	mv mmc_dtrace.tmp memcached_dtrace.h

memcached_dtrace.o: $(memcached_OBJECTS)
	$(DTRACE) $(DTRACEFLAGS) -G -o memcached_dtrace.o -s ${srcdir}/memcached_dtrace.d $(memcached_OBJECTS)
