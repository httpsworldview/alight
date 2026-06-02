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

enum { NAME_LEN = 256, TYPE_LEN = 32, LINE_LEN = 64 };

struct light {
	char name[NAME_LEN], type[TYPE_LEN];
	long now, max;
};

static int fail(int error) {
	errno = error ? error : EIO;
	return -1;
}

static int usage(FILE *out, int code) {
	fputs("usage: alight [-d DEVICE] [N|+N|-N]\n"
	      "       alight -l\n"
	      "       alight -h\n",
	      out);
	return code;
}

static long clamp(long value, long min, long max) {
	return value < min ? min : value > max ? max : value;
}

static long percent(long raw, long max) {
	return (long)((long double)clamp(raw, 0, max) * 100 / max + 0.5L);
}

static long raw_percent(long value, long max) {
	value = clamp(value, 0, 100);
	return (max / 100) * value + ((max % 100) * value + 50) / 100;
}

static long target_raw(const struct light *light, const char *arg, long value) {
	long now = clamp(light->now, 0, light->max);

	if (*arg == '+')
		return value >= 100
		           ? light->max
		           : clamp(now + raw_percent(value, light->max), 0,
		                   light->max);
	if (*arg == '-')
		return value <= -100
		           ? 0
		           : clamp(now - raw_percent(-value, light->max), 0,
		                   light->max);
	return raw_percent(value, light->max);
}

static int valid_name(const char *name) {
	return *name && strcmp(name, ".") && strcmp(name, "..") &&
	       strlen(name) < NAME_LEN && !strchr(name, '/');
}

static FILE *open_attr(const char *dev, const char *attr, const char *mode) {
	char path[sizeof SYSFS + NAME_LEN + sizeof "/max_brightness"];
	int len = snprintf(path, sizeof path, "%s/%s/%s", SYSFS, dev, attr);

	if (len < 0 || len >= (int)sizeof path) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	return fopen(path, mode);
}

static int close_as(FILE *file, int error) {
	if (fclose(file) && !error)
		error = errno ? errno : EIO;
	return error ? fail(error) : 0;
}

static int read_attr(const char *dev, const char *attr, char *buf, int len) {
	FILE *file = open_attr(dev, attr, "r");
	int error = 0;

	if (!file)
		return -1;
	errno = 0;
	if (!fgets(buf, len, file))
		error = ferror(file) ? (errno ? errno : EIO) : EINVAL;
	else if (!strchr(buf, '\n') && !feof(file))
		error = ENAMETOOLONG;
	else
		buf[strcspn(buf, "\n")] = 0;
	return close_as(file, error);
}

static int parse_number(const char *text, long *value) {
	char *end;

	errno = 0;
	if ((unsigned char)*text <= ' ')
		return fail(EINVAL);
	*value = strtol(text, &end, 10);
	return errno || end == text || *end ? fail(errno ? errno : EINVAL) : 0;
}

static int read_number(const char *dev, const char *attr, long *value) {
	char buf[LINE_LEN];

	if (read_attr(dev, attr, buf, sizeof buf) || parse_number(buf, value))
		return -1;
	return *value < 0 ? fail(EINVAL) : 0;
}

static int write_number(const char *dev, const char *attr, long value) {
	FILE *file = open_attr(dev, attr, "w");
	int error = 0;

	if (!file)
		return -1;
	errno = 0;
	if (fprintf(file, "%ld\n", value) < 0)
		error = errno ? errno : EIO;
	return close_as(file, error);
}

static int type_rank(const char *type) {
	if (!strcmp(type, "firmware"))
		return 0;
	if (!strcmp(type, "platform"))
		return 1;
	if (!strcmp(type, "raw"))
		return 2;
	return 3;
}

static int load_light(struct light *light, const char *name) {
	if (!valid_name(name))
		return fail(EINVAL);
	strcpy(light->name, name);
	if (read_number(name, "max_brightness", &light->max) || light->max <= 0)
		return fail(errno ? errno : EINVAL);
	if (read_number(name, "brightness", &light->now))
		return -1;
	if (read_attr(name, "type", light->type, sizeof light->type))
		strcpy(light->type, "unknown");
	return 0;
}

static int better_light(const struct light *a, const struct light *b) {
	int ar = type_rank(a->type), br = type_rank(b->type);
	return ar < br || (ar == br && strcmp(a->name, b->name) < 0);
}

static void print_light(const struct light *light) {
	printf("%s\t%ld%%\t%ld/%ld\t%s\n", light->name,
	       percent(light->now, light->max), light->now, light->max,
	       light->type);
}

static void scan_lights(struct light *best) {
	DIR *dir = opendir(SYSFS);
	struct dirent *entry;
	struct light light;
	int found = 0;

	if (!dir)
		err(1, "%s", SYSFS);
	for (errno = 0; (entry = readdir(dir)); errno = 0) {
		if (load_light(&light, entry->d_name))
			continue;
		if (!best)
			print_light(&light);
		else if (!found || better_light(&light, best))
			*best = light;
		found = 1;
	}
	if (errno)
		err(1, "%s", SYSFS);
	if (closedir(dir))
		err(1, "%s", SYSFS);
	if (!found)
		errx(1, "no backlight in %s", SYSFS);
}

int main(int argc, char **argv) {
	struct light light;
	const char *device = getenv("ALIGHT_DEVICE"), *value_arg = NULL;
	long value = 0;
	int list = 0, device_arg = 0;

	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		char option = *arg == '-' && arg[1] && !arg[2] ? arg[1] : 0;

		if (option == 'h')
			return usage(stdout, 0);
		if (option == 'l') {
			list = 1;
			continue;
		}
		if (option == 'd' && ++i == argc)
			return usage(stderr, 2);
		if (option == 'd') {
			device = argv[i];
			device_arg = 1;
			continue;
		}
		if (value_arg)
			return usage(stderr, 2);
		value_arg = arg;
	}
	if (list && (value_arg || device_arg))
		return usage(stderr, 2);
	if (value_arg && parse_number(value_arg, &value))
		return usage(stderr, 2);
	if (list) {
		scan_lights(NULL);
		return 0;
	}
	if (!device_arg && (!device || !*device))
		scan_lights(&light);
	else if (load_light(&light, device))
		err(1, "%s", device);
	if (!value_arg) {
		printf("%ld\n", percent(light.now, light.max));
		return 0;
	}
	if (write_number(light.name, "brightness",
	                 target_raw(&light, value_arg, value)))
		err(1, "%s/brightness", light.name);
	return 0;
}
