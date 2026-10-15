# Harpo's Coat - a cartoon screensaver: Harpo Marx and the bottomless coat
VERSION  := 1.0.0
PREFIX   ?= /usr
SYSCONFDIR ?= /etc
DESTDIR  ?=
CC       ?= cc
# gnu11 keeps the binary free of very recent glibc symbols, so packages built on a
# new distribution still run on older ones
CFLAGS   ?= -O2 -g
override CFLAGS += -std=gnu11 -Wall -Isrc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)

MINGW    ?= x86_64-w64-mingw32
MINGW_ROOT ?= /usr/$(MINGW)/sys-root/mingw

all: build/harpos-coat

# the pictures in assets/art are built into the program, so it must be compiled from here
build/harpos-coat: src/harposcoat.c src/stb_image.h $(wildcard assets/art/*)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) -o $@ src/harposcoat.c $(LDFLAGS) $(SDL_LIBS) -lm

check: build/harpos-coat
	SDL_VIDEODRIVER=dummy ./build/harpos-coat --snapshot build/check.bmp --time 5 --size 640x400 && echo "it runs"

install: all
	install -Dm755 build/harpos-coat $(DESTDIR)$(PREFIX)/bin/harpos-coat
	install -Dm755 packaging/linux/harpos-coat-screensaver $(DESTDIR)$(PREFIX)/bin/harpos-coat-screensaver
	install -Dm644 packaging/linux/harpos-coat.desktop $(DESTDIR)$(PREFIX)/share/applications/harpos-coat.desktop
	install -Dm644 packaging/linux/harpos-coat-saver.desktop $(DESTDIR)$(PREFIX)/share/screensaver-service/savers/harpos-coat.desktop
	install -Dm644 packaging/linux/harpos-coat-autostart.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/harpos-coat-screensaver.desktop
	install -Dm644 packaging/harpos-coat.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/harpos-coat.png
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/harpos-coat/README.md
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/doc/harpos-coat/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/harpos-coat $(DESTDIR)$(PREFIX)/bin/harpos-coat-screensaver
	rm -f $(DESTDIR)$(PREFIX)/share/applications/harpos-coat.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/harpos-coat-screensaver.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/screensaver-service/savers/harpos-coat.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/harpos-coat.png
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/harpos-coat

# ---- packages (written to dist/) ----
ARCH_DEB := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)

deb: all
	rm -rf build/deb && mkdir -p build/deb/DEBIAN dist
	$(MAKE) install DESTDIR=build/deb PREFIX=/usr
	strip build/deb/usr/bin/harpos-coat
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@ARCH@/$(ARCH_DEB)/' \
	    -e "s/@SIZE@/$$(du -sk build/deb/usr | cut -f1)/" packaging/linux/control.in > build/deb/DEBIAN/control
	echo /etc/xdg/autostart/harpos-coat-screensaver.desktop > build/deb/DEBIAN/conffiles
	dpkg-deb --root-owner-group --build build/deb dist/harpos-coat_$(VERSION)_$(ARCH_DEB).deb

rpm:
	mkdir -p dist build/rpm/SOURCES
	git archive --format=tar.gz --prefix=harpos-coat-$(VERSION)/ -o build/rpm/SOURCES/harpos-coat-$(VERSION).tar.gz HEAD
	rpmbuild -bb $(RPMFLAGS) --define "_topdir $(CURDIR)/build/rpm" --define "version $(VERSION)" packaging/linux/harpos-coat.spec
	cp build/rpm/RPMS/*/harpos-coat-$(VERSION)-*.rpm dist/

# Windows: the screensaver itself, and a small HarposCoat.scr that goes in the
# Windows system folder (so it is listed with the other screensavers) and starts it
windows:
	@mkdir -p build/win
	$(MINGW)-gcc -O2 -std=gnu11 -Wall -Isrc -I$(MINGW_ROOT)/include/SDL2 -Dmain=SDL_main \
	    -o build/win/harpos-coat.exe src/harposcoat.c \
	    -L$(MINGW_ROOT)/lib -lmingw32 -lSDL2main -lSDL2 -lm -mwindows -static-libgcc
	$(MINGW)-windres packaging/windows/stub.rc -O coff -o build/win/stub.res
	$(MINGW)-gcc -O2 -Wall -o build/win/HarposCoat.scr packaging/windows/stub.c build/win/stub.res -mwindows -static
	$(MINGW)-strip build/win/harpos-coat.exe build/win/HarposCoat.scr
	cp $(MINGW_ROOT)/bin/SDL2.dll build/win/
	[ ! -f $(MINGW_ROOT)/bin/SDL3.dll ] || cp $(MINGW_ROOT)/bin/SDL3.dll build/win/

installer: windows
	mkdir -p dist
	makensis -V2 -DVERSION=$(VERSION) packaging/windows/harpos-coat.nsi

dist: deb rpm installer

clean:
	rm -rf build dist

.PHONY: all check install uninstall deb rpm windows installer dist clean
