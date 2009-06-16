noinst_PROGRAMS = sizes testapp
testapp_SOURCES = testapp.c \
                  $(top_srcdir)/server/util.c \
                  $(top_srcdir)/server/util.h

if BUILD_CACHE
testapp_SOURCES += $(top_srcdir)/server/cache.c
endif

MOSTLYCLEANFILES = *.gcov *.gcno *.gcda *.tcov

test:	sizes testapp
	$(srcdir)/sizes
	$(srcdir)/testapp
	prove $(srcdir)/t

report:
	@if test `basename $(PROFILER)` = "gcov"; then \
	  for file in memcached-*.gc??; do \
	    mv -f $$file `echo $$file | sed 's/memcached-//'`; \
	  done && \
	  for file in *.gcda; do \
	    srcfile=`echo $$file | sed 's/.gcda/.c/'`; \
	    if test -f "$(top_srcdir)/server/$$srcfile"; then \
	      echo `$(PROFILER) $$srcfile` | sed 's/'$$srcfile':.*//'; \
	    fi \
	  done \
	elif test `basename $(PROFILER)` = "tcov"; then \
	  files=`grep SRCFILE memcached.profile/tcovd | sed 's/SRCFILE://' | sort | uniq` && \
	  $(PROFILER) -x memcached.profile $$files 2>&1; \
	  for file in *.tcov; do \
	    srcfile=`echo $$file | sed 's/.tcov//'`; \
	    if test -f "$(top_srcdir)/server/$$srcfile"; then \
	      echo $$srcfile : `grep 'Percent of the file executed' $$file`; \
	    fi \
	  done \
	else :; fi
