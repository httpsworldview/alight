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
enum command { CMD_HELP, CMD_GET, CMD_SET, CMD_RELATIVE, CMD_LIST };

struct light {
	char name[NAME_LEN], type[TYPE_LEN];
	long now, max;
	int rank;
};

struct options {
	enum command command;
	const char *device;
	long value;
	int device_arg;
};

static int fail(int error) { errno = error ? error : EIO; return -1; }

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

static int path_to(char path[PATH_LEN], const char *dev, const char *attr) {
	int len = snprintf(path, PATH_LEN, "%s/%s/%s", SYSFS, dev, attr);
	return len < 0 || len >= PATH_LEN ? fail(ENAMETOOLONG) : 0;
}

static int close_as(FILE *file, int error) {
	if (fclose(file) && !error) error = errno ? errno : EIO;
	return error ? fail(error) : 0;
}

static int read_attr(const char *dev, const char *attr, char *buf, int len) {
	char path[PATH_LEN];
	FILE *file;
	int error = 0;

	if (path_to(path, dev, attr)) return -1;
	if (!(file = fopen(path, "r"))) return -1;
	if (!fgets(buf, len, file))
		error = ferror(file) ? (errno ? errno : EIO) : EINVAL;
	if (!error) buf[strcspn(buf, "\n")] = 0;
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
	char path[PATH_LEN];
	FILE *file;
	int error = 0;

	if (path_to(path, dev, attr)) return -1;
	if (!(file = fopen(path, "w"))) return -1;
	errno = 0;
	if (fprintf(file, "%ld\n", value) < 0) error = errno ? errno : EIO;
	return close_as(file, error);
}

static int type_rank(const char *type) {
	static const char *const order[] = { "raw", "platform", "firmware" };

	for (int i = 0; i < 3; i++)
		if (!strcmp(type, order[i]))
			return i;
	return 3;
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

static void scan_lights(struct light *best) {
	DIR *dir = opendir(SYSFS);
	struct dirent *entry;
	struct light candidate;
	int found = 0;

	if (!dir) err(1, "%s", SYSFS);
	if (best) *best->name = 0;
	for (errno = 0; (entry = readdir(dir)); errno = 0) {
		if (load_light(&candidate, entry->d_name)) continue;
		if (best) {
			if (better_light(&candidate, best)) *best = candidate;
		} else {
			printf("%s\t%ld%%\t%ld/%ld\t%s\n",
				candidate.name, percent(candidate.now, candidate.max),
				candidate.now, candidate.max, candidate.type);
		}
		found = 1;
	}
	if (errno) err(1, "%s", SYSFS);
	if (closedir(dir)) err(1, "%s", SYSFS);
	if (!found) errx(1, "no backlight in %s", SYSFS);
}

static int parse_options(int argc, char **argv, struct options *opts) {
	const char *value_arg = NULL;
	int list = 0;

	*opts = (struct options){ CMD_GET, getenv("ALIGHT_DEVICE"), 0, 0 };
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "-h")) { opts->command = CMD_HELP; return 0; }
		if (!strcmp(arg, "-l")) { list = 1; continue; }
		if (!strcmp(arg, "-d")) {
			if (++i == argc) return -1;
			opts->device = argv[i];
			opts->device_arg = 1;
			continue;
		}
		if (value_arg) return -1;
		value_arg = arg;
	}
	if (list && (value_arg || opts->device_arg)) return -1;
	if (list) opts->command = CMD_LIST;
	else if (value_arg) {
		if (parse_number(value_arg, &opts->value)) return -1;
		opts->command = *value_arg == '+' || *value_arg == '-' ?
			CMD_RELATIVE : CMD_SET;
	}
	return 0;
}

int main(int argc, char **argv) {
	struct options opts;
	struct light light;

	if (parse_options(argc, argv, &opts)) return usage(stderr, 2);
	if (opts.command == CMD_HELP) return usage(stdout, 0);
	if (opts.command == CMD_LIST) { scan_lights(NULL); return 0; }
	if (!opts.device_arg && (!opts.device || !*opts.device))
		scan_lights(&light);
	else if (load_light(&light, opts.device))
		err(1, "%s", opts.device);
	if (opts.command != CMD_GET) {
		if (write_number(light.name, "brightness",
			opts.command == CMD_RELATIVE ?
			relative_raw(light.now, light.max, opts.value) :
			percent_to_raw(clamp(opts.value, 100), light.max)))
			err(1, "%s/brightness", light.name);
		return 0;
	}
	printf("%ld\n", percent(light.now, light.max));
	return 0;
}
