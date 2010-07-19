/*
 * Copyright (C) 2007 The Regents of the University of California.
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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>

#define COPY_PORT (10000)

/** =========================================================================
 * Data structures
 */

/** =========================================================================
 * Global data.
 */
enum
{
	ALL,
	LID_ONLY,
	GID_ONLY
} mode = ALL;

char *argv0 = NULL;


/** =========================================================================
 * helper functions to print nice pretty strings...
 */
static const char *
event_type_str(enum rdma_cm_event_type event)
{
        switch (event)
        {
           case RDMA_CM_EVENT_ADDR_RESOLVED: return ("Addr resolved");
           case RDMA_CM_EVENT_ADDR_ERROR: return ("Addr Error");
           case RDMA_CM_EVENT_ROUTE_RESOLVED: return ("Route resolved");
           case RDMA_CM_EVENT_ROUTE_ERROR: return ("Route Error");
           case RDMA_CM_EVENT_CONNECT_REQUEST: return ("Connect request");
           case RDMA_CM_EVENT_CONNECT_RESPONSE: return ("Connect response");
           case RDMA_CM_EVENT_CONNECT_ERROR: return ("Connect Error");
           case RDMA_CM_EVENT_UNREACHABLE: return ("Unreachable");
           case RDMA_CM_EVENT_REJECTED: return ("Rejected");
           case RDMA_CM_EVENT_ESTABLISHED: return ("Established");
           case RDMA_CM_EVENT_DISCONNECTED: return ("Disconnected");
           case RDMA_CM_EVENT_DEVICE_REMOVAL: return ("Device removal");
           default: return ("Unknown");
        }
        return ("Unknown");
}

#if 0
static const char *
qp_state_str(enum ibv_qp_state state)
{
        switch (state)
        {
           case IBV_QPS_RESET: return ("RESET");
           case IBV_QPS_INIT: return ("INIT");
           case IBV_QPS_RTR: return ("RTR");
           case IBV_QPS_RTS: return ("RTS");
           case IBV_QPS_SQD: return ("SQD");
           case IBV_QPS_SQE: return ("SQE");
           case IBV_QPS_ERR: return ("ERR");
        }
        return ("Unknown");
}

static const char *
qp_type_str(enum ibv_qp_type type)
{
        switch (type)
        {
           case IBV_QPT_RC: return ("RC");
           case IBV_QPT_UC: return ("UC");
           case IBV_QPT_UD: return ("UD");
        }
        return ("Unknown");
}

static const char *
qp_access_flags_str(enum ibv_access_flags flags)
{
        static char string[128];
        memset(string, 0, 128);
        if (flags & IBV_ACCESS_LOCAL_WRITE) { strcat(string, "Local Write,"); }
        if (flags & IBV_ACCESS_REMOTE_WRITE) { strcat(string, "Remote Write,"); }
        if (flags & IBV_ACCESS_REMOTE_READ) { strcat(string, "Remote Read,"); }
        if (flags & IBV_ACCESS_REMOTE_ATOMIC) { strcat(string, "Remote Atomic,"); }
        if (flags & IBV_ACCESS_MW_BIND) { strcat(string, "MW Bind"); }
        return (string);
}

static const char *
wc_status_str(enum ibv_wc_status status)
{
        switch (status)
        {
		case IBV_WC_SUCCESS:
			return ("Success");
		case IBV_WC_LOC_LEN_ERR:
			return ("Local Length Error");
		case IBV_WC_LOC_QP_OP_ERR:
			return ("Local QP Operation Error");
		case IBV_WC_LOC_EEC_OP_ERR:
			return ("Local EE Context Operation Error");
		case IBV_WC_LOC_PROT_ERR:
			return ("Local Protection Error");
		case IBV_WC_WR_FLUSH_ERR:
			return ("Work Request Flushed Error");
		case IBV_WC_MW_BIND_ERR:
			return ("Memory Window? Bind? Error");
		case IBV_WC_BAD_RESP_ERR:
			return ("Bad Response Error");
		case IBV_WC_LOC_ACCESS_ERR:
			return ("Local Access? Error");
		case IBV_WC_REM_INV_REQ_ERR:
			return ("Remote Invalid Request Error");
		case IBV_WC_REM_ACCESS_ERR:
			return ("Remote Access Error");
		case IBV_WC_REM_OP_ERR:
			return ("Remote Operation Error");
		case IBV_WC_RETRY_EXC_ERR:
			return ("Retry Exceeded? Error");
		case IBV_WC_RNR_RETRY_EXC_ERR:
			return ("RNR Retry Counter Exceeded Error");
		case IBV_WC_LOC_RDD_VIOL_ERR:
			return ("Local RDD Violation Error");
		case IBV_WC_REM_INV_RD_REQ_ERR:
			return("Remote Invalid RD Request Error");
		case IBV_WC_REM_ABORT_ERR:
			return ("Remote Abort? Error");
		case IBV_WC_INV_EECN_ERR:
			return ("Invalid Local EE Context Number Error");
		case IBV_WC_INV_EEC_STATE_ERR:
			return ("Invalid Local EE Context State Error");
		case IBV_WC_FATAL_ERR:
			return ("Fatal Error");
		case IBV_WC_RESP_TIMEOUT_ERR:
			return ("Response? Timeout? Error");
		case IBV_WC_GENERAL_ERR:
			return ("General Error");
        }
	return ("Unknown");
}
#endif

/** =========================================================================
 * Wait for the rdma_cm event specified.
 * If another event comes in return an error.
 */
static int
wait_for_event(struct rdma_event_channel *channel,
		enum rdma_cm_event_type requested_event)
{
        struct rdma_cm_event *event;
        int                   rc = 0;

        if ((rc = rdma_get_cm_event(channel, &event)))
        {
                fprintf(stderr, "ERROR: get event failed : %d\n", rc);
                return (rc);
        }
        rdma_ack_cm_event(event);

        if (event->event == requested_event)
                return (0);

        fprintf(stderr, "ERROR: got \"%s\" event instead of \"%s\"\n",
			event_type_str(event->event),
			event_type_str(requested_event));
        return (-1);
}

/** =========================================================================
 * str must be longer than 32 to hold the full gid.
 * len will be checked to ensure this.
 */
static char *
sprint_gid(union ibv_gid *gid, char *str, size_t len)
{
	int  i = 0;
	char tmp[16];

	assert(str != NULL);
	assert(len > 32);

	str[0] = '\0';
	for (i = 0; i < 16; i++)
	{
		sprintf(tmp, "%02X", gid->raw[i]);
		strcat(str, tmp);
	}

	return (str);
}


/** =========================================================================
 * Print the path information returned from the query.
 */
static void
print_path_rec(struct rdma_cm_id *id)
{
	struct ibv_sa_path_rec *rec = &(id->route.path_rec[0]);
	int                     i = 0;
	char                    str[64];

	if (id->route.num_paths <= 0)
	{
		fprintf(stderr,
			"ERROR: Failed to find any path record information\n");
		return;
	}

	switch (mode)
	{
		case LID_ONLY:
			printf("%d\n", ntohs(rec->dlid));
			return;
		case GID_ONLY:
			printf("%s\n", sprint_gid(&(rec->dgid), str, 64));
			return;
		default:
			break;
	}

	printf("   Path Information:\n");
	for (i = 0; i < id->route.num_paths; i++)
	{
		struct ibv_sa_path_rec *rec = &(id->route.path_rec[i]);

		printf("      Record : %d\n", i);
		printf("         dgid       : %s\n", sprint_gid(&(rec->dgid), str, 64));
		printf("         sgid       : %s\n", sprint_gid(&(rec->sgid), str, 64));
		printf("         dlid       : %d\n", ntohs(rec->dlid));
		printf("         slid       : %d\n", ntohs(rec->slid));
		printf("         raw        : %d\n", rec->raw_traffic);
		printf("         hop limit  : %d\n", rec->hop_limit);
		printf("         reversible : %d\n", rec->reversible);
		printf("         numb_path  : %d\n", rec->numb_path);
		printf("         pkey       : %d\n", ntohs(rec->pkey));
		printf("         mtu        : %d\n", rec->mtu);
	}
}


/** =========================================================================
 */
static int
run_query(const char *host)
{
        int                        rc = 0;
        struct addrinfo           *res;
	struct rdma_cm_id         *id;
	struct rdma_event_channel *channel;
        struct sockaddr_in         addr;

        if (getaddrinfo(host, NULL, NULL, &res))
        {
                fprintf(stderr, "getaddrinfo failed : %s\n", strerror(errno));
                return (errno);
        }

	if ((channel = rdma_create_event_channel()) == NULL) {
		fprintf(stderr, "failed to create event channel: %s\n",
				strerror(errno));
		if (errno == ENOENT)
		{
			fprintf(stderr,
				"   Perhaps \"modprobe rdma_ucm\" is needed?\n");
		}
		return (errno);
	}

        if ((rc = rdma_create_id(channel, &id, NULL, RDMA_PS_TCP)))
        {
                fprintf(stderr, "Failed to create rdma_cm_id\n");
                return (rc);
        }

        addr = *(struct sockaddr_in *)res->ai_addr;
        addr.sin_port = COPY_PORT;
        if ( (rc =  rdma_resolve_addr(id, NULL, (struct sockaddr *)&addr, 2000))
              ||
             (rc = wait_for_event(channel, RDMA_CM_EVENT_ADDR_RESOLVED)) )
        {
                fprintf(stderr, "   failed to resolve addr for : %s\n", host);
                fprintf(stderr, "   perhaps the node is down?\n");
                goto error;
        }

        if ( (rc =  rdma_resolve_route(id, 2000))
                ||
             (rc = wait_for_event(channel, RDMA_CM_EVENT_ROUTE_RESOLVED)) )
        {
                fprintf(stderr, "   failed to resolve route to : %s\n", host);
                fprintf(stderr, "   perhaps the node is down?\n");
                goto error;
        }

	print_path_rec(id);

error:
        rdma_destroy_id(id);
        return (rc);
}

/** =========================================================================
 */
static int
usage(void)
{
        fprintf(stderr,
              "%s [-LG] <hosname|IP>\n"
              "Usage: use rdmacm to query for LID/GID.\n"
              "       Default is to return all information\n"
              "       -L Return one LID only\n"
              "       -G Return one GID only\n"
              , argv0
              );
        exit(0);
        return (0);
}

/** =========================================================================
 */
int
main(int argc, char *argv[])
{
        char *host = NULL;

        char  ch = 0;
        static char const str_opts[] = "LGh";
        static const struct option long_opts [] = {
           {"L", 0, 0, 'L'},
           {"G", 0, 0, 'G'},
           {"help", 0, 0, 'h'},
           { }
        };

	argv0 = argv[0];

        while ((ch = getopt_long(argc, argv, str_opts, long_opts, NULL))
                != -1)
        {
                switch (ch)
                {
                        case 'L': mode = LID_ONLY; break;
                        case 'G': mode = GID_ONLY; break;
                        case 'h':
                        default:
                        	usage();
                }
	}

	argc -= optind;
	argv += optind;

	if (argc) {
		host = strdup(argv[0]);
	} else {
		fprintf(stderr, "ERROR: Hostname or IP address required\n");
		exit(-1);
	}

	return (run_query(host));
}

