/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2007 Xsigo Systems Inc.  All rights reserved.
 * Copyright (c) 2008 Lawrence Livermore National Lab.  All rights reserved.
 * Copyright (c) 2009 HNR Consulting.  All rights reserved.
 * Copyright (c) 2010 Lawrence Livermore National Lab.  All rights reserved.
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
char *root_regex = NULL;
char *ignore_regex = NULL;
char *cluster_name = NULL;
uint64_t node_guid = 0;
char *dr_path = NULL;
int combine_edges = 0;
char *fromname = NULL;
char *toname = NULL;

ibnd_node_t *fromnode = NULL;
ibnd_node_t *tonode = NULL;

#define HTSZ 137
#define HASHGUID(guid) (((uint32_t)(((uint32_t)(guid) * 101) ^ ((uint32_t)((guid) >> 32) * 103))) % HTSZ)
struct lnode {
	struct lnode *next;
	ibnd_node_t *node;
	char port_visited[256];
};

struct local_fabric {
	int num_nodes;
	int rank_sep;
	struct lnode *nodes[HTSZ];
	struct lnode *roots;
	struct lnode *leafs;
} lfab;

static struct lnode *find_lnode(uint64_t guid)
{
	int hash = HASHGUID(guid) % HTSZ;
	struct lnode *lnode;

	for (lnode = lfab.nodes[hash]; lnode; lnode = lnode->next)
		if (lnode->node->guid == guid)
			return lnode;
	return (NULL);
}
static void insert_node(struct lnode *lnode)
{
	int hash_idx = HASHGUID(lnode->node->guid) % HTSZ;
	/* keep track by hash */
	lnode->next = lfab.nodes[hash_idx];
	lfab.nodes[hash_idx] = lnode;
}

static void flag_port(ibnd_node_t *node, uint8_t portnum)
{
	struct lnode *lnode = find_lnode(node->guid);
	if (!lnode) {
		lnode = calloc(1, sizeof(*lnode));
		lnode->node = node;
		insert_node(lnode);
	}
	lnode->port_visited[portnum] = 1;
}

static int port_flagged(uint64_t guid, uint8_t portnum)
{
	struct lnode *node = find_lnode(guid);
	if (!node)
		return 0;
	return (node->port_visited[portnum]);
}

static void id_root(ibnd_node_t *node, char *node_name)
{
	int rc;
	static regex_t exp;
	static int regex_compiled = 0;
	static int regex_failed = 0;

	if (!root_regex || regex_failed)
		return;

	if (!regex_compiled) { /* only compile it one time */
		if ((rc = regcomp(&exp, root_regex, REG_ICASE |
				REG_NOSUB | REG_EXTENDED)) != 0) {
			fprintf(stderr, "ERROR: regcomp failed on \"%s\": %d\n",
				root_regex, rc);
			regex_failed = 1;
			return;
		}
		regex_compiled = 1;
	}

	if (regexec(&exp, node_name, 0, NULL, 0) == 0) {
		struct lnode *lnode = calloc(1, sizeof(*lnode));
		if (!lnode) {
			fprintf(stderr, "%s:%d calloc FAILED! (%s)\n",
				__FUNCTION__, __LINE__, strerror(errno));
			return;
		}
		lnode->node = node;
		if (lfab.roots)
			lnode->next = lfab.roots;
		lfab.roots = lnode;
	}
}

static void add_to_leafs(ibnd_node_t *node)
{
	struct lnode *lnode = calloc(1, sizeof(*lnode));
	if (!lnode) {
		fprintf(stderr, "%s:%d calloc FAILED! (%s)\n",
			__FUNCTION__, __LINE__, strerror(errno));
		return;
	}
	lnode->node = node;
	if (lfab.leafs)
		lnode->next = lfab.leafs;
	lfab.leafs = lnode;
}

static int ignore_node(char *node_name)
{
	int rc;
	static regex_t exp;
	static int regex_compiled = 0;
	static int regex_failed = 0;

	if (!ignore_regex || regex_failed)
		return 0;

	if (!regex_compiled) { /* only compile it one time */
		if ((rc = regcomp(&exp, ignore_regex, REG_ICASE |
				REG_NOSUB | REG_EXTENDED)) != 0) {
			fprintf(stderr, "ERROR: regcomp failed on \"%s\": %d\n",
				root_regex, rc);
			regex_failed = 1;
			return 0;
		}
		regex_compiled = 1;
	}

	return (regexec(&exp, node_name, 0, NULL, 0) == 0);
}

struct links {
	uint8_t pnum;
	uint64_t rem_guid;
	uint8_t rem_pnum;
	int combine_num;
};

static void process_node(ibnd_node_t *node, void *data)
{
	static regex_t exp;
	static int regex_compiled = 0;
	char re_str[256];

	int rc, pnum, i;
	char label[256];
	char attr[256];
	char *node_name = NULL;
	attr[0] = '\0';
	ibnd_port_t *rem_port = NULL;
	struct links links[256];
	int num_links = 0;

	node_name = remap_node_name(node_name_map,
					node->guid,
					node->nodedesc);

	if (ignore_node(node_name))
		return;

	id_root(node, node_name);

	if (!regex_compiled && cluster_name) { /* only compile it one time */
		snprintf(re_str, 256, "^%s.*$", cluster_name);
		re_str[255] = '\0';
		if ((rc = regcomp(&exp, re_str, REG_ICASE |
				REG_NOSUB | REG_EXTENDED)) != 0) {
			fprintf(stderr, "ERROR: regcomp failed on \"%s\": %d\n",
				re_str, rc);
		}
		regex_compiled = 1;
	}

	if (regex_compiled && cluster_name
	    && (regexec(&exp, node_name, 0, NULL, 0) == 0)) {
		snprintf(label, 256, "%s", node_name);
		add_to_leafs(node);
	} else
		snprintf(label, 256, "%s\\nG: x%016lx", node_name, node->guid);

	label[255] = '\0';
	printf("   x%016lx [label=\"%s\"%s];\n", node->guid, label, attr);

	for (pnum = 1; pnum <= node->numports; pnum++) {
		if (!node->ports[pnum])
			continue;
		rem_port = node->ports[pnum]->remoteport;
		if (!rem_port)
			continue;
		if (!port_flagged(node->guid, pnum)) {
			char *rem_node_name = remap_node_name(node_name_map,
							rem_port->node->guid,
							rem_port->node->nodedesc);
			if (ignore_node(rem_node_name)) {
				free(rem_node_name);
				continue;
			}
			free(rem_node_name);

			flag_port(rem_port->node, rem_port->portnum);
			if (combine_edges) {
				int f = 0;
				for (i = 0; i < num_links; i++)
					if (links[i].rem_guid == rem_port->node->guid) {
						links[i].combine_num++;
						f = 1;
						break;
					}
				if (f)
					continue;
			}
			links[num_links].pnum = pnum;
			links[num_links].rem_guid = rem_port->node->guid;
			links[num_links].rem_pnum = rem_port->portnum;
			links[num_links].combine_num = 1;
			num_links++;
		}
	}

	for (i = 0; i < num_links; i++) {
		if (links[i].combine_num > 1) {
			printf("   x%016lx -> x%016lx [label=\"%d\", "
				"arrowhead=\"none\", color=\"blue\"];\n",
				node->guid, links[i].rem_guid,
				links[i].combine_num);
		} else {
			printf("   x%016lx -> x%016lx "
				"[taillabel=\"%d\", headlabel=\"%d\", "
				"arrowhead=\"none\"];\n",
				node->guid, links[i].rem_guid,
				links[i].pnum, links[i].rem_pnum);
		}
	}

	free(node_name);
}

static void print_dot_file(ibnd_fabric_t *fabric)
{
	ibnd_node_t *node = fabric->nodes;
	while (node) {
		lfab.num_nodes++;
		node = node->next;
	}
	lfab.rank_sep = 1 + ((lfab.num_nodes) * 0.5/10);
	printf("digraph G {\n");
	printf("   node [shape=record, fontsize=9];\n");
	printf("   graph [outputorder=nodesfirst, ranksep=\"%d equally\"];\n",
		lfab.rank_sep);

	ibnd_iter_nodes(fabric, process_node, NULL);

	if (lfab.roots) {
		struct lnode *lnode;
		printf("{rank=min; ");
		for (lnode = lfab.roots; lnode; lnode = lnode->next) {
			printf("x%016lx; \n", lnode->node->guid);
		}
		printf("}\n");
	}

	if (lfab.leafs) {
		struct lnode *lnode;
		printf("{rank=max; ");
		for (lnode = lfab.leafs; lnode; lnode = lnode->next) {
			printf("x%016lx; \n", lnode->node->guid);
		}
		printf("}\n");
	}

	printf("}\n");
}

/** =========================================================================
*/
static ibnd_node_t *find_node(ibnd_fabric_t *fabric, char *name)
{
	ibnd_node_t node;
	for (node = fabric->nodes; node; node->next) {
		if (strcmp(node->nodedesc, name) == 0) {
			return (node);
		}
	}
	return (NULL);
}

/** =========================================================================
 */
static int
usage(void)
{
        fprintf(stderr,
"%s [options]\n"
"Usage: generate a slurm topology file to be fed to slurm for better node utilization\n"
"\n"
"Options:\n"
"  -S <guid> generate for the node specified by the port guid\n"
"  -G <guid> Same as \"-S\" for compatibility with other diags\n"
"  -D <dr_path> generate for the node specified by the DR path given\n"
"  --hops -n <hops> hops to progress away from node given in -G,-S, or -D\n"
"                   default == 0\n"
"  -e combine edges between nodes into one edge with an associated weight\n"
"  -g <cluster_name> DON'T print GUID's on nodes if labeled <cluster_name>N\n"
"  -r <regex> regex to identify root switches\n"
"  -i <regex> regex to identify nodes to ignore\n"
"  -M <node1:node2> plot route(s) from node1 to node2 in red\n"

"  --node-name-map <map> specify alternate node name map\n"
"  --Ca, -C <ca>         Ca name to use\n"
"  --Port, -P <port>     Ca port number to use\n"
"  --timeout, -t <ms>    timeout in ms\n"
"  --verbose, -v         increase verbosity level\n"
"  -R GNDN.  This option is here for backward compatibility\n"
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

	int mgmt_classes[4] = { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS,
		IB_PERFORMANCE_CLASS
	};
	char *ibd_ca = NULL;
	int ibd_ca_port = 0;
	int ibd_timeout = 200;

        char  ch = 0;
        static char const str_opts[] = "hS:G:D:r:ei:g:n:C:P:t:vM:";
        static const struct option long_opts [] = {
           {"help", 0, 0, 'h'},
	   {"node-name-map", 1, 0, 1},
	   {"hops", 1, 0, 'n'},
	   {"Ca", 1, 0, 'C'},
	   {"Port", 1, 0, 'P'},
	   {"timeout", 1, 0, 't'},
	   {"verbose", 1, 0, 'v'},
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
			case 'r':
				root_regex = strdup(optarg);
				break;
			case 'e':
				combine_edges = 1;
				break;
			case 'M':
			{
				char *tmp = strdup(optarg);
				fromname = strdup(strtok(tmp, ":"));
				toname = strdup(strtok(NULL, ":"));
				free(tmp);
				break;
			}
			case 'i':
				ignore_regex = strdup(optarg);
				break;
			case 'g':
				cluster_name = strdup(optarg);
				break;
			case 'n':
				config.max_hops = (int)strtol(optarg, NULL, 0);
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
			case 'R': /* noop */
				break;
                        case 'h':
                        default:
                        	exit(usage());
                }
	}

	ibmad_port = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 4);
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
		if (!config.max_hops)
			config.max_hops = 1;
		fabric = ibnd_discover_fabric(ibd_ca, ibd_ca_port, &portid, &config);
	} else {
		fabric = ibnd_discover_fabric(ibd_ca, ibd_ca_port, NULL, &config);
	}

	if (!fabric) {
		fprintf(stderr, "ibnd_discover_fabric failed\n");
		rc = -1;
		goto close_port;
	}

	if (fromname && toname) {
		fprintf(stderr, "Plotting from %s => to %s\n",
			fromname, toname);
		fromnode = find_node(fabric, fromname);
		tonode = find_node(fabric, toname);
		if (!fromnode || !tonode) {
			if (!fromnode)
				fprintf(stderr,
					"Error: %s not found in fabric\n",
					fromname);
			if (!tonode)
				fprintf(stderr,
					"Error: %s not found in fabric\n",
					toname);
			free(fromname);
			free(toname);
			goto destroy_fabric;
		}
		free(fromname);
		free(toname);
	}

	print_dot_file(fabric);

destroy_fabric:
	ibnd_destroy_fabric(fabric);

close_port:
	mad_rpc_close_port(ibmad_port);
	close_node_name_map(node_name_map);
	exit(rc);
}
