/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Maika Namuo
 */

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SYSFS
#define SYSFS "/sys/class/backlight"
#endif

enum { NAME_LEN = 256, TYPE_LEN = 32, PATH_LEN = 512, NUM_LEN = 64 };

struct light {
	char name[NAME_LEN], type[TYPE_LEN];
	long now, max;
	int rank;
};

static int fail(int error) {
	errno = error ? error : EIO;
	return -1;
}

static int usage(FILE *out, int code) {
	fputs("usage: alight [-d DEVICE] [N|+N|-N]\n"
		"       alight -l\n"
		"       alight -h\n", out);
	return code;
}

static long clamp(long value, long max) {
	return value < 0 ? 0 : value > max ? max : value;
}

static long percent(long value, long max) {
	return (long)((long double)clamp(value, max) * 100 / max + 0.5L);
}

static long percent_to_raw(long value, long max) {
	return max / 100 * value + (max % 100 * value + 50) / 100;
}

static long relative_raw(long now, long max, long value) {
	long delta;

	now = clamp(now, max);
	if (value >= 100) return max;
	if (value <= -100) return 0;
	if (value >= 0) {
		delta = percent_to_raw(value, max);
		return now > max - delta ? max : now + delta;
	}
	delta = percent_to_raw(-value, max);
	return now < delta ? 0 : now - delta;
}

static int valid_name(const char *name) {
	return name && *name && strcmp(name, ".") && strcmp(name, "..") &&
		strlen(name) < NAME_LEN && !strchr(name, '/');
}

static FILE *open_attr(const char *dev, const char *attr, const char *mode) {
	char path[PATH_LEN];
	int len = snprintf(path, sizeof path, "%s/%s/%s", SYSFS, dev, attr);

	if (len < 0 || len >= (int)sizeof path) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	return fopen(path, mode);
}

static int close_as(FILE *file, int error) {
	if (fclose(file) && !error) error = errno ? errno : EIO;
	return error ? fail(error) : 0;
}

static int read_attr(const char *dev, const char *attr, char *buf, int len) {
	FILE *file = open_attr(dev, attr, "r");
	int error = 0;

	if (!file) return -1;
	errno = 0;
	if (fgets(buf, len, file))
		buf[strcspn(buf, "\n")] = 0;
	else
		error = ferror(file) ? (errno ? errno : EIO) : EINVAL;
	return close_as(file, error);
}

static int parse_number(const char *text, long *value) {
	char *end;

	errno = 0;
	if (!text || (unsigned char)*text <= ' ') return -1;
	*value = strtol(text, &end, 10);
	return errno || end == text || *end ? -1 : 0;
}

static int read_number(const char *dev, const char *attr, long *value) {
	char buf[NUM_LEN];

	if (read_attr(dev, attr, buf, sizeof buf) || parse_number(buf, value))
		return fail(errno ? errno : EINVAL);
	return *value < 0 ? fail(EINVAL) : 0;
}

static int write_number(const char *dev, const char *attr, long value) {
	FILE *file = open_attr(dev, attr, "w");
	int error = 0;

	if (!file) return -1;
	errno = 0;
	if (fprintf(file, "%ld\n", value) < 0) error = errno ? errno : EIO;
	return close_as(file, error);
}

static int type_rank(const char *type) {
	return !strcmp(type, "raw") ? 0 : !strcmp(type, "platform") ? 1 :
		!strcmp(type, "firmware") ? 2 : 3;
}

static int load_light(struct light *light, const char *name) {
	if (!valid_name(name)) return fail(EINVAL);
	strcpy(light->name, name);
	if (read_number(name, "max_brightness", &light->max) || light->max <= 0)
		return fail(errno ? errno : EINVAL);
	if (read_number(name, "brightness", &light->now)) return -1;
	if (read_attr(name, "type", light->type, sizeof light->type))
		strcpy(light->type, "unknown");
	light->rank = type_rank(light->type);
	return 0;
}

static int better_light(const struct light *candidate,
	const struct light *best) {
	return !*best->name || candidate->rank < best->rank ||
		(candidate->rank == best->rank &&
		strcmp(candidate->name, best->name) < 0);
}

static void print_light(const struct light *light) {
	printf("%s\t%ld%%\t%ld/%ld\t%s\n",
		light->name, percent(light->now, light->max),
		light->now, light->max, light->type);
}

static void scan_lights(struct light *best) {
	DIR *dir = opendir(SYSFS);
	struct dirent *entry;
	struct light candidate;
	int found = 0;

	if (!dir) err(1, "%s", SYSFS);
	if (best) *best->name = 0;
	for (errno = 0; (entry = readdir(dir)); errno = 0) {
		if (load_light(&candidate, entry->d_name)) continue;
		found = 1;
		if (!best) print_light(&candidate);
		else if (better_light(&candidate, best)) *best = candidate;
	}
	if (errno) err(1, "%s", SYSFS);
	if (closedir(dir)) err(1, "%s", SYSFS);
	if (!found) errx(1, "no backlight in %s", SYSFS);
}

int main(int argc, char **argv) {
	struct light light;
	const char *device = getenv("ALIGHT_DEVICE"), *value_arg = NULL;
	long value;
	int list = 0, device_arg = 0;

	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "-h")) return usage(stdout, 0);
		if (!strcmp(arg, "-l")) { list = 1; continue; }
		if (!strcmp(arg, "-d")) {
			if (++i == argc) return usage(stderr, 2);
			device = argv[i];
			device_arg = 1;
			continue;
		}
		if (value_arg) return usage(stderr, 2);
		value_arg = arg;
	}
	if (list) {
		if (value_arg || device_arg) return usage(stderr, 2);
		scan_lights(NULL);
		return 0;
	}
	if (value_arg && parse_number(value_arg, &value)) return usage(stderr, 2);
	if (!device_arg && (!device || !*device))
		scan_lights(&light);
	else if (load_light(&light, device))
		err(1, "%s", device);
	if (value_arg) {
		if (write_number(light.name, "brightness",
			*value_arg == '+' || *value_arg == '-' ?
			relative_raw(light.now, light.max, value) :
			percent_to_raw(clamp(value, 100), light.max)))
			err(1, "%s/brightness", light.name);
		return 0;
	}
	printf("%ld\n", percent(light.now, light.max));
	return 0;
}
