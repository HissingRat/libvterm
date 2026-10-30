ifeq ($(shell uname),Darwin)
  LIBTOOL ?= glibtool
else
  LIBTOOL ?= libtool
endif

ifneq ($(VERBOSE),1)
  LIBTOOL +=--quiet
endif

override CFLAGS +=-Wall -Iinclude -std=c99 -Wpedantic

ifeq ($(shell uname),SunOS)
  override CFLAGS +=-D__EXTENSIONS__ -D_XPG6 -D__XOPEN_OR_POSIX
endif

ifeq ($(DEBUG),1)
  override CFLAGS +=-ggdb -DDEBUG
endif

ifeq ($(PROFILE),1)
  override CFLAGS +=-pg
  override LDFLAGS+=-pg
endif

CFILES=$(sort $(wildcard src/*.c src/lib/*.c))
HFILES=$(sort $(wildcard include/*.h))
OBJECTS=$(CFILES:.c=.lo)
LIBRARY=libvterm.la

BINFILES_SRC=$(sort $(wildcard bin/*.c))
BINFILES=$(BINFILES_SRC:.c=)

TBLFILES=$(sort $(wildcard src/encoding/*.tbl))
INCFILES=$(TBLFILES:.tbl=.inc)

HFILES_INT=$(sort $(wildcard src/*.h src/lib/*.h)) $(HFILES)
HOST_HEADERS=support/host.h

VERSION_CURRENT=1
VERSION_REVISION=0
VERSION_AGE=0

VERSION=0.4.0

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
LIBDIR=$(PREFIX)/lib
INCDIR=$(PREFIX)/include
MANDIR=$(PREFIX)/share/man
MAN3DIR=$(MANDIR)/man3

all: $(LIBRARY) $(BINFILES)

$(LIBRARY): $(OBJECTS)
	@echo LINK $@
	@$(LIBTOOL) --mode=link --tag=CC $(CC) -rpath $(LIBDIR) -version-info $(VERSION_CURRENT):$(VERSION_REVISION):$(VERSION_AGE) -o $@ $^ $(LDFLAGS)

src/%.lo: src/%.c $(HFILES_INT)
	@echo CC $<
	@$(LIBTOOL) --mode=compile --tag=CC $(CC) $(CFLAGS) -o $@ -c $<

src/encoding/%.inc: src/encoding/%.tbl
	@echo TBL $<
	@perl -CSD tbl2inc_c.pl $< >$@

src/fullwidth.inc:
	@perl find-wide-chars.pl >$@

src/encoding.lo: $(INCFILES)

bin/%: bin/%.c $(LIBRARY) $(HOST_HEADERS)
	@echo CC $<
	@$(LIBTOOL) --mode=link --tag=CC $(CC) $(CFLAGS) -o $@ $< $(LIBRARY) $(LDFLAGS)

t/harness.lo: t/harness.c $(HFILES_INT) $(HOST_HEADERS)
	@echo CC $<
	@$(LIBTOOL) --mode=compile --tag=CC $(CC) $(CFLAGS) -o $@ -c $<

t/harness: t/harness.lo $(LIBRARY)
	@echo LINK $@
	@$(LIBTOOL) --mode=link --tag=CC $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: test
test: $(LIBRARY) t/harness test-utils test-output test-errors
	for T in `ls t/[0-9]*.test`; do echo "** $$T **"; perl t/run-test.pl $$T $(if $(VALGRIND),--valgrind) || exit 1; done

t/utils: t/utils.c src/lib/utils.c src/lib/utils.h
	$(CC) $(CFLAGS) -Isrc -o $@ t/utils.c src/lib/utils.c $(LDFLAGS)

.PHONY: test-utils test-output test-errors test-native freestanding
test-utils: t/utils
	./t/utils

t/output: t/output.c $(CFILES) $(HFILES_INT) $(INCFILES) $(HOST_HEADERS)
	$(CC) $(CFLAGS) -o $@ t/output.c $(CFILES) $(LDFLAGS)

test-output: t/output
	./t/output

t/errors: t/errors.c $(CFILES) $(HFILES_INT) $(INCFILES)
	$(CC) $(CFLAGS) -o $@ t/errors.c $(CFILES) $(LDFLAGS)

test-errors: t/errors
	./t/errors

FREESTANDING_CFLAGS ?= -O2 -ffreestanding -fno-stack-protector
FS_OBJECTS=$(patsubst %.c,.freestanding/%.o,$(CFILES))

freestanding: libvterm.a

libvterm.a: $(FS_OBJECTS)
	$(AR) rcs $@ $^

.freestanding/%.o: %.c $(HFILES_INT) $(INCFILES)
	@mkdir -p $(dir $@)
	$(CC) -std=c99 -Iinclude $(FREESTANDING_CFLAGS) -c $< -o $@

# Run the same suite without requiring GNU libtool on the development host.
t/harness-native: t/harness.c $(CFILES) $(HFILES_INT) $(INCFILES) $(HOST_HEADERS)
	$(CC) $(CFLAGS) -o $@ t/harness.c $(CFILES) $(LDFLAGS)

test-native: t/harness-native test-utils test-output test-errors
	@for T in t/[0-9]*.test; do echo "** $$T **"; perl t/run-test.pl --executable ./t/harness-native $$T || exit 1; done

.PHONY: clean
clean:
	$(LIBTOOL) --mode=clean rm -f $(OBJECTS) $(INCFILES)
	$(LIBTOOL) --mode=clean rm -f t/harness.lo t/harness
	$(LIBTOOL) --mode=clean rm -f $(LIBRARY) $(BINFILES)
	rm -f t/utils t/output t/errors t/freestanding t/harness-native libvterm.a
	rm -rf .freestanding

.PHONY: install
install: install-inc install-lib install-bin

install-inc:
	install -d $(DESTDIR)$(INCDIR)
	install -m644 $(HFILES) $(DESTDIR)$(INCDIR)
	install -d $(DESTDIR)$(LIBDIR)/pkgconfig
	sed -e "s,@INCDIR@,$(INCDIR)," -e "s,@LIBDIR@,$(LIBDIR)," -e "s,@VERSION@,$(VERSION)," <vterm.pc.in >$(DESTDIR)$(LIBDIR)/pkgconfig/vterm.pc

install-lib: $(LIBRARY)
	install -d $(DESTDIR)$(LIBDIR)
	$(LIBTOOL) --mode=install install $(LIBRARY) $(DESTDIR)$(LIBDIR)/$(LIBRARY)
	$(LIBTOOL) --mode=finish $(DESTDIR)$(LIBDIR)

install-bin: $(BINFILES)
	install -d $(DESTDIR)$(BINDIR)
	$(LIBTOOL) --mode=install install $(BINFILES) $(DESTDIR)$(BINDIR)/

# DIST CUT

DISTDIR=libvterm-$(VERSION)

distdir: $(INCFILES)
	mkdir __distdir
	cp LICENSE CONTRIBUTING README.md __distdir
	mkdir __distdir/src
	cp src/*.c src/*.h src/*.inc __distdir/src
	mkdir __distdir/src/lib
	cp src/lib/*.c src/lib/*.h __distdir/src/lib
	mkdir __distdir/src/encoding
	cp src/encoding/*.inc __distdir/src/encoding
	mkdir __distdir/include
	cp include/*.h __distdir/include
	mkdir __distdir/bin
	cp bin/*.c __distdir/bin
	mkdir __distdir/t
	cp t/*.test t/harness.c t/utils.c t/output.c t/errors.c t/freestanding.c t/run-test.pl __distdir/t
	mkdir __distdir/support
	cp support/host.h __distdir/support
	sed "s,@VERSION@,$(VERSION)," <vterm.pc.in >__distdir/vterm.pc.in
	sed "/^# DIST CUT/Q" <Makefile >__distdir/Makefile
	mv __distdir $(DISTDIR)

TARBALL=$(DISTDIR).tar.gz

dist: distdir
	tar -czf $(TARBALL) $(DISTDIR)
	rm -rf $(DISTDIR)

dist+bzr:
	$(MAKE) dist VERSION=$(VERSION)+bzr`bzr revno`

distdir+bzr:
	$(MAKE) distdir VERSION=$(VERSION)+bzr`bzr revno`
