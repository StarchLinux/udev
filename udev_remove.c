/*
 * udev-remove.c
 *
 * Userspace devfs
 *
 * Copyright (C) 2003 Greg Kroah-Hartman <greg@kroah.com>
 *
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "udev.h"
#include "udev_lib.h"
#include "udev_version.h"
#include "logging.h"
#include "namedev.h"
#include "udevdb.h"

static int delete_path(char *path)
{
	char *pos;
	int retval;

	pos = strrchr(path, '/');
	while (1) {
		*pos = '\0';
		pos = strrchr(path, '/');

		/* don't remove the last one */
		if ((pos == path) || (pos == NULL))
			break;

		/* remove if empty */
		retval = rmdir(path);
		if (errno == ENOENT)
			retval = 0;
		if (retval) {
			if (errno == ENOTEMPTY)
				return 0;
			dbg("rmdir(%s) failed with error '%s'",
			    path, strerror(errno));
			break;
		}
		dbg("removed '%s'", path);
	}
	return 0;
}

/** Remove all permissions on the device node, before
  * unlinking it. This fixes a security issue.
  * If the user created a hard-link to the device node,
  * he can't use it any longer, because he lost permission
  * to do so.
  */
static int secure_unlink(const char *filename)
{
	int retval;

	retval = chown(filename, 0, 0);
	if (retval) {
		dbg("chown(%s, 0, 0) failed with error '%s'",
		    filename, strerror(errno));
		/* We continue nevertheless.
		 * I think it's very unlikely for chown
		 * to fail here, if the file exists.
		 */
	}
	retval = chmod(filename, 0000);
	if (retval) {
		dbg("chmod(%s, 0000) failed with error '%s'",
		    filename, strerror(errno));
		/* We continue nevertheless. */
	}
	retval = unlink(filename);
	if (errno == ENOENT)
		retval = 0;
	if (retval) {
		dbg("unlink(%s) failed with error '%s'",
			filename, strerror(errno));
	}
	return retval;
}

static int delete_node(struct udevice *dev)
{
	char filename[NAME_SIZE];
	char linkname[NAME_SIZE];
	char partitionname[NAME_SIZE];
	int retval;
	int i;
	char *pos;
	int len;
	int num;

	strfieldcpy(filename, udev_root);
	strfieldcat(filename, dev->name);

	info("removing device node '%s'", filename);
	retval = secure_unlink(filename);
	if (retval)
		return retval;

	/* remove all_partitions nodes */
	num = dev->partitions;
	if (num > 0) {
		info("removing all_partitions '%s[1-%i]'", filename, num);
		if (num > PARTITIONS_COUNT) {
			info("garbage from udev database, skip all_partitions removal");
			return -1;
		}
		for (i = 1; i <= num; i++) {
			strfieldcpy(partitionname, filename);
			strintcat(partitionname, i);
			secure_unlink(partitionname);
		}
	}

	/* remove subdirectories */
	if (strchr(dev->name, '/'))
		delete_path(filename);

	foreach_strpart(dev->symlink, " ", pos, len) {
		strfieldcpymax(linkname, pos, len+1);
		strfieldcpy(filename, udev_root);
		strfieldcat(filename, linkname);

		dbg("unlinking symlink '%s'", filename);
		retval = unlink(filename);
		if (errno == ENOENT)
			retval = 0;
		if (retval) {
			dbg("unlink(%s) failed with error '%s'",
				filename, strerror(errno));
			return retval;
		}
		if (strchr(dev->symlink, '/')) {
			delete_path(filename);
		}
	}

	return retval;
}

/*
 * Look up the sysfs path in the database to see if we have named this device
 * something different from the kernel name.  If we have, us it.  If not, use
 * the default kernel name for lack of anything else to know to do.
 */
int udev_remove_device(const char *path, const char *subsystem)
{
	struct udevice dev;
	char *temp;
	int retval;

	memset(&dev, 0x00, sizeof(dev));

	retval = udevdb_get_dev(path, &dev);
	if (retval != 0) {
		dbg("'%s' not found in database, falling back on default name", path);
		temp = strrchr(path, '/');
		if (temp == NULL)
			return -ENODEV;
		strfieldcpy(dev.name, &temp[1]);
	}
	dbg("name='%s'", dev.name);

	dev.type = get_device_type(path, subsystem);
	dev_d_send(&dev, subsystem, path);
	udevdb_delete_dev(path);

	if (dev.type == 'b' || dev.type == 'c')
		retval = delete_node(&dev);
	else if (dev.type == 'n')
		retval = 0;

	return retval;
}