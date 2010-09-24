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
#include <infiniband/ibedgeconf.h>

/**
 * Test code below
 */
void
print_port(ibedge_port_t *port, void *user_data)
{
	char prop[256];
	char rprop[256];
	printf ("\"%30s\" %4d  ==(%s)==>  %4d \"%s\"\n",
		ibedge_port_get_name(port),
		ibedge_port_get_port_num(port),
		ibedge_prop_str(port, prop, 256),
		ibedge_port_get_port_num(ibedge_port_get_remote(port)),
		ibedge_port_get_name(ibedge_port_get_remote(port))
		);
}

int
main(int argc, char **argv)
{
	int rc = 0;
	ibedge_conf_t *edgeconf = ibedge_alloc_conf();

	if (argc < 2)
		return(1);

	rc = ibedge_parse_file(argv[1], edgeconf);
	if (argv[2]) {
		char prop[256];
		int p_num = 1;
		if (argv[3])
			p_num = strtol(argv[3], NULL, 0);
		ibedge_port_t *port = ibedge_get_port(edgeconf, argv[2], p_num);
		if (port)
			print_port(port, NULL);
		else
			printf ("WARNING: \"%s\":%d port not found\n",
				argv[2], p_num);
	} else {
		printf("Name: %s\n", ibedge_conf_get_name(edgeconf));
		ibedge_iter_ports(edgeconf, print_port, NULL);
	}

	ibedge_free(edgeconf);

	return (rc);
}

