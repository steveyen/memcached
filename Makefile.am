pkginclude_HEADERS = include/memcached/protocol_binary.h
SUBDIRS = doc plugin server tests
DIST_DIRS = scripts
EXTRA_DIST = doc server tests scripts TODO memcached.spec version.m4

test: all
	@(cd tests; ${MAKE} test)
