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
#include <infiniband/ibedgeconf.h>

char *edgeconf_file = NULL;
char *argv0 = NULL;

void
print_port(ibedge_port_t *port, void *user_data)
{
	char prop[256];
	//char rprop[256];
	printf ("\"%30s\" %4d  ==(%s)==>  %4d \"%s\"\n",
		ibedge_port_get_name(port),
		ibedge_port_get_port_num(port),
		ibedge_prop_str(port, prop, 256),
		ibedge_port_get_port_num(ibedge_port_get_remote(port)),
		ibedge_port_get_name(ibedge_port_get_remote(port))
		);
}

/** =========================================================================
 */
static int
usage(void)
{
        fprintf(stderr,
"%s [options] [node] [port]\n"
"Usage: parse the edgeconf file\n"
"\n"
"Options:\n"
"  --conf, specify an alternate config (default: %s)\n"
"  [node] if node is specified print port for that node\n"
"  [port] if port is specified print port for that node (default 1)\n"
"         if neither node nor port is specified print all edges in config file\n"
"\n"
, argv0,
IBEDGE_DEF_CONFIG
);
        return (0);
}


int
main(int argc, char **argv)
{
	ibedge_conf_t *edgeconf;
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
				edgeconf_file = strdup(optarg);
				break;
                        case 'h':
                        default:
                        	exit(usage());
                }
	}

	argc -= optind;
	argv += optind;

	edgeconf = ibedge_alloc_conf();
	rc = ibedge_parse_file(edgeconf_file, edgeconf);

	if (rc) {
		fprintf(stderr, "ERROR: failed to parse edge config "
			"\"%s\":%s\n", edgeconf_file, strerror(rc));
		return (rc);
	}

	if (argv[0]) {
		char prop[256];
		int p_num = 1;
		if (argv[1]) {
			p_num = strtol(argv[1], NULL, 0);
			ibedge_port_t *port = ibedge_get_port(edgeconf, argv[0], p_num);
			if (port)
				print_port(port, NULL);
			else
				fprintf (stderr, "ERROR: \"%s\":%d port not found\n",
					argv[0], p_num);
		} else {
			ibedge_port_list_t *port_list;
			int rc = ibedge_get_port_list(edgeconf, argv[0],
							&port_list);
			if (rc) {
				fprintf(stderr, "ERROR: Failed to get port "
					"list for \"%s\":%s\n",
					argv[0], strerror(rc));
			} else {
				ibedge_iter_port_list(port_list, print_port,
							NULL);
				ibedge_free_port_list(port_list);
			}
		}
	} else {
		printf("Name: %s\n", ibedge_conf_get_name(edgeconf));
		ibedge_iter_ports(edgeconf, print_port, NULL);
	}

	ibedge_free(edgeconf);

	return (rc);
}

