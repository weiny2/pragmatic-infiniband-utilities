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

#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <infiniband/iblinkconf.h>

char *linkconf_file = NULL;
char *argv0 = NULL;

void
print_port(iblink_port_t *port, void *user_data)
{
	char prop[256];
	//char rprop[256];
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
"  --conf <file>, specify an alternate config (default: %s)\n"
"  [node] if node is specified print port for that node\n"
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
        char  ch = 0;
        static char const str_opts[] = "h";
        static const struct option long_opts [] = {
           {"help", 0, 0, 'h'},
           {"config", 1, 0, 1},
	   {0, 0, 0, 0}
        };

	argv0 = argv[0];

        while ((ch = getopt_long(argc, argv, str_opts, long_opts, NULL))
                != -1)
        {
                switch (ch)
                {
			case 1:
				linkconf_file = strdup(optarg);
				break;
                        case 'h':
                        default:
                        	exit(usage());
                }
	}

	argc -= optind;
	argv += optind;

	linkconf = iblink_alloc_conf();
	rc = iblink_parse_file(linkconf_file, linkconf);

	if (rc) {
		fprintf(stderr, "ERROR: failed to parse link config "
			"\"%s\":%s\n", linkconf_file, strerror(rc));
		return (rc);
	}

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
		printf("Name: %s\n", iblink_conf_get_name(linkconf));
		iblink_iter_ports(linkconf, print_port, NULL);
	}

	iblink_free(linkconf);

	return (rc);
}

