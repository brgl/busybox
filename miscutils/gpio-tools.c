/* vi: set sw=4 ts=4: */
/*
 * Minimal port of gpio-tools for busybox.
 *
 * Copyright (C) 2019 by Bartosz Golaszewski <bgolaszewski@baylibre.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

//config:config GPIODETECT
//config:	bool "gpiodetect"
//config:	select PLATFORM_LINUX
//config:	help
//config:	List all GPIO chips in the system
//config:
//config:config GPIOINFO
//config:	bool "gpioinfo"
//config:	select PLATFORM_LINUX
//config:	help
//config:	Print info about GPIO lines
//config:
//config:config GPIOGET
//config:	bool "gpioget"
//config:	select PLATFORM_LINUX
//config:	help
//config:	Read line values from a GPIO chip

//applet:IF_GPIODETECT(APPLET(gpiodetect, BB_DIR_USR_BIN, BB_SUID_DROP))
//applet:IF_GPIOINFO(APPLET(gpioinfo, BB_DIR_USR_BIN, BB_SUID_DROP))
//applet:IF_GPIOGET(APPLET(gpioget, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_GPIODETECT) += gpio-tools.o
//kbuild:lib-$(CONFOG_GPIOINFO) += gpio-tools.o
//kbuild:lib-$(CONFIG_GPIOGET) += gpio-tools.o

#include "libbb.h"

#include <linux/gpio.h>

/*
 * Open the file under 'path' and make sure it is a character device
 * associated with a GPIO chip. Return the file descriptor number.
 */
static int gpiochip_open(const char *path)
{
	char *sysfsp, devstr[16], sysfsdev[16];
	struct stat statbuf;
	int fd;

	fd = xopen(path, O_RDWR);
	/*
	 * We were able to open the file but is it really a gpiochip character
	 * device?
	 */
	xfstat(fd, &statbuf, path);

	/* Is it a character device? */
	if (!S_ISCHR(statbuf.st_mode)) {
		/*
		 * If we passed a file descriptor not associated with a
		 * character device to ioctl(), it would make it set errno to
		 * ENOTTY. Let's do the same.
		 */
		errno = ENOTTY;
		goto err;
	}

	/* Do we have a corresponding sysfs attribute? */
	sysfsp = xasprintf("/sys/bus/gpio/devices/%s/dev", bb_basename(path));
	if (access(sysfsp, R_OK) != 0) {
		/*
		 * This is a character device but not the one we're after. We'd
		 * still fail with ENOTTY on the first GPIO ioctl().
		 */
		errno = -ENOTTY;
		goto err;
	}

	/*
	 * Make sure the major and minor numbers of the character device
	 * correspond with the ones in the dev attribute in sysfs.
	 */
	snprintf(devstr, sizeof(devstr), "%u:%u",
		 major(statbuf.st_rdev), minor(statbuf.st_rdev));
	open_read_close(sysfsp, sysfsdev, sizeof(sysfsdev) - 1);
	free(sysfsp);

	if (strncmp(sysfsdev, devstr, strlen(devstr)) != 0) {
		errno = ENODEV;
		goto err;
	}

	return fd;
err:
	bb_perror_msg_and_die("unable to open %s", path);
}

#if ENABLE_GPIODETECT || ENABLE_GPIOINFO
static int is_dirent_gpiochip(const struct dirent *dir)
{
	return !strncmp(dir->d_name, "gpiochip", 8);
}

static int get_gpiochip_list(struct dirent ***dirs)
{
	int num_chips;

	/*
	 * We're using scandir() instead of recursive_action() because we're
	 * getting the added benefit of having the chips sorted without any
	 * additional code other than using alphasort().
	 */
	num_chips = scandir("/dev", dirs, is_dirent_gpiochip, alphasort);
	if (num_chips < 0)
		bb_perror_msg_and_die("/dev");

	return num_chips;
}

static void for_each_chip(void (*func)(int))
{
	struct dirent **dirs;
	int i, num_chips, fd;
	char *path;

	num_chips = get_gpiochip_list(&dirs);

	for (i = 0; i < num_chips; i++) {
		path = xasprintf("/dev/%s", dirs[i]->d_name);
		fd = gpiochip_open(path);
		free(path);
		func(fd);
		close(fd);
	}

	if (ENABLE_FEATURE_CLEAN_UP) {
		for (i = 0; i < num_chips; i++)
			free(dirs[i]);
		free(dirs);
	}
}
#endif /* ENABLE_GPIODETECT || ENABLE_GPIOINFO */

#ifdef ENABLE_GPIODETECT
static void print_chip_detect(int fd)
{
	struct gpiochip_info info;

	ioctl_or_perror_and_die(fd, GPIO_GET_CHIPINFO_IOCTL,
				&info, "chip info ioctl");

	printf("%s [%s] (%u lines)\n",
	       info.name, info.label, info.lines);
}

//usage:#define gpiodetect_trivial_usage
//usage:	""
//usage:#define gpiodetect_full_usage "\n\n"
//usage:	"List all GPIO chips in the system"
int gpiodetect_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int gpiodetect_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	for_each_chip(print_chip_detect);

	return EXIT_SUCCESS;
}
#endif /* ENABLE_GPIODETECT */

#ifdef ENABLE_GPIOINFO
static void print_chip_info(int fd)
{

}

//usage:#define gpioinfo_trivial_usage
//usage:	"[CHIP1 [CHIP2 ...]]"
//usage:#define gpioinfo_full_usage "\n\n"
//usage:	"Print info about GPIO lines"
int gpioinfo_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int gpioinfo_main(int argc, char **argv)
{
	int fd, i;

	if (argc == 1) {
		for_each_chip(print_chip_info);
	} else {
		for (i = 1; i < argc; i++) {
			fd = gpiochip_open_lookup(argv[i]);
			print_chip_info(fd);
			close(fd);
		}
	}

	return EXIT_SUCCESS;
}
#endif /* ENABLE_GPIOINFO */

#if ENABLE_GPIOGET
//usage:#define gpioget_trivial_usage
//usage:	"[-a] CHIP OFFSET_1 OFFSET_2 ..."
//usage:#define gpioget_full_usage "\n\n"
//usage:	"Read line values from a GPIO chip"
//usage:      "\n	-l	Set the line active state to low"
int gpioget_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int gpioget_main(int argc, char **argv)
{
	enum {
		opt_l = (1 << 0),
	};

	unsigned int opts;
	int i, num_lines;
	char *device;

	opts = getopt32(argv,
			"^"
			"l"
			"\0-2" /* minimum 2 args */
	);

	argv += optind;
	argc -= optind;

	device = argv[1];
	num_lines = argc - 1;

	return EXIT_SUCCESS;
}
#endif /* ENABLE_GPIOGET */
