/*
 * Copyright (c) 2008 Lawrence Livermore National Lab.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <inttypes.h>
#include <regex.h>

#include <complib/cl_nodenamemap.h>
#include <infiniband/ibnetdisc.h>
#include <infiniband/mad.h>


char *argv0 = NULL;

static char *node_name_map_file = NULL;
static nn_map_t *node_name_map = NULL;

struct ibmad_port *ibmad_port;
ib_portid_t *ibd_sm_id;
ib_portid_t sm_portid = { 0 };

char *node_guid_str = NULL;
uint64_t node_guid = 0;
char *dr_path = NULL;
int hops = 0;
char *fabric_name = "fabric";
char *ignore_regex = NULL;

static int ignore_switch(char *node_name)
{
	int rc;
	static regex_t exp;
	static int regex_compiled = 0;
	static int regex_failed = 0;

	if (!ignore_regex)
		return 0;

	if (!regex_compiled) { /* only compile it one time */
		if ((rc = regcomp(&exp, ignore_regex, REG_ICASE |
				REG_NOSUB | REG_EXTENDED)) != 0) {
			fprintf(stderr, "ERROR: regcomp failed on \"%s\": %d\n",
				ignore_regex, rc);
			regex_failed = 1;
			return 0;
		}
		regex_compiled = 1;
	}

	return (regexec(&exp, node_name, 0, NULL, 0) == 0);
}

static void print_switch_xml(ibnd_node_t *sw, void *ud)
{
	int i = 0;
	char * node_name = remap_node_name(node_name_map,
					sw->guid,
					sw->nodedesc);

	if (ignore_switch(node_name))
		goto ignore;

	printf("\t<linklist name=\"%s\">\n", node_name);
	for (i = 0; i <= sw->numports; i++) {
		if (sw->ports[i] && sw->ports[i]->remoteport) {
			char *rem_name = remap_node_name(node_name_map,
					sw->ports[i]->remoteport->node->guid,
					sw->ports[i]->remoteport->node->nodedesc);
			printf("\t\t<port num=\"%d\">", i);
			printf("<r_port>%d</r_port>",
				sw->ports[i]->remoteport->portnum);
			printf("<r_node>%s</r_node>", rem_name);
			printf("</port>\n");
		}
	}
	printf("\t</linklist>\n");

ignore:
	free(node_name);
}

/** =========================================================================
 */
static int
usage(void)
{
        fprintf(stderr,
"%s [options]\n"
"Usage: generate an xml config file based off the scanned fabric\n"
"\n"
"Options:\n"
"  --name -m <name> fabric name\n"
"  --ignore -i <regex> skip switches which match regex\n"
"  -S <guid> generate for the node specified by the port guid\n"
"  -G <guid> Same as \"-S\" for compatibility with other diags\n"
"  -D <dr_path> generate for the node specified by the DR path given\n"
"  --hops -n <hops> hops to progress away from node given in -G,-S, or -D\n"
"                   default == 0\n"
"  --node-name-map <map> specify alternate node name map\n"
"  --Ca, -C <ca>         Ca name to use\n"
"  --Port, -P <port>     Ca port number to use\n"
"  --timeout, -t <ms>    timeout in ms\n"
"  --verbose, -v         increase verbosity level\n"
"\n"
, argv0
);
        return (0);
}

int main(int argc, char **argv)
{
	int resolved = -1;
	ib_portid_t portid = { 0 };
	int rc = 0;
	ibnd_fabric_t *fabric = NULL;
	struct ibnd_config config = { 0 };

	int mgmt_classes[3] = { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS };
	char *ibd_ca = NULL;
	int ibd_ca_port = 0;
	int ibd_timeout = 200;

        char  ch = 0;
        static char const str_opts[] = "hS:G:D:n:C:P:t:vm:i:";
        static const struct option long_opts [] = {
           {"help", 0, 0, 'h'},
	   {"node-name-map", 1, 0, 1},
	   {"hops", 1, 0, 'n'},
	   {"Ca", 1, 0, 'C'},
	   {"Port", 1, 0, 'P'},
	   {"timeout", 1, 0, 't'},
	   {"verbose", 0, 0, 'v'},
	   {"name", 1, 0, 'm'},
	   {"ignore", 1, 0, 'i'},
	   {0, 0, 0, 0}
        };

	argv0 = argv[0];

        while ((ch = getopt_long(argc, argv, str_opts, long_opts, NULL))
                != -1)
        {
                switch (ch)
                {
			case 'S':
			case 'G':
				node_guid_str = strdup(optarg);
				break;
			case 'D':
				dr_path = strdup(optarg);
				break;
			case 'n':
				hops = (int)strtol(optarg, NULL, 0);
				break;
			case 'm':
				fabric_name = strdup(optarg);
				break;
			case 'i':
				ignore_regex = strdup(optarg);
				break;
			case 1:
				node_name_map_file = strdup(optarg);
				break;
			case 'C':
				ibd_ca = strdup(optarg);
				break;
			case 'P':
				ibd_ca_port = atoi(optarg);
				break;
			case 't':
				ibd_timeout = atoi(optarg);
				break;
			case 's':
				/* srcport is not required when resolving via IB_DEST_LID */
				if (ib_resolve_portid_str_via(&sm_portid, optarg, IB_DEST_LID,
							      0, NULL) < 0)
					fprintf(stderr, "cannot resolve SM destination port %s",
						optarg);
				ibd_sm_id = &sm_portid;
				break;
                        case 'h':
                        default:
                        	exit(usage());
                }
	}

	ibmad_port = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!ibmad_port) {
		fprintf(stderr, "Failed to open port; %s:%d\n", ibd_ca, ibd_ca_port);
		exit(-1);
	}

	if (ibd_timeout) {
		mad_rpc_set_timeout(ibmad_port, ibd_timeout);
		config.timeout_ms = ibd_timeout;
	}

	node_name_map = open_node_name_map(node_name_map_file);

	/* limit the scan to around the target */
	if (dr_path) {
		if ((resolved =
		     ib_resolve_portid_str_via(&portid, dr_path, IB_DEST_DRPATH,
					       NULL, ibmad_port)) < 0) {
			fprintf(stderr, "Failed to resolve %s", dr_path);
			rc = -1;
			goto close_port;
		}
	} else if (node_guid_str) {
		if ((resolved =
		     ib_resolve_portid_str_via(&portid, node_guid_str,
					       IB_DEST_GUID, ibd_sm_id,
					       ibmad_port)) < 0) {
			fprintf(stderr, "Failed to resolve %s", node_guid_str);
			rc = -1;
			goto close_port;
		}
	}

	if (resolved >= 0) {
		config.max_hops = hops;
		fabric = ibnd_discover_fabric(ibd_ca, ibd_ca_port, &portid, &config);
	} else {
		fabric = ibnd_discover_fabric(ibd_ca, ibd_ca_port, NULL, &config);
	}

	if (!fabric) {
		fprintf(stderr, "ibnd_discover_fabric failed\n");
		rc = -1;
		goto close_port;
	}

	printf("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
	printf("<!DOCTYPE ibfabric SYSTEM \"ibfabric.dtd\">\n\n");
	printf("<fabric name=\"%s\">\n", fabric_name);

	ibnd_iter_nodes_type(fabric, print_switch_xml, IB_NODE_SWITCH, NULL);

	printf("</fabric>\n");

	ibnd_destroy_fabric(fabric);

close_port:
	mad_rpc_close_port(ibmad_port);
	close_node_name_map(node_name_map);
	exit(rc);
}
