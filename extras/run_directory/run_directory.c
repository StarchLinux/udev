/*
 * udev_run_directory.c - directory multiplexer
 *
 * Copyright (C) 2005 Kay Sievers <kay@vrfy.org>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation version 2 of the License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "../../udev_utils.h"
#include "../../list.h"
#include "../../logging.h"

int run_directory(const char *dir, const char *suffix, const char *subsystem);

static int run_program(const char *filename, const char *subsystem)
{
	pid_t pid;

	dbg("running %s", filename);
	pid = fork();
	switch (pid) {
	case 0:
		/* child */
		execl(filename, filename, subsystem, NULL);
		dbg("exec of child failed");
		_exit(1);
	case -1:
		dbg("fork of child failed");
		break;
		return -1;
	default:
		waitpid(pid, NULL, 0);
	}

	return 0;
}

int run_directory(const char *dir, const char *suffix, const char *subsystem)
{
	char dirname[NAME_SIZE];
	struct name_entry *name_loop, *name_tmp;
	LIST_HEAD(name_list);

	if (subsystem) {
		snprintf(dirname, sizeof(dirname), "%s/%s", dir, subsystem);
		dirname[sizeof(dirname)-1] = '\0';
		dbg("looking at '%s'", dirname);
		add_matching_files(&name_list, dirname, suffix);
	}

	snprintf(dirname, sizeof(dirname), "%s/default", dir);
	dirname[sizeof(dirname)-1] = '\0';
	dbg("looking at '%s'", dirname);
	add_matching_files(&name_list, dirname, suffix);

	list_for_each_entry_safe(name_loop, name_tmp, &name_list, node) {
		run_program(name_loop->name, subsystem);
		list_del(&name_loop->node);
	}

	logging_close();
	return 0;
}