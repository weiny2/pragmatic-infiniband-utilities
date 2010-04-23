/*
 * Copyright (C) 2009 Lawrence Livermore National Security
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Written by Ira Weiny weiny2@llnl.gov
 * UCRL-CODE-235440
 * 
 * This file is part of pragmatic-infiniband-tools (PIU), usefull tools to manage
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#define _GNU_SOURCE
#include <ctype.h>
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

#include "hostlist.h"

#define BUF_SIZE 2048

#ifdef ENABLE_GENDERS
#include <genders.h>
char *g_genders_file = NULL;
char *g_query = "compute";
#endif

hostlist_t g_expected_host_list;
hostlist_t g_host_not_found_list;
char *expected_hosts = NULL;

char *argv0 = NULL;

static char *node_name_map_file = NULL;
static nn_map_t *node_name_map = NULL;

/* store the mapping done as a comment for the end of the file */
char *slurm_name_mapped_output = NULL;
int slurm_name_mapped_size = 0;
int g_num_levels = -1;


/** =========================================================================
 * Create a little hash of additional node info we need stored.
 */
#define HTSZ 137
typedef struct slurm_info {
	struct slurm_info *htnext;
	struct slurm_info *level_next;
	ibnd_node_t *node;
	int level;
	char *slurm_name;
} slurm_info_t;
slurm_info_t *info_hash[HTSZ] = { 0 };

#define MAX_DEPTH 65
slurm_info_t *levels[MAX_DEPTH]; /* also store by the level for easier sorting */

#define HASHGUID(guid) ((uint32_t)(((uint32_t)(guid) * 101) ^ ((uint32_t)((guid) >> 32) * 103)))

/** =========================================================================
 * Slurm requires very specific naming for it's config file.  If the fabric
 * node descriptor or the node name map does not follow a naming convention of:
 *
 * <prefix>X
 *
 * where X is a number this function will convert the name to "ibcoreSWX"
 */
char *get_name(ibnd_node_t *node, slurm_info_t *info)
{
	static int core_num = 1;
	static int reexp_compiled = 0;
	static regex_t exp;

	char *re_str = "^[[:alpha:]]*[0-9]*$";

	char buf[64];
	char comment[256];
	int rc = 0;

	/* we have already found this name */
	if (info->slurm_name)
		return (info->slurm_name);

	info->slurm_name = remap_node_name(node_name_map,
					node->guid,
					node->nodedesc);

	if (!reexp_compiled) { /* only compile it one time */
		if ((rc = regcomp(&exp, re_str, REG_ICASE |
				REG_NOSUB | REG_EXTENDED)) != 0) {
			fprintf(stderr, "ERROR: regcomp failed on \"%s\": %d\n",
				re_str, rc);
			return (info->slurm_name);
		}
		reexp_compiled = 1;
	}

	if (regexec(&exp, info->slurm_name, 0, NULL, 0) != 0) {
		snprintf(buf, 64, "ibcoreSW%d", core_num);
		core_num++;

		snprintf(comment, 256, "#    %s == %s\n",
			buf, info->slurm_name);
		comment[255] = '\0';
		slurm_name_mapped_size += strlen(comment);
		slurm_name_mapped_output = realloc(slurm_name_mapped_output,
						slurm_name_mapped_size);
		if (!slurm_name_mapped_output) {
			perror("ralloc BADNESS\n");
			exit(0);
		}
		strcat(slurm_name_mapped_output, comment);

		free(info->slurm_name);
		info->slurm_name = strdup(buf);
	}

	return (info->slurm_name);
}

/** =========================================================================
 * Compare the name n1 to n2 using <prefix>X notation
 * return TRUE if X of n1 is larger than X of n2
 * for example given n1 = host3 and n2 = host1 return 1
 * if n1 != <prefix>X || n2 != <prefix>X
 * Then return 0
 */
static int
name_larger(char *n1, char *n2)
{
	int i1 = 0, i2 = 0;
	int rc = 0;

	rc = strcspn(n1, "0123456789");
	if (rc == strlen(n2))
		return (0);
	i1 = atoi(&n1[rc]);

	rc = strcspn(n2, "0123456789");
	if (rc == strlen(n2))
		return (0);
	i2 = atoi(&n2[rc]);

	return (i1 > i2);
}

/** =========================================================================
 */
static void
add_info_to_level(slurm_info_t *info)
{
	slurm_info_t *level_head = levels[info->level];
	slurm_info_t *level_prev = NULL;

	/* sort these as we go */

	if (!level_head) {
		info->level_next = NULL;
		levels[info->level] = info;
		return;
	}

	while (level_head && name_larger(info->slurm_name, level_head->slurm_name)) {
		level_prev = level_head;
		level_head = level_head->level_next;
	}

	info->level_next = level_head;
	if (level_prev)
		level_prev->level_next = info;
	else
		levels[info->level] = info;
}

/** =========================================================================
 */
static void
add_info(ibnd_node_t *node, int level)
{
	int hash_idx = HASHGUID(node->guid) % HTSZ;
	slurm_info_t *info = NULL;

	assert(level < MAX_DEPTH);

	info = calloc(1, sizeof(*info));
	info->node = node;
	info->level = level;
	info->level_next = NULL;

	/* convert the name */
	get_name(node, info);

	/* keep track by level */
	add_info_to_level(info);

	/* keep track by hash */
	info->htnext = info_hash[hash_idx];
	info_hash[hash_idx] = info;
}
static slurm_info_t *
get_info(ibnd_node_t *node)
{
	int hash = HASHGUID(node->guid) % HTSZ;
	slurm_info_t *info;

	for (info = info_hash[hash]; info; info = info->htnext)
		if (info->node->guid == node->guid)
			return info;

	return NULL;
}

/** =========================================================================
 */
static void
resolve_genders(ibnd_node_t *node)
{
	slurm_info_t *slurm_info = get_info(node);
	hostlist_delete_host(g_host_not_found_list, get_name(node, slurm_info));
}

/** =========================================================================
 * create the level data
 */
void process_cas_level(ibnd_node_t *node, void *user_data)
{
	int i = 0;
 	/* Ca's are defined to be level 0 */
	add_info(node, 0);

	resolve_genders(node);

	/* and all switches attached to any nodes are level 1 */
	for (i = 1; i <= node->numports; i++) {
		if (node->ports[i] && node->ports[i]->remoteport) {
			ibnd_node_t *rem_node = node->ports[i]->remoteport->node;
			if (rem_node && !get_info(rem_node)) {
				if (rem_node->type == IB_NODE_SWITCH) {
					add_info(rem_node, 1);
				} else if (rem_node->type ==
					   IB_NODE_CA) {
					fprintf(stderr, "ERROR: Found a "
					"node connected to another node\n");
				}
			}
		}
	}
}

/** =========================================================================
 * finds all switches of level user_data.level and finds the next switches up.
 */
typedef struct proc_sw_level_user_data {
	int level;
	int node_found;
} proc_sw_level_user_data_t;
void process_switch_level(ibnd_node_t *node, void *user_data)
{
	int i = 0;
	slurm_info_t *info = get_info(node);
	int level = ((proc_sw_level_user_data_t *)user_data)->level;
	
	if (info && info->level == level) {
		for (i = 1; i <= node->numports; i++) {
			if (node->ports[i] && node->ports[i]->remoteport) {
				ibnd_node_t *rem_node = node->ports[i]->remoteport->node;
				if (rem_node && !get_info(rem_node)
				    && rem_node->type == IB_NODE_SWITCH) {
					add_info(rem_node, level + 1);
					((proc_sw_level_user_data_t *)user_data)->node_found = 1;
				}
			}
		}
	}
}

ibnd_node_t *get_remote_node(ibnd_node_t *node, int p, slurm_info_t **rem_info)
{
	ibnd_node_t *rem_node = NULL;
	if (node->ports[p] && node->ports[p]->remoteport) {
		rem_node = node->ports[p]->remoteport->node;
		*rem_info = get_info(rem_node);
	}
	return (rem_node);
}

/** =========================================================================
 */
static void
print_virtual_top(int level)
{
	hostlist_t last_level = hostlist_create(NULL);
	slurm_info_t *info = levels[level];
	char hl_str[BUF_SIZE];
	hl_str[0] = '\0';

	while (info) {
		hostlist_push_host(last_level, get_name(info->node, info));
		info = info->level_next;
	}

	printf("\n# \"Virtual Top\"\n");
	if (hostlist_ranged_string(last_level, BUF_SIZE, hl_str) > 0) {
		printf("SwitchName=VirtualTop1 Switches=%s\n", hl_str);
	} else {
		printf("ERROR creation of virtual top failed; hostlist_ranged_string\n");
	}
}

/** =========================================================================
 */
static void print_switches(void)
{
	hostlist_t sw_list = hostlist_create(NULL);
	char sw_hl_str[BUF_SIZE];
	hostlist_t n_list = hostlist_create(NULL);
	char n_hl_str[BUF_SIZE];
	hostlist_iterator_t it;
	char *name = NULL;

	ibnd_node_t *node;
	ibnd_node_t *rem_node;
	slurm_info_t *rem_info;

	int level = 1;
	int i = 0;
	char *node_name = NULL;
	char *rem_node_name = NULL;

	for (level = 1; levels[level]; level++) {
		/* only report the number of levels the user wants */
		if (g_num_levels != -1 && level > g_num_levels) {
			print_virtual_top(level-1);
			break;
		}

		printf("\n# Begin switches at level %d\n", level);
		slurm_info_t *info = levels[level];
		while (info) {
			node = info->node;

			node_name = get_name(node, info);
			printf("SwitchName=%s ", node_name);

			for (i = 1; i <= node->numports; i++) {
				rem_node = get_remote_node(node, i, &rem_info);
				if (!rem_node)
					continue;

				rem_node_name = get_name(rem_node, rem_info);

				/* find all the down (direction) ports */
				if (rem_info->level < level) {
					if (rem_node->type == IB_NODE_SWITCH) {
						hostlist_push(sw_list, rem_node_name);
					} else if (rem_node->type == IB_NODE_CA) {
						hostlist_push(n_list, rem_node_name);
					}
				} else if (rem_info->level == level) {
					fprintf(stderr, "WARNING: found "
					"%s at equal level to %s\n",
					node_name,
					rem_node_name);
				}
			}

			hostlist_sort(sw_list);
			hostlist_sort(n_list);

			if (hostlist_ranged_string(sw_list, BUF_SIZE, sw_hl_str) > 0)
				printf("Switches=%s ", sw_hl_str);

			if (hostlist_ranged_string(n_list, BUF_SIZE, n_hl_str) > 0)
				printf("Nodes=%s ", n_hl_str);

			it = hostlist_iterator_create(sw_list);
			for (name = hostlist_next(it);
				name != NULL; name = hostlist_next(it)) {
				hostlist_remove(it);
			}
			hostlist_iterator_destroy(it);

			it = hostlist_iterator_create(n_list);
			for (name = hostlist_next(it);
				name != NULL; name = hostlist_next(it)) {
				hostlist_remove(it);
			}
			hostlist_iterator_destroy(it);

			printf("\n");

			info = info->level_next;
		}
		printf("# End switches at level %d\n", level);
	}
}

/** =========================================================================
 */
static void print_missing_hosts(void)
{
	char hl_str[BUF_SIZE];
	hl_str[0] = '\0';

	if (hostlist_is_empty(g_host_not_found_list))
		return;

	hostlist_sort(g_host_not_found_list);

	/* print this to both the output file and stderr */
	printf("\n#\n# ERROR: failed to find these expected nodes in the fabric: ");
	if (hostlist_ranged_string(g_host_not_found_list, BUF_SIZE, hl_str) > 0) {
		printf("%s\n", hl_str);
	} else {
		printf("(ERROR \"hostlist_ranged_string\" failed)\n");
	}
	printf("#\n");

	fprintf(stderr,
		"\nERROR: failed to find these expected nodes in the fabric: %s\n",
		hl_str);
}

static void print_header(void)
{
	time_t ltime;
	ltime=time(NULL);

	printf("# Slurm Topology %s", asctime(localtime(&ltime)));

	printf("# Expected hosts: ");
	if (hostlist_is_empty(g_expected_host_list))
		printf("<none>\n");
	else {
		char hl_str[BUF_SIZE];
		hostlist_sort(g_expected_host_list);
		if (hostlist_ranged_string(g_expected_host_list, BUF_SIZE, hl_str) > 0)
			printf("%s\n", hl_str);
		else
			printf("<ERROR \"hostlist_ranged_string\" failed>\n");
	}

	print_missing_hosts();
}

/** =========================================================================
 */
static void process_fabric(ibnd_fabric_t *fabric)
{
	proc_sw_level_user_data_t user_data;

	slurm_name_mapped_output = malloc(1);
	slurm_name_mapped_output[0] = '\0';
	slurm_name_mapped_size = 1;

	ibnd_iter_nodes_type(fabric, process_cas_level, IB_NODE_CA, NULL);

	user_data.level = 0;
	user_data.node_found = 0;

	do {
		user_data.level++;
		user_data.node_found = 0;
		ibnd_iter_nodes_type(fabric, process_switch_level,
				     IB_NODE_SWITCH, &user_data);
	} while (user_data.node_found);

	print_header();
	print_switches();

	printf("\n# Slurm Remapped Names\n%s", slurm_name_mapped_output);
}

/** =========================================================================
 */
static void load_expected_host_list(void)
{
#ifdef ENABLE_GENDERS
	genders_t genders;
	char **node_list;
	int node_list_len;
	int i, n;

	if (expected_hosts) {
		g_expected_host_list = hostlist_create(expected_hosts);
		goto done;
	}

	genders = genders_handle_create();

	if (genders_load_data(genders, g_genders_file))
		fprintf(stderr, "ERROR: genders load data failed: %s\n",
			genders_errormsg(genders));

	if ((node_list_len =
	     genders_nodelist_create(genders, &node_list)) < 0)
		fprintf(stderr, "ERROR: genders nodelist create failed: %s\n",
			genders_errormsg(genders));

	if ((n = genders_query(genders, node_list,
			  node_list_len, g_query)) < 0)
		fprintf(stderr,
			"ERROR: genders query failed: %s\n",
			genders_errormsg(genders));

	g_expected_host_list = hostlist_create(NULL);

	for (i = 0 ; i < n; i++)
		hostlist_push_host(g_expected_host_list, node_list[i]);

	genders_nodelist_destroy(genders, node_list);
	genders_handle_destroy(genders);
#else /* !ENABLE_GENDERS */
	g_expected_host_list = hostlist_create(expected_hosts);
#endif /* ENABLE_GENDERS */

done:
	g_host_not_found_list = hostlist_copy(g_expected_host_list);
}

/** =========================================================================
 */
static void destroy_expected_host_list(void)
{
	hostlist_destroy(g_expected_host_list);
	hostlist_destroy(g_host_not_found_list);
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
"  --num-levels, -n <num_levels> limit the output to the specified number of levels\n"
"  --hosts <hostlist> specify hosts to be found"
#ifdef ENABLE_GENDERS
"; overrides genders queries\n"
"  --genders, -g <genders_file> specified an alternate genders file\n"
"  --genders-query, -q <query> specify a node query to genders (default: \"%s\")\n"
"                              \"man genders_query\" for query examples\n"
#else /* !ENABLE_GENDERS */
"\n"
#endif /* ENABLE_GENDERS */
"  --node-name-map <map> specify alternate node name map\n"
"  --Ca, -C <ca>         Ca name to use\n"
"  --Port, -P <port>     Ca port number to use\n"
"  --timeout, -t <ms>    timeout in ms\n"
"  --verbose, -v         increase verbosity level\n"
"\n"
"  NOTE: This utility ignores Routers\n"
, argv0
#ifdef ENABLE_GENDERS
, g_query
#endif /* ENABLE_GENDERS */
);
        return (0);
}

/** =========================================================================
 */
int main(int argc, char **argv)
{
	int rc = 0;
	ibnd_fabric_t *fabric = NULL;
	struct ibmad_port *ibmad_port;
	int mgmt_classes[3] =
	    { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS };
	char *ibd_ca = NULL;
	int ibd_ca_port = 0;
	int ibd_timeout = 200;

        char  ch = 0;

#ifdef ENABLE_GENDERS
        static char const str_opts[] = "hC:P:t:vn:g:q:";
#else /* !ENABLE_GENDERS */
        static char const str_opts[] = "hC:P:t:vn:";
#endif /* ENABLE_GENDERS */

        static const struct option long_opts [] = {
           {"help", 0, 0, 'h'},
	   {"num-levels", 'n', 0, 1},
	   {"node-name-map", 1, 0, 1},
	   {"hosts", 1, 0, 2},
#ifdef ENABLE_GENDERS
	   {"genders", 'g', 0, 1},
	   {"genders-query", 'q', 0, 1},
#endif /* ENABLE_GENDERS */
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
			case 1:
				node_name_map_file = strdup(optarg);
				break;
			case 2:
				expected_hosts = strdup(optarg);
				break;
			case 'n':
				g_num_levels = atoi(optarg);
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
#ifdef ENABLE_GENDERS
			case 'g':
				g_genders_file = strdup(optarg);
				break;
			case 'q':
				g_query = strdup(optarg);
				break;
#endif /* ENABLE_GENDERS */
                        case 'h':
                        default:
                        	exit(usage());
                }
	}

	ibmad_port = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!ibmad_port) {
		fprintf(stderr, "Failed to open %s port %d", ibd_ca,
			ibd_ca_port);
		exit(1);
	}

	if (ibd_timeout)
		mad_rpc_set_timeout(ibmad_port, ibd_timeout);

	load_expected_host_list();
	node_name_map = open_node_name_map(node_name_map_file);

	fabric = ibnd_discover_fabric(ibmad_port, NULL, -1);

	process_fabric(fabric);

	ibnd_destroy_fabric(fabric);

	close_node_name_map(node_name_map);
	destroy_expected_host_list();
	mad_rpc_close_port(ibmad_port);
	exit(rc);
}
