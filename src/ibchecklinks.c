/*
 * Copyright (c) 2010 Lawrence Livermore National Security.  All rights reserved.
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

#include <complib/cl_nodenamemap.h>
#include <infiniband/ibnetdisc.h>
#include <infiniband/ibfabricconf.h>

#include "hostlist.h"

static char *node_name_map_file = NULL;
static nn_map_t *node_name_map = NULL;
static ibfc_conf_t *fabricconf = NULL;
static char *fabricconffile = NULL;
static char *ibd_ca = NULL;
static int ibd_ca_port = 1;
static int ibd_timeout = 100;

static char *downhosts = NULL;
hostlist_t downhosts_list;

static int smlid = 0;

static uint64_t guid = 0;
static char *guid_str = NULL;
static char *dr_path = NULL;
static char *argv0 = NULL;
static int all = 0;

/* Global to set return code of utility when error is found */
static int check_node_rc = 0;

static struct {
	int num_ports;
	int pn_down;
	int pn_init;
	int pn_armed;
	int pn_active;
	int pn_disabled;
	int pn_sdr;
	int pn_ddr;
	int pn_qdr;
	int pn_fdr;
	int pn_edr;
	int pn_1x;
	int pn_4x;
	int pn_8x;
	int pn_12x;
	int pn_undef;
} totals = {
	num_ports   : 0,
	pn_down     : 0,
	pn_init     : 0,
	pn_armed    : 0,
	pn_active   : 0,
	pn_disabled : 0,
	pn_sdr      : 0,
	pn_ddr      : 0,
	pn_qdr      : 0,
	pn_fdr      : 0,
	pn_edr      : 0,
	pn_1x       : 0,
	pn_4x       : 0,
	pn_8x       : 0,
	pn_12x      : 0,
	pn_undef    : 0
};

typedef struct port_vis {
	struct port_vis *next;
	uint64_t guid;
	int pnum;
} port_vis_t;

port_vis_t *vis_head = NULL;

void mark_seen(uint64_t guid, int pnum)
{
	port_vis_t *tmp = calloc(1, sizeof *tmp);
	if (!tmp) {
		fprintf(stderr, "calloc failure\n");
		exit(1);
	}
	tmp->guid = guid;
	tmp->pnum = pnum;
	tmp->next = vis_head;
	vis_head = tmp;
}

int port_seen(uint64_t guid, int pnum)
{
	port_vis_t *cur;
	for (cur = vis_head; cur; cur = cur->next) {
		if (guid == cur->guid && pnum == cur->pnum)
			return (1);
	}
	return (0);
}

void free_seen(void)
{
	port_vis_t *cur = vis_head;
	while (cur) {
		port_vis_t *tmp = cur;
		cur = cur->next;
		free(tmp);
	}
}

void
print_port_stats(void)
{
	printf("Stats Summary: (%d total ports)\n", totals.num_ports);
	if (totals.pn_down)
		printf("   %d down ports(s)\n", totals.pn_down);
	if (totals.pn_disabled)
		printf("   %d disabled ports(s)\n", totals.pn_disabled);
	if (totals.pn_1x)
		printf("   %d link(s) at 1X\n", totals.pn_1x);
	if (totals.pn_4x)
		printf("   %d link(s) at 4X\n", totals.pn_4x);
	if (totals.pn_8x)
		printf("   %d link(s) at 8X\n", totals.pn_8x);
	if (totals.pn_12x)
		printf("   %d link(s) at 12X\n", totals.pn_12x);
	if (totals.pn_sdr)
		printf("   %d link(s) at 2.5 Gbps (SDR)\n", totals.pn_sdr);
	if (totals.pn_ddr)
		printf("   %d link(s) at 5.0 Gbps (DDR)\n", totals.pn_ddr);
	if (totals.pn_qdr)
		printf("   %d link(s) at 10.0 Gbps (QDR)\n", totals.pn_qdr);
	if (totals.pn_fdr)
		printf("   %d link(s) at 14.0 Gbps (FDR)\n", totals.pn_fdr);
	if (totals.pn_edr)
		printf("   %d link(s) at 25.0 Gbps (EDR)\n", totals.pn_edr);
}


static unsigned int get_max(unsigned int num)
{
	unsigned r = 0;		// r will be lg(num)

	while (num >>= 1)	// unroll for more speed...
		r++;

	return (1 << r);
}

void get_msg(char *width_msg, char *speed_msg, int msg_size, ibnd_port_t * port)
{
	char buf[64];
	uint32_t max_speed = 0;

	uint32_t max_width = get_max(mad_get_field(port->info, 0,
						   IB_PORT_LINK_WIDTH_SUPPORTED_F)
				     & mad_get_field(port->remoteport->info, 0,
						     IB_PORT_LINK_WIDTH_SUPPORTED_F));
	if ((max_width & mad_get_field(port->info, 0,
				       IB_PORT_LINK_WIDTH_ACTIVE_F)) == 0)
		// we are not at the max supported width
		// print what we could be at.
		snprintf(width_msg, msg_size, "Could be %s",
			 mad_dump_val(IB_PORT_LINK_WIDTH_ACTIVE_F,
				      buf, 64, &max_width));

	max_speed = get_max(mad_get_field(port->info, 0,
					  IB_PORT_LINK_SPEED_SUPPORTED_F)
			    & mad_get_field(port->remoteport->info, 0,
					    IB_PORT_LINK_SPEED_SUPPORTED_F));
	if ((max_speed & mad_get_field(port->info, 0,
				       IB_PORT_LINK_SPEED_ACTIVE_F)) == 0)
		// we are not at the max supported speed
		// print what we could be at.
		snprintf(speed_msg, msg_size, "Could be %s",
			 mad_dump_val(IB_PORT_LINK_SPEED_ACTIVE_F,
				      buf, 64, &max_speed));
}

void print_port(char *node_name, ibnd_node_t * node, ibnd_port_t * port,
		ibfc_port_t *portconf, int inc_attributes)
{
	char width[64], speed[64], state[64], physstate[64];
	char remote_guid_str[256];
	char remote_str[256];
	char link_str[256];
	char width_msg[256];
	char speed_msg[256];
	char ext_port_str[256];
	int iwidth, ispeed, istate, iphystate;
	int n = 0;

	if (!port)
		return;

	iwidth = mad_get_field(port->info, 0, IB_PORT_LINK_WIDTH_ACTIVE_F);
	ispeed = mad_get_field(port->info, 0, IB_PORT_LINK_SPEED_ACTIVE_F);
	istate = mad_get_field(port->info, 0, IB_PORT_STATE_F);
	iphystate = mad_get_field(port->info, 0, IB_PORT_PHYS_STATE_F);

	remote_guid_str[0] = '\0';
	remote_str[0] = '\0';
	link_str[0] = '\0';
	width_msg[0] = '\0';
	speed_msg[0] = '\0';

	if (inc_attributes)
	{
		/* C14-24.2.1 states that a down port allows for invalid data to be
		 * returned for all PortInfo components except PortState and
		 * PortPhysicalState */
		if (istate != IB_LINK_DOWN) {
			n = snprintf(link_str, 256, "(%s %s %6s/%8s)",
			     mad_dump_val(IB_PORT_LINK_WIDTH_ACTIVE_F, width, 64,
					  &iwidth),
			     mad_dump_val(IB_PORT_LINK_SPEED_ACTIVE_F, speed, 64,
					  &ispeed),
			     mad_dump_val(IB_PORT_STATE_F, state, 64, &istate),
			     mad_dump_val(IB_PORT_PHYS_STATE_F, physstate, 64,
					  &iphystate));
		} else {
			n = snprintf(link_str, 256, "(%6s/%8s)",
			     mad_dump_val(IB_PORT_STATE_F, state, 64, &istate),
			     mad_dump_val(IB_PORT_PHYS_STATE_F, physstate, 64,
					  &iphystate));
		}
	}

	if (port->remoteport) {
		char *remap =
		    remap_node_name(node_name_map, port->remoteport->node->guid,
				    port->remoteport->node->nodedesc);

		if (port->remoteport->ext_portnum)
			snprintf(ext_port_str, 256, "%d",
				 port->remoteport->ext_portnum);
		else
			ext_port_str[0] = '\0';

		get_msg(width_msg, speed_msg, 256, port);

		snprintf(remote_guid_str, 256,
			"0x%016" PRIx64 " ",
			port->remoteport->node->guid);

		snprintf(remote_str, 256, "%s%6d %4d[%2s] \"%s\" (%s %s)\n",
			 remote_guid_str, port->remoteport->base_lid ?
			 port->remoteport->base_lid :
			 port->remoteport->node->smalid,
			 port->remoteport->portnum, ext_port_str, remap,
			 width_msg, speed_msg);
		free(remap);
	} else if (portconf) {
		char prop[256];
		if (ibfc_port_num_dont_care(portconf))
			snprintf(remote_str, 256,
			" <don't care> \"%s\" (Should be: %s Active)\n",
				ibfc_port_get_name(portconf),
				ibfc_prop_str(portconf, prop, 256));
		else
			snprintf(remote_str, 256,
			" %4d \"%s\" (Should be: %s Active)\n",
				ibfc_port_get_port_num(portconf),
				ibfc_port_get_name(portconf),
				ibfc_prop_str(portconf, prop, 256));
	} else
		snprintf(remote_str, 256, " [  ] \"\" ( )\n");

	if (port->ext_portnum)
		snprintf(ext_port_str, 256, "%d", port->ext_portnum);
	else
		ext_port_str[0] = '\0';

	printf("0x%016" PRIx64 " \"%s\" ", node->guid, node_name);
	if (link_str[0] != '\0')
		printf("%6d %4d[%2s] <==%s==>  %s",
			port->base_lid ?  port->base_lid : port->node->smalid,
			port->portnum, ext_port_str, link_str, remote_str);
	else
		printf("%6d %4d[%2s] <==>  %s",
			port->base_lid ?  port->base_lid : port->node->smalid,
			port->portnum, ext_port_str, remote_str);
}

void
print_config_port(ibfc_port_t *port)
{
	printf ("\"%s\" %d  <==>  ",
		ibfc_port_get_name(port),
		ibfc_port_get_port_num(port));

	if (ibfc_port_num_dont_care(ibfc_port_get_remote(port)))
		printf ("<don't care> ");
	else
		printf ("%4d ",
			ibfc_port_get_port_num(ibfc_port_get_remote(port)));

	printf ("\"%s\"\n",
		ibfc_port_get_name(ibfc_port_get_remote(port)));
}

void compare_port(ibfc_port_t *portconf, char *node_name, ibnd_node_t *node, ibnd_port_t *port)
{
	int iwidth, ispeed, istate, iphysstate;

	iwidth = mad_get_field(port->info, 0, IB_PORT_LINK_WIDTH_ACTIVE_F);
	ispeed = mad_get_field(port->info, 0, IB_PORT_LINK_SPEED_ACTIVE_F);
	istate = mad_get_field(port->info, 0, IB_PORT_STATE_F);
	iphysstate = mad_get_field(port->info, 0, IB_PORT_PHYS_STATE_F);

	ibfc_port_t *rem_portconf = ibfc_port_get_remote(portconf);

	if (istate != IB_LINK_ACTIVE) {
		int print = 0;
		int hostdown = 1;

		if (downhosts && hostlist_find(downhosts_list,
				ibfc_port_get_name(rem_portconf)) == -1)
			hostdown = 0;

		if (iphysstate == IB_PORT_PHYS_STATE_DISABLED) {
			printf("ERR: port disabled");
			if (downhosts && !hostdown)
				printf(" (host UP): ");
			else
				printf(": ");
			print = 1;
		} else {
			if (!downhosts || !hostdown) {
				printf("ERR: port down     : ");
				print = 1;
			}
		}
		if (print) {
			print_port(node_name, node, port, rem_portconf, 0);
			check_node_rc = 1;
		}
	} else {
		char str[64];
		int conf_width = ibfc_prop_get_width(
					ibfc_port_get_prop(portconf));
		int conf_speed = ibfc_prop_get_speed(
					ibfc_port_get_prop(portconf));
		int rem_port_num = ibfc_port_get_port_num(rem_portconf);
		char *rem_node_name = ibfc_port_get_name(rem_portconf);
		char *rem_remap = NULL;
		ibnd_port_t *remport = port->remoteport;

		if (iwidth != conf_width) {
			printf("ERR: width != %s: ",
				mad_dump_val(IB_PORT_LINK_WIDTH_ACTIVE_F,
					str, 64, &conf_width));
			print_port(node_name, node, port, NULL, 1);
			check_node_rc = 1;
		}
		if (ispeed != conf_speed) {
			printf("ERR: speed != %s: ",
				mad_dump_val(IB_PORT_LINK_SPEED_ACTIVE_F,
					str, 64, &conf_speed));
			print_port(node_name, node, port, NULL, 1);
			check_node_rc = 1;
		}

		if (remport) {
			rem_remap = remap_node_name(node_name_map,
					port->remoteport->node->guid,
					port->remoteport->node->nodedesc);
			if ((!ibfc_port_name_dont_care(rem_portconf) &&
			    strcmp(rem_node_name, rem_remap) != 0)
			    ||
			    (!ibfc_port_num_dont_care(rem_portconf) &&
			    rem_port_num != port->remoteport->portnum)) {
				printf("ERR: invalid link : ");
				print_port(node_name, node, port, NULL, 0);
				printf("     Should be    : ");
				print_config_port(portconf);
				check_node_rc = 1;
			}
			free(rem_remap);
		} else {
			fprintf(stderr, "ERR: query failure: ");
			print_port(node_name, node, port, rem_portconf, 1);
			check_node_rc = 1;
		}
	}
}

void check_config(char *node_name, ibnd_node_t *node, ibnd_port_t *port)
{
	int istate;
	ibfc_port_t *portconf = NULL;

	portconf = ibfc_get_port(fabricconf, node_name, port->portnum);
	istate = mad_get_field(port->info, 0, IB_PORT_STATE_F);
	if (portconf) {
		compare_port(portconf, node_name, node, port);
	} else if (istate == IB_LINK_ACTIVE) {
		char *remote_name = NULL;
		ibnd_node_t *remnode;
		ibnd_port_t *remport = port->remoteport;
		if (!remport) {
			fprintf(stderr, "ERROR: ibnd error; port ACTIVE "
					"but no remote port! (Lights on, "
					"nobody home???)\n");
			check_node_rc = 1;
			goto invalid_active;
		}
		remnode = remport->node;
		remote_name = remap_node_name(node_name_map, remnode->guid,
					remnode->nodedesc);
		portconf = ibfc_get_port(fabricconf, remote_name, remport->portnum);
		if (portconf) {
			compare_port(portconf, remote_name, remnode, remport);
		} else {
invalid_active:
			printf("ERR: Unconfigured active link: ");
			print_port(node_name, node, port, NULL, 1);
			check_node_rc = 1;
		}
		free(remote_name); /* OK; may be null */
	}
}

void check_port(char *node_name, ibnd_node_t * node, ibnd_port_t * port)
{
	int iwidth, ispeed, istate, iphysstate;
	int n_undef = totals.pn_undef;

	totals.num_ports++;

	iwidth = mad_get_field(port->info, 0, IB_PORT_LINK_WIDTH_ACTIVE_F);
	ispeed = mad_get_field(port->info, 0, IB_PORT_LINK_SPEED_ACTIVE_F);
	istate = mad_get_field(port->info, 0, IB_PORT_STATE_F);
	iphysstate = mad_get_field(port->info, 0, IB_PORT_PHYS_STATE_F);

	switch (istate) {
		case IB_LINK_DOWN: totals.pn_down++; break;
		case IB_LINK_INIT: totals.pn_init++; break;
		case IB_LINK_ARMED: totals.pn_armed++; break;
		case IB_LINK_ACTIVE: totals.pn_active++; break;
		default:  totals.pn_undef++; break;
	}

	switch (iphysstate) {
		case IB_PORT_PHYS_STATE_DISABLED: totals.pn_disabled++; break;
		default: break;
	}

	if (istate == IB_LINK_ACTIVE) {
		switch (iwidth) {
			case IB_LINK_WIDTH_ACTIVE_1X: totals.pn_1x++; break;
			case IB_LINK_WIDTH_ACTIVE_4X: totals.pn_4x++; break;
			case IB_LINK_WIDTH_ACTIVE_8X: totals.pn_8x++; break;
			case IB_LINK_WIDTH_ACTIVE_12X: totals.pn_12x++; break;
			default:  totals.pn_undef++; break;
		}
		switch (ispeed) {
			case IB_LINK_SPEED_ACTIVE_2_5: totals.pn_sdr++; break;
			case IB_LINK_SPEED_ACTIVE_5: totals.pn_ddr++; break;
			case IB_LINK_SPEED_ACTIVE_10: totals.pn_qdr++; break;
			default:  totals.pn_undef++; break;
		}
	}

	if (totals.pn_undef > n_undef) {
		printf("WARN: Undefined value found: ");
		print_port(node_name, node, port, NULL, 1);
		check_node_rc = 1;
	}

	if (fabricconf)
		check_config(node_name, node, port);
}

void check_smlid(ibnd_port_t *port)
{
	int checklid = 0;
	char *remap = NULL;

	if (!port)
		return;

	if (port->node->type == IB_NODE_SWITCH)
		port = port->node->ports[0];

	remap = remap_node_name(node_name_map, port->node->guid,
				    port->node->nodedesc);

	checklid = mad_get_field(port->info, 0, IB_PORT_SMLID_F);

	if (smlid != checklid) {
		printf("ERROR smlid %d != specified %d on node %s\n", checklid,
				smlid, remap);
		check_node_rc = 1;
	}

	free(remap);
}

void check_node(ibnd_node_t * node, void *user_data)
{
	int i = 0;
	char *remap =
	    remap_node_name(node_name_map, node->guid, node->nodedesc);

	for (i = 1; i <= node->numports; i++) {
		ibnd_port_t *port = node->ports[i];
		if (!port)
			continue;
		if (smlid)
			check_smlid(port->remoteport);
		if (!port_seen(node->guid, i)) {
			check_port(remap, node, port);
			mark_seen(node->guid, i);
			if (port->remoteport) {
				mark_seen(port->remoteport->node->guid,
					port->remoteport->portnum);
				totals.num_ports++;
			}
		}
	}
	free(remap);
}


int usage(void)
{
        fprintf(stderr,
"%s [options]\n"
"Usage: Check fabric and compare to fabric config file\n"
"\n"
"Options:\n"
"  --config, -c <ibfabricconf> Specify an alternate fabric config file (default: %s)\n"
"  --warn_dup, -w print warning about duplicate links found in fabric config\n"
"  --downhosts <hostlist> specify hosts which are known to be off"
"  -S <guid> generate for the node specified by the port guid\n"
"  -G <guid> Same as \"-S\" for compatibility with other diags\n"
"  -D <dr_path> generate for the node specified by the DR path given\n"
"  --outstanding_smps, -o <outstanding_smps> specify the number of outstanding smps on the wire\n"
"  --hops -n <hops> hops to progress away from node given in -G,-S, or -D\n"
"                   default == 0\n"
"  --node-name-map <map> specify alternate node name map\n"
"  --Ca, -C <ca>         Ca name to use\n"
"  --Port, -P <port>     Ca port number to use\n"
"  --timeout, -t <ms>    timeout in ms\n"
"  --sm <smlid>    specify an sm lid to check for.\n"
"\n"
, argv0
, IBFC_DEF_CONFIG
);
        return (0);
}

int main(int argc, char **argv)
{
	ib_portid_t sm_portid;
        char  ch = 0;
	struct ibnd_config config = { 0 };
	int resolved = -1;
	int warn_dup = 0;
	ibnd_fabric_t *fabric = NULL;
	struct ibmad_port *ibmad_port;
	ib_portid_t port_id = { 0 };
	int mgmt_classes[3] =
	    { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS };

        static char const str_opts[] = "hS:G:D:n:C:P:t:vo:lc:w";
        static const struct option long_opts [] = {
		{"help", 0, 0, 'h'},
		{"node-name-map", 1, 0, 1},
		{"hops", 1, 0, 'n'},
		{"downhosts", 1, 0, 2},
		{"Ca", 1, 0, 'C'},
		{"Port", 1, 0, 'P'},
		{"timeout", 1, 0, 't'},
		{"outstanding_smps", 1, 0, 'o'},
		{"config", 1, 0, 'c'},
		{"sm", 1, 0, 3},
		{0, 0, 0, 0}
        };

	argv0 = argv[0];

        while ((ch = getopt_long(argc, argv, str_opts, long_opts, NULL))
                != -1)
        {
                switch (ch)
                {
			case 1:
				node_name_map_file = strdup(optarg);
				break;
			case 2:
				downhosts = strdup(optarg);
				break;
			case 3:
				smlid = atoi(optarg);
				break;
			case 'S':
			case 'G':
				guid_str = strdup(optarg);
				guid = (uint64_t) strtoull(guid_str, 0, 0);
				break;
			case 'D':
				dr_path = strdup(optarg);
				break;
			case 'n':
				config.max_hops = strtoul(optarg, NULL, 0);
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
			case 'o':
				config.max_smps = atoi(optarg);
				break;
			case 'c':
				fabricconffile = strdup(optarg);
				break;
			case 's':
				/* srcport is not required when resolving via IB_DEST_LID */
				if (ib_resolve_portid_str_via(&sm_portid, optarg, IB_DEST_LID,
							      0, NULL) < 0)
					fprintf(stderr, "cannot resolve SM destination port %s",
						optarg);
				break;
			case 'w':
				warn_dup = 1;
				break;
                        case 'h':
                        default:
				exit(usage());
                }
	}

	argc -= optind;
	argv += optind;

	downhosts_list = hostlist_create(downhosts);

	fabricconf = ibfc_alloc_conf();
	if (!fabricconf) {
		fprintf(stderr, "ERROR: Failed to alloc fabricconf\n");
		exit(1);
	}
	ibfc_set_warn_dup(fabricconf, warn_dup);

	if (ibfc_parse_file(fabricconffile, fabricconf)) {
		fprintf(stderr, "WARN: Failed to parse link config file...\n");
		ibfc_free(fabricconf);
		fabricconf = NULL;
		check_node_rc = 1;
	}

	ibmad_port = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!ibmad_port) {
		fprintf(stderr, "Failed to open %s port %d", ibd_ca,
			ibd_ca_port);
		exit(1);
	}

	if (ibd_timeout) {
		mad_rpc_set_timeout(ibmad_port, ibd_timeout);
		config.timeout_ms = ibd_timeout;
	}

	node_name_map = open_node_name_map(node_name_map_file);

	if (dr_path) {
		/* only scan part of the fabric */
		if ((resolved =
		     ib_resolve_portid_str_via(&port_id, dr_path,
					       IB_DEST_DRPATH, NULL,
					       ibmad_port)) < 0)
			IBWARN("Failed to resolve %s; attempting full scan\n",
			       dr_path);
	} else if (guid_str) {
		if ((resolved =
		     ib_resolve_portid_str_via(&port_id, guid_str, IB_DEST_GUID,
					       NULL, ibmad_port)) < 0)
			IBWARN("Failed to resolve %s; attempting full scan\n",
			       guid_str);
	}

	if (resolved >= 0 &&
	    !(fabric =
	      ibnd_discover_fabric(ibd_ca, ibd_ca_port, &port_id, &config)))
		IBWARN("Single node discover failed;"
		       " attempting full scan\n");

	if (!fabric &&
	    !(fabric = ibnd_discover_fabric(ibd_ca, ibd_ca_port, NULL, &config))) {
		fprintf(stderr, "discover failed on %s:%d\n", ibd_ca, ibd_ca_port);
		check_node_rc = 1;
		goto close_port;
	}

	printf("Collecting port information:\n");

	if (!all && guid_str) {
		ibnd_node_t *node = ibnd_find_node_guid(fabric, guid);
		if (node)
			check_node(node, NULL);
		else
			fprintf(stderr, "Failed to find node: %s\n",
				guid_str);
	} else if (!all && dr_path) {
		ibnd_node_t *node = NULL;
		uint8_t ni[IB_SMP_DATA_SIZE];

		if (!smp_query_via(ni, &port_id, IB_ATTR_NODE_INFO, 0,
				   ibd_timeout, ibmad_port))
			return -1;
		mad_decode_field(ni, IB_NODE_GUID_F, &(guid));

		node = ibnd_find_node_guid(fabric, guid);
		if (node)
			check_node(node, NULL);
		else
			fprintf(stderr, "Failed to find node: %s\n", dr_path);
	} else {
		/* We are wanting to check all nodes on the fabric,
		 * however all nodes must be connected to switch.  This utility
		 * does is not very useful on a point to point link.
		 */
		ibnd_iter_nodes_type(fabric, check_node, IB_NODE_SWITCH, NULL);
	}

	ibnd_destroy_fabric(fabric);
	hostlist_destroy(downhosts_list);

	print_port_stats();

close_port:
	close_node_name_map(node_name_map);
	mad_rpc_close_port(ibmad_port);
	ibfc_free(fabricconf);
	free_seen();
	exit(check_node_rc);
}
