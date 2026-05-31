# alight

`alight` is a tiny backlight control utility for Linux.

## Usage

```sh
alight                 # show current brightness (%)
alight 40              # set brightness to 40%
alight +10             # increase by 10 percentage points
alight -10             # decrease by 10 percentage points
alight -l              # list usable devices
alight -d acpi_video0  # use a specific device
```

Values are percentages from 0 to 100 and are clamped. `alight 0`
writes raw brightness 0, which can blank some displays.

By default, `alight` scans `/sys/class/backlight` and prefers devices
by type: `raw`, `platform`, `firmware`, then anything else; ties are
sorted by name. Use `-d DEVICE` or `ALIGHT_DEVICE=DEVICE` to choose
one.

## Build

```sh
make
```

## Install

```sh
doas make install
doas udevadm control --reload-rules
doas udevadm trigger --subsystem-match=backlight --settle
```

Defaults: `/usr/local/bin` and `/etc/udev/rules.d`. Override `PREFIX`,
`BINDIR`, `UDEVDIR`, or `DESTDIR` as needed.

The udev rule gives the `video` group read/write access to backlight
`brightness` files using `/bin/chgrp` and `/bin/chmod`; adjust
`90-alight.rules` if needed. Your user must be in `video`.

## Uninstall

```sh
doas make uninstall
make clean
```
