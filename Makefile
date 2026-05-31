PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
UDEVDIR ?= /etc/udev/rules.d
LICENSEDIR ?= $(PREFIX)/share/licenses/alight
CFLAGS ?= -Os -std=c99 -Wall -Wextra -pedantic

export CC CPPFLAGS CFLAGS LDFLAGS LDLIBS

UDEV_RULE = 90-alight.rules
LICENSE_FILE = LICENSE

all: alight

alight: alight.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

install: alight $(UDEV_RULE) $(LICENSE_FILE)
	install -d '$(DESTDIR)$(BINDIR)' '$(DESTDIR)$(UDEVDIR)' \
		'$(DESTDIR)$(LICENSEDIR)'
	install -m 755 alight '$(DESTDIR)$(BINDIR)/alight'
	install -m 644 $(UDEV_RULE) '$(DESTDIR)$(UDEVDIR)/$(UDEV_RULE)'
	install -m 644 $(LICENSE_FILE) '$(DESTDIR)$(LICENSEDIR)/$(LICENSE_FILE)'

uninstall:
	rm -f '$(DESTDIR)$(BINDIR)/alight' '$(DESTDIR)$(UDEVDIR)/$(UDEV_RULE)' \
		'$(DESTDIR)$(LICENSEDIR)/$(LICENSE_FILE)'

clean:
	rm -f alight

.PHONY: all install uninstall clean
