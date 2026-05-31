PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
UDEVDIR ?= /etc/udev/rules.d
CFLAGS ?= -Os -std=c99 -Wall -Wextra -pedantic

export CC CPPFLAGS CFLAGS LDFLAGS LDLIBS

UDEV_RULE = 90-alight.rules

all: alight

alight: alight.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

install: alight $(UDEV_RULE)
	install -d '$(DESTDIR)$(BINDIR)' '$(DESTDIR)$(UDEVDIR)'
	install -m 755 alight '$(DESTDIR)$(BINDIR)/alight'
	install -m 644 $(UDEV_RULE) '$(DESTDIR)$(UDEVDIR)/$(UDEV_RULE)'

uninstall:
	rm -f '$(DESTDIR)$(BINDIR)/alight' '$(DESTDIR)$(UDEVDIR)/$(UDEV_RULE)'

clean:
	rm -f alight

.PHONY: all install uninstall clean
