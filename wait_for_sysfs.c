/*
 * wait_for_sysfs.c  - small program to delay the execution
 *		       of /etc/hotplug.d/ programs, until sysfs
 *		       is populated by the kernel. Depending on
 *		       the type of device, we wait for all expected
 *		       directories and then just exit.
 *
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation version 2 of the License.
 * 
 *	This program is distributed in the hope that it will be useful, but
 *	WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *	General Public License for more details.
 * 
 *	You should have received a copy of the GNU General Public License along
 *	with this program; if not, write to the Free Software Foundation, Inc.,
 *	675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

#include "logging.h"
#include "libsysfs/sysfs/libsysfs.h"

#ifdef LOG
unsigned char logname[LOGNAME_SIZE];
void log_message(int level, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vsyslog(level, format, args);
	va_end(args);
}
#endif

#define WAIT_MAX_SECONDS		5
#define WAIT_LOOP_PER_SECOND		20

static int wait_for_class_device_attributes(struct sysfs_class_device *class_dev)
{
	static struct class_file {
		char *subsystem;
		char *file;
	} class_files[] = {
		{ .subsystem = "net",		.file = "ifindex" },
		{ .subsystem = "usb_host",	.file = NULL },
		{ .subsystem = "pcmcia_socket",	.file = NULL },
		{ NULL, NULL }
	};
	struct class_file *classfile;
	const char *file = "dev";
	int loop;

	/* look if we want to look for another file instead of "dev" */
	for (classfile = class_files; classfile->subsystem != NULL; classfile++) {
		if (strcmp(class_dev->classname, classfile->subsystem) == 0) {
			if (classfile->file == NULL) {
				dbg("class '%s' has no file to wait for", class_dev->classname);
				return 0;
			}
			file = classfile->file;
			break;
		}
	}
	dbg("looking at class '%s' for specific file '%s'", class_dev->classname, file);

	loop = WAIT_MAX_SECONDS * WAIT_LOOP_PER_SECOND;
	while (--loop) {
		if (sysfs_get_classdev_attr(class_dev, file) != NULL) {
			dbg("class '%s' specific file '%s' found", class_dev->classname, file);
			return 0;
		}
	}

	dbg("error: getting bus '%s' specific file '%s'", class_dev->classname, file);
	return -1;
}

static int class_device_expect_no_device_link(struct sysfs_class_device *class_dev)
{
	char **device;

	static char *devices_without_link[] = {
		"nb",
		"ram",
		"loop",
		"fd",
		"md",
		"dos_cd",
		"double",
		"flash",
		"msd",
		"rflash",
		"rom",
		"rrom",
		"sbpcd",
		"pcd",
		"pf",
		"scd",
		"sit",
		"lp",
		"ubd",
		"vcs",
		"vcsa",
		"console",
		"tty",
		"ttyS",
		NULL
	};

	for (device = devices_without_link; *device != NULL; device++) {
		int len = strlen(*device);

		/* look if name matches */
		if (strncmp(class_dev->name, *device, len) != 0)
			continue;

		/* exact match */
		if (strlen(class_dev->name) == len)
			return 1;

		/* instance numbers are matching too */
		if (isdigit(class_dev->name[len]))
			return 1;
	}

	return 0;
}

static int wait_for_bus_device(struct sysfs_device *device_dev)
{
	static struct bus_file {
		char *bus;
		char *file;
	} bus_files[] = {
		{ .bus = "scsi",	.file = "vendor" },
		{ .bus = "usb",		.file = "idVendor" },
		{ .bus = "usb",		.file = "iInterface" },
		{ .bus = "usb-serial",	.file = "detach_state" },
		{ .bus = "ide",		.file = "detach_state" },
		{ .bus = "pci",		.file = "vendor" },
		{ NULL }
	};
	struct bus_file *busfile;
	int loop;

	/* wait for the /bus-device link to the /device-device */
	loop = WAIT_MAX_SECONDS * WAIT_LOOP_PER_SECOND;
	while (--loop) {
		if (sysfs_get_device_bus(device_dev) == 0)
			break;

		usleep(1000 * 1000 / WAIT_LOOP_PER_SECOND);
	}
	if (loop == 0) {
		dbg("error: getting /bus-device link");
		return -1;
	}
	dbg("/bus-device link found for bus '%s'", device_dev->bus);

	/* wait for a bus specific file to show up */
	loop = WAIT_MAX_SECONDS * WAIT_LOOP_PER_SECOND;
	while (--loop) {
		for (busfile = bus_files; busfile->bus != NULL; busfile++) {
			if (strcmp(device_dev->bus, busfile->bus) == 0) {
				dbg("looking at bus '%s' for specific file '%s'", device_dev->bus, busfile->file);
				if (sysfs_get_device_attr(device_dev, busfile->file) != NULL) {
					dbg("bus '%s' specific file '%s' found", device_dev->bus, busfile->file);
					return 0;
				}
				if (busfile->bus == NULL) {
					info("error: unknown bus, update the build-in list '%s'", device_dev->bus);
					return -1;
				}
			}
		}
	}

	dbg("error: getting bus '%s' specific file '%s'", device_dev->bus, busfile->file);
	return -1;
}

int main(int argc, char *argv[], char *envp[])
{
	const char *devpath = "";
	const char *action;
	const char *subsystem;
	char sysfs_path[SYSFS_PATH_MAX];
	char filename[SYSFS_PATH_MAX];
	struct sysfs_class_device *class_dev;
	struct sysfs_class_device *class_dev_parent;
	struct sysfs_device *device_dev = NULL;
	int loop;
	int rc = 0;

	if (argc != 2) {
		dbg("error: subsystem");
		return 1;
	}
	subsystem = argv[1];

	devpath = getenv ("DEVPATH");
	if (!devpath) {
		dbg("error: no DEVPATH");
		return 1;
	}

	action = getenv ("ACTION");
	if (!action) {
		dbg("error: no ACTION");
		return 1;
	}

	if (strcmp(action, "add") != 0)
		return 0;

	if (sysfs_get_mnt_path(sysfs_path, SYSFS_PATH_MAX) != 0) {
		dbg("error: no sysfs path");
		return 2;
	}

	if ((strncmp(devpath, "/block/", 7) == 0) || (strncmp(devpath, "/class/", 7) == 0)) {
		/* open the class device we are called for */
		snprintf(filename, SYSFS_PATH_MAX-1, "%s%s", sysfs_path, devpath);
		filename[SYSFS_PATH_MAX-1] = '\0';

		loop = WAIT_MAX_SECONDS * WAIT_LOOP_PER_SECOND;
		while (--loop) {
			class_dev = sysfs_open_class_device_path(filename);
			if (class_dev)
				break;
		}
		if (class_dev == NULL) {
			dbg("error: getting class_device");
			rc = 4;
			goto exit;
		}
		dbg("class_device opened '%s'", filename);

		wait_for_class_device_attributes(class_dev);

		if (class_device_expect_no_device_link(class_dev)) {
			dbg("no device symlink expected");
			sysfs_close_class_device(class_dev);
			goto exit;
		}

		/* the symlink may be on the parent device */
		class_dev_parent = sysfs_get_classdev_parent(class_dev);
		if (class_dev_parent)
			dbg("looking at parent device for device link '%s'", class_dev_parent->path);

		/* wait for the symlink to the /device-device */
		dbg("waiting for symlink to /device-device");
		loop = WAIT_MAX_SECONDS * WAIT_LOOP_PER_SECOND;
		while (--loop) {
			if (class_dev_parent)
				device_dev = sysfs_get_classdev_device(class_dev_parent);
			else
				device_dev = sysfs_get_classdev_device(class_dev);

			if (device_dev)
				break;

			usleep(1000 * 1000 / WAIT_LOOP_PER_SECOND);
		}
		if (device_dev == NULL) {
			dbg("error: getting /device-device");
			sysfs_close_class_device(class_dev);
			rc = 5;
			goto exit;
		}
		dbg("device symlink found pointing to '%s'", device_dev->path);

		/* wait for the bus value */
		if (wait_for_bus_device(device_dev) != 0)
			rc = 6;
		sysfs_close_class_device(class_dev);

		/* finished */
		goto exit;

	} else if ((strncmp(devpath, "/devices/", 9) == 0)) {
		/* open the path we are called for */
		snprintf(filename, SYSFS_PATH_MAX-1, "%s%s", sysfs_path, devpath);
		filename[SYSFS_PATH_MAX-1] = '\0';

		loop = WAIT_MAX_SECONDS * WAIT_LOOP_PER_SECOND;
		while (--loop) {
			device_dev = sysfs_open_device_path(filename);
			if (device_dev)
				break;
		}
		if (device_dev == NULL) {
			dbg("error: getting /device-device");
			rc = 4;
			goto exit;
		}
		dbg("device_device opened '%s'", filename);

		/* wait for the bus value */
		if (wait_for_bus_device(device_dev) != 0)
			rc = 9;

		sysfs_close_device(device_dev);

		/* finished */
		goto exit;

	} else {
		dbg("unhandled sysfs path, no need to wait");
	}

exit:
	if (rc == 0)
		info("result: waiting for sysfs successful '%s'", devpath);
	else
		info("result: waiting for sysfs failed '%s'", devpath);

	return rc;
}