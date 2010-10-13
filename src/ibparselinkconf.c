/*
 * Copyright (C) 2010 Lawrence Livermore National Security
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Written by Ira Weiny weiny2@llnl.gov
 * UCRL-CODE-235440
 * 
 * This file is part of pragmatic-infiniband-tools (PIU), useful tools to manage
 * Infiniband Clusters.
 * For details, see http://www.llnl.gov/linux/.
 * 
 * PIU is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 * 
 * PIU is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * PIU; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <iba/ib_types.h>
#include <infiniband/iblinkconf.h>

char *linkconf_file = NULL;
char *argv0 = NULL;

char *delim_out = NULL;

/** =========================================================================
 * Borrow from ibnetdiscover
 */
char *linkspeed_str(int speed)
{
	switch (speed) {
	case IB_LINK_SPEED_ACTIVE_2_5:
		return ("2.5 Gbps");
		//return ("SDR");
		break;
	case IB_LINK_SPEED_ACTIVE_5:
		return ("5.0 Gbps");
		//return ("DDR");
		break;
	case IB_LINK_SPEED_ACTIVE_10:
		return ("10.0 Gbps");
		//return ("QDR");
		break;
	}
	return ("???");
}

char *linkwidth_str(int width)
{
	switch (width) {
	case IB_LINK_WIDTH_ACTIVE_1X:
		return ("1X");
		break;
	case IB_LINK_WIDTH_ACTIVE_4X:
		return ("4X");
		break;
	case IB_LINK_WIDTH_ACTIVE_8X:
		return ("8X");
		break;
	case IB_LINK_WIDTH_ACTIVE_12X:
		return ("12X");
		break;
	}
	return ("??");
}

void
print_port(iblink_port_t *port, void *user_data)
{
	char prop[256];
	if (delim_out)
		printf("%s%s%d%s%d%s%s%s%s%s%s\n",
			iblink_port_get_name(port),
			delim_out,
			iblink_port_get_port_num(port),
			delim_out,
			iblink_port_get_port_num(iblink_port_get_remote(port)),
			delim_out,
			iblink_port_get_name(iblink_port_get_remote(port)),
			delim_out,
			linkspeed_str(iblink_prop_get_speed(iblink_port_get_prop(port))),
			delim_out,
			linkwidth_str(iblink_prop_get_width(iblink_port_get_prop(port))));
	else
		printf ("\"%30s\" %4d  ==(%s)==>  %4d \"%s\"\n",
			iblink_port_get_name(port),
			iblink_port_get_port_num(port),
			iblink_prop_str(port, prop, 256),
			iblink_port_get_port_num(iblink_port_get_remote(port)),
			iblink_port_get_name(iblink_port_get_remote(port))
			);
}

/** =========================================================================
 */
static int
usage(void)
{
        fprintf(stderr,
"%s [options] [node] [port]\n"
"Usage: parse the linkconf file\n"
"\n"
"Options:\n"
"  --conf, -c <file> Use an alternate link config file (default: %s)\n"
"  --warn_dup, -w If duplicated link configs are found warn about them\n"
"  --check_dup only print duplicates\n"
"  --delim_out, -d <deliminator> output colums deliminated by <deliminator>\n"
"  [node] if node is specified print ports for that node\n"
"  [port] if port is specified print information for just that port (default \"all\")\n"
"         if neither node nor port is specified print all links in config file\n"
"\n"
, argv0,
IBLINK_DEF_CONFIG
);
        return (0);
}


int
main(int argc, char **argv)
{
	iblink_conf_t *linkconf;
	int rc = 0;
	int warn_dup = 0;
	int check_dup = 0;
        char  ch = 0;
        static char const str_opts[] = "hc:wd:";
        static const struct option long_opts [] = {
           {"help", 0, 0, 'h'},
           {"conf", 1, 0, 'c'},
           {"warn_dup", 0, 0, 'w'},
           {"check_dup", 0, 0, 1},
           {"delim_out", 1, 0, 'd'},
	   {0, 0, 0, 0}
        };

	argv0 = argv[0];

        while ((ch = getopt_long(argc, argv, str_opts, long_opts, NULL))
                != -1)
        {
                switch (ch)
                {
			case 'c':
				linkconf_file = strdup(optarg);
				break;
			case 'w':
				warn_dup = 1;
				break;
			case 1:
				check_dup = 1;
				break;
			case 'd':
				delim_out = strdup(optarg);
				break;
                        case 'h':
                        default:
                        	exit(usage());
                }
	}

	argc -= optind;
	argv += optind;

	linkconf = iblink_alloc_conf();
	if (!linkconf) {
		fprintf(stderr, "ERROR: Failed to alloc linkconf\n");
		exit(1);
	}

	if (check_dup)
		iblink_set_stderr(linkconf, stdout);

	iblink_set_warn_dup(linkconf, warn_dup | check_dup);
	rc = iblink_parse_file(linkconf_file, linkconf);

	if (rc) {
		fprintf(stderr, "ERROR: failed to parse link config "
			"\"%s\":%s\n", linkconf_file, strerror(rc));
		return (rc);
	}

	if (check_dup)
		goto done;

	if (delim_out) {
		printf("Fabric Name%s%s\n", delim_out, iblink_conf_get_name(linkconf));
		printf("Node%sPort%sRem Port%sRem Node%sSpeed%sWidth\n",
			delim_out, delim_out, delim_out, delim_out, delim_out);
	} else
		printf("Fabric Name: %s\n", iblink_conf_get_name(linkconf));

	if (argv[0]) {
		char prop[256];
		int p_num = 1;
		if (argv[1]) {
			p_num = strtol(argv[1], NULL, 0);
			iblink_port_t *port = iblink_get_port(linkconf, argv[0], p_num);
			if (port)
				print_port(port, NULL);
			else
				fprintf (stderr, "ERROR: \"%s\":%d port not found\n",
					argv[0], p_num);
		} else {
			iblink_port_list_t *port_list;
			int rc = iblink_get_port_list(linkconf, argv[0],
							&port_list);
			if (rc) {
				fprintf(stderr, "ERROR: Failed to get port "
					"list for \"%s\":%s\n",
					argv[0], strerror(rc));
			} else {
				iblink_iter_port_list(port_list, print_port,
							NULL);
				iblink_free_port_list(port_list);
			}
		}
	} else {
		iblink_iter_ports(linkconf, print_port, NULL);
	}

done:
	iblink_free(linkconf);

	return (rc);
}

