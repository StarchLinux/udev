/*
 * Copyright (C) 2005-2006 Kay Sievers <kay.sievers@vrfy.org>
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
 *	51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "config.h"

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>

#include "udev.h"

static void print_help(void)
{
	printf("Usage: udevadm control COMMAND\n"
		"  --log-priority=<level>   set the udev log level for the daemon\n"
		"  --stop-exec-queue        keep udevd from executing events, queue only\n"
		"  --start-exec-queue       execute events, flush queue\n"
		"  --reload-rules           reloads the rules files\n"
		"  --env=<KEY>=<value>      set a global environment variable\n"
		"  --max-childs=<N>         maximum number of childs\n"
		"  --help                   print this help text\n\n");
}

int udevadm_control(struct udev *udev, int argc, char *argv[])
{
	struct udev_ctrl *uctrl = NULL;
	int rc = 1;

	/* compat values with '_' will be removed in a future release */
	static const struct option options[] = {
		{ "log-priority", 1, NULL, 'l' },
		{ "log_priority", 1, NULL, 'l' + 256 },
		{ "stop-exec-queue", 0, NULL, 's' },
		{ "stop_exec_queue", 0, NULL, 's' + 256 },
		{ "start-exec-queue", 0, NULL, 'S' },
		{ "start_exec_queue", 0, NULL, 'S' + 256},
		{ "reload-rules", 0, NULL, 'R' },
		{ "reload_rules", 0, NULL, 'R' + 256},
		{ "env", 1, NULL, 'e' },
		{ "max-childs", 1, NULL, 'm' },
		{ "max_childs", 1, NULL, 'm' + 256},
		{ "help", 0, NULL, 'h' },
		{}
	};

	if (getuid() != 0) {
		fprintf(stderr, "root privileges required\n");
		goto exit;
	}

	uctrl = udev_ctrl_new_from_socket(udev, UDEV_CTRL_SOCK_PATH);
	if (uctrl == NULL)
		goto exit;

	while (1) {
		int option;
		int i;
		char *endp;

		option = getopt_long(argc, argv, "l:sSRe:m:M:h", options, NULL);
		if (option == -1)
			break;

		if (option > 255) {
			fprintf(stderr, "udevadm control expects commands without underscore, "
				"this will stop working in a future release\n");
			err(udev, "udevadm control expects commands without underscore, "
			    "this will stop working in a future release\n");
		}

		switch (option) {
		case 'l':
		case 'l' + 256:
			i = log_priority(optarg);
			if (i < 0) {
				fprintf(stderr, "invalid number '%s'\n", optarg);
				goto exit;
			}
			udev_ctrl_send_set_log_level(uctrl, log_priority(optarg));
			rc = 0;
			break;
		case 's':
		case 's' + 256:
			udev_ctrl_send_stop_exec_queue(uctrl);
			rc = 0;
			break;
		case 'S':
		case 'S' + 256:
			udev_ctrl_send_start_exec_queue(uctrl);
			rc = 0;
			break;
		case 'R':
		case 'R' + 256:
			udev_ctrl_send_reload_rules(uctrl);
			rc = 0;
			break;
		case 'e':
			if (strchr(optarg, '=') == NULL) {
				fprintf(stderr, "expect <KEY>=<valaue> instead of '%s'\n", optarg);
				goto exit;
			}
			udev_ctrl_send_set_env(uctrl, optarg);
			rc = 0;
			break;
		case 'm':
		case 'm' + 256:
			i = strtoul(optarg, &endp, 0);
			if (endp[0] != '\0' || i < 1) {
				fprintf(stderr, "invalid number '%s'\n", optarg);
				goto exit;
			}
			udev_ctrl_send_set_max_childs(uctrl, i);
			rc = 0;
			break;
		case 'h':
			print_help();
			rc = 0;
			goto exit;
		default:
			goto exit;
		}
	}

	/* compat stuff which will be removed in a future release */
	if (argv[optind] != NULL) {
		const char *arg = argv[optind];

		fprintf(stderr, "udevadm control commands requires the --<command> format, "
			"this will stop working in a future release\n");
		err(udev, "udevadm control commands requires the --<command> format, "
		    "this will stop working in a future release\n");

		if (!strncmp(arg, "log_priority=", strlen("log_priority="))) {
			udev_ctrl_send_set_log_level(uctrl, log_priority(&arg[strlen("log_priority=")]));
			rc = 0;
			goto exit;
		} else if (!strcmp(arg, "stop_exec_queue")) {
			udev_ctrl_send_stop_exec_queue(uctrl);
			rc = 0;
			goto exit;
		} else if (!strcmp(arg, "start_exec_queue")) {
			udev_ctrl_send_start_exec_queue(uctrl);
			rc = 0;
			goto exit;
		} else if (!strcmp(arg, "reload_rules")) {
			udev_ctrl_send_reload_rules(uctrl);
			rc = 0;
			goto exit;
		} else if (!strncmp(arg, "max_childs=", strlen("max_childs="))) {
			udev_ctrl_send_set_max_childs(uctrl, strtoul(&arg[strlen("max_childs=")], NULL, 0));
			rc = 0;
			goto exit;
		} else if (!strncmp(arg, "env", strlen("env"))) {
			udev_ctrl_send_set_env(uctrl, &arg[strlen("env=")]);
			rc = 0;
			goto exit;
		}
	}

	if (rc != 0) {
		fprintf(stderr, "unrecognized command\n");
		err(udev, "unrecognized command\n");
	}
exit:
	udev_ctrl_unref(uctrl);
	return rc;
}