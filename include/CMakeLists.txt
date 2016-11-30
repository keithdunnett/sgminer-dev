cmake_minimum_required (VERSION 2.6)

#OBJS=libut.a
#all: $(OBJS) 
#INCDIR=./include
#CFLAGS+=-I$(INCDIR)
#CFLAGS+=-Wall -Wextra
#CFLAGS+=-g

#libut.a: libut.o utvector.o utmm.o ringbuf.o
#	ar r $@ $^

#libut.o: src/libut.c $(INCDIR)/libut.h
#	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

#utvector.o: src/utvector.c $(INCDIR)/utvector.h
#	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<
#
#utmm.o: src/utmm.c $(INCDIR)/utmm.h
#	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<
#
#ringbuf.o: src/ringbuf.c $(INCDIR)/ringbuf.h
#	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<
#
#.PHONY: clean tests install
#
#clean:
#	rm -f $(OBJS) *.o
#	make -C tests clean
#
#tests: libut.a
#	make -C tests 
#
#install: libut.a
#	cp $< /usr/local/lib
#
#distclean:
#	rm -f $(OBJS) *.o
#	make -C tests clean


include_directories(uthash/libut/include)
add_library(ut uthash/libut/src/libut.c uthash/libut/include/libut.h uthash/libut/src/utvector.c uthash/libut/include/utvector.h uthash/libut/src/utmm.c uthash/libut/include/utmm.h uthash/libut/src/ringbuf.c uthash/libut/include/ringbuf.h)
target_include_directories(ut PUBLIC $CMAKE_CURRENT_SOURCE_DIR)
