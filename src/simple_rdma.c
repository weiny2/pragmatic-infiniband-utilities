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
#include <arpa/inet.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>

#define COPY_PORT (10000)

/*************************************************************************
 * Data structures
 */
typedef struct 
{
	uint64_t addr;
	uint32_t rkey;
	uint32_t size;
} rdma_eth_t;

typedef struct
{
	struct rdma_cm_id         *id;
	struct ibv_pd             *pd;
	struct ibv_cq             *cq;
	struct rdma_event_channel *channel;
	int                        quit_cq_thread;

	/**
	 * These are the memory regions for the server side.
	 *
	 * The remote info is the rdma_eth from the client which gives the information
	 * about where the server should write the data.
	 *
	 * The dma_region is the data which the server is going to write.  You have to
	 * "register" this memory with the card so that it can do a local read from it
	 * when it wants to send it.
	 */
	struct
	{
	        rdma_eth_t     remote_info;
	        struct ibv_mr *remote_info_mr;
	        uint8_t        dma_region[512];
	        struct ibv_mr *dma_region_mr;
	} server_data;
} simple_context_t;

/**
 * Global data.
 */
char *argv0 = NULL;
int   server_mode = 0;
int   cycles = 1;
int   query_qp_on_alloc = 0;
struct
{
        rdma_eth_t       rdma_eth;
        struct ibv_mr   *rdma_eth_mr;
        uint8_t          dma_region[512];
        struct ibv_mr   *dma_region_mr;
} client_data;


/**************************************************************************
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
#endif

#if 0
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
#endif

#if 0
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
#endif

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

/*************************************************************************
 * Print the qp state...
 */
static void
debug_print_qp(simple_context_t *context)
{
        struct ibv_qp_attr      attr;
        struct ibv_qp_init_attr init_attr;

        if (ibv_query_qp(context->id->qp, &attr,
                 0xFFFFF,
                 &init_attr))
        {
                fprintf(stderr, "failed to query qp\n");
                return;
        }

        fprintf(stderr,
		" QP query:\n"
		"   qp num              : 0x%x\n"
		"   events completed    : %d\n"
		//"   State               : %s\n"
		"   rq psn              : %d\n"
		"   sq psn              : %d\n"
		"   dest qp num         : %X\n"
		//"   qp access flags     : %s (%d)\n"
		"   cap.max send wr     : %d\n"
		"   cap.max recv wr     : %d\n"
		//"   cap.max send sge    : %d\n"
		//"   cap.max recv sge    : %d\n"
		//"   cap.max inline date : %d\n"
		//"   sq draining         : %d\n"
		//"   port num            : %d\n"
		//"   timeout             : %d\n"
		"   min_rnr_timer       : %d\n"
		"   rnr retry           : %d\n"
		//"   qp type             : %s\n"
        ,
                context->id->qp->qp_num,
                context->id->qp->events_completed,
                //qp_state_str(attr.qp_state),
                attr.rq_psn,
                attr.sq_psn,
                attr.dest_qp_num,
                //qp_access_flags_str(attr.qp_access_flags), attr.qp_access_flags,
                attr.cap.max_send_wr,
                attr.cap.max_recv_wr,
                //attr.cap.max_send_sge,
                //attr.cap.max_recv_sge,
                //attr.cap.max_inline_data,
                //attr.sq_draining,
                //attr.port_num,
                //attr.timeout,
		attr.min_rnr_timer,
                attr.rnr_retry
                //qp_type_str(init_attr.qp_type)
              );
}

/*************************************************************************
 * Wait for the rdma_cm event specified.
 * If another event comes in return an error.
 */
static int
wait_for_event(struct rdma_event_channel *channel,
		enum rdma_cm_event_type requested_event)
{
        struct rdma_cm_event *event;
        int                   rc = 0;
	int                   rv = -1;

        if ((rc = rdma_get_cm_event(channel, &event)))
        {
                fprintf(stderr, "get event failed : %d\n", rc);
                return (rc);
        }
        fprintf(stderr, "got \"%s\" event\n", event_type_str(event->event));

        if (event->event == requested_event)
		rv = 0;

        rdma_ack_cm_event(event);
        return (rv);
}

/*************************************************************************
 * free the PD, CQ, MR, and QP for this connection
 * the server will have multiple's open the client just one.
 */
static void
free_server_resources(simple_context_t *context)
{
        if (context->server_data.remote_info_mr)  { ibv_dereg_mr(context->server_data.remote_info_mr); }
        if (context->server_data.dma_region_mr)   { ibv_dereg_mr(context->server_data.dma_region_mr); }
        if (context->pd)                 { ibv_dealloc_pd(context->pd); }
        if (context->id->qp)             { rdma_destroy_qp(context->id); }
        if (context->cq)                 { ibv_destroy_cq(context->cq); }
        //if (context->channel)            { rdma_destroy_event_channel(context->channel); }
}

/*************************************************************************
 * free the PD, CQ, MR, and QP for this connection
 * the server will have multiple's open the client just one.
 */
static void
free_client_resources(simple_context_t *context)
{
        if (context->id->qp)             { rdma_destroy_qp(context->id); }
        if (client_data.rdma_eth_mr)     { ibv_dereg_mr(client_data.rdma_eth_mr); }
        if (client_data.dma_region_mr)   { ibv_dereg_mr(client_data.dma_region_mr); }
        if (context->cq)                 { ibv_destroy_cq(context->cq); }
        if (context->pd)                 { ibv_dealloc_pd(context->pd); }
        if (context->channel)            { rdma_destroy_event_channel(context->channel); }
}


/*************************************************************************
 * Send the rdma_eth so the server knows where to RDMA read from.
 */
static int
send_client_rdma_eth(simple_context_t *context)
{
        struct ibv_send_wr *bad_wr;
        struct ibv_send_wr  snd_wr;
        struct ibv_sge      sg_entry;
	int rc = 0;

        memset(&client_data.rdma_eth, 0, sizeof(client_data.rdma_eth));
        client_data.rdma_eth.addr = (uint64_t)client_data.dma_region;
        client_data.rdma_eth.rkey = client_data.dma_region_mr->rkey;
        client_data.rdma_eth.size = sizeof(client_data.dma_region);

	memset(&sg_entry, 0, sizeof(sg_entry));
        sg_entry.addr = (uint64_t)&client_data.rdma_eth;
        sg_entry.length = sizeof(client_data.rdma_eth);
        sg_entry.lkey = client_data.rdma_eth_mr->lkey;

	memset(&snd_wr, 0, sizeof(snd_wr));
        snd_wr.next = NULL;
        snd_wr.sg_list = &sg_entry;
        snd_wr.num_sge = 1;
	snd_wr.opcode = IBV_WR_SEND;
	snd_wr.send_flags = IBV_SEND_SIGNALED;

        printf("Posting client read buffer information: addr %LX, rkey %X, size %d\n",
              (long long unsigned int)client_data.rdma_eth.addr,
              client_data.rdma_eth.rkey,
              client_data.rdma_eth.size);

	if (cycles > 0) { cycles--; }

	rc = ibv_post_send(context->id->qp, &snd_wr, &bad_wr);
	//printf("rc %d\n", rc);
        return (rc);
}

/*************************************************************************
 * allocate PD, CQ, MR, and QP for the server
 */
static int
allocate_server_resources(simple_context_t *context)
{
        int                     rc = 0;
	struct ibv_qp_init_attr init_qp_attr;

        char *message = "Hello from over here\n";

        context->pd = ibv_alloc_pd(context->id->verbs);
	if (!context->pd)
	{
		fprintf(stderr, "alloc PD failed\n");
		goto error;
	}

        context->cq = ibv_create_cq(context->id->verbs, 10, context, 0, 0);
        if (!context->cq)
	{
		fprintf(stderr, "alloc CQ failed\n");
		goto error;
	}

        memset(&init_qp_attr, 0, sizeof(init_qp_attr));
	init_qp_attr.cap.max_send_wr = 1;
	init_qp_attr.cap.max_recv_wr = 1;
	init_qp_attr.cap.max_send_sge = 1;
	init_qp_attr.cap.max_recv_sge = 1;
	init_qp_attr.sq_sig_all = 1;
	init_qp_attr.qp_type = IBV_QPT_RC;
	init_qp_attr.send_cq = context->cq;
	init_qp_attr.recv_cq = context->cq;
	rc = rdma_create_qp(context->id, context->pd, &init_qp_attr);
	if (rc) {
		fprintf(stderr, "unable to create QP: %d\n", rc);
                goto error;
	}

	/**
	 * Register the memory regions
	 */
        memcpy(context->server_data.dma_region, message, strlen(message));

        context->server_data.dma_region_mr = ibv_reg_mr(context->pd,
                                               context->server_data.dma_region,
                                               sizeof(context->server_data.dma_region),
				               IBV_ACCESS_LOCAL_WRITE |
				               IBV_ACCESS_REMOTE_READ |
				               IBV_ACCESS_REMOTE_WRITE);
        if (context->server_data.dma_region_mr == NULL)
        {
		fprintf(stderr, "unable to register dma_region : %s\n", strerror(errno));
                goto error;
        }

        context->server_data.remote_info_mr = ibv_reg_mr(context->pd,
                                               &context->server_data.remote_info,
                                               sizeof(context->server_data.remote_info),
				               IBV_ACCESS_LOCAL_WRITE);
        if (context->server_data.remote_info_mr == NULL)
        {
		fprintf(stderr, "unable to register remote_info : %s\n", strerror(errno));
                goto error;
        }
        return (0);
error:
        free_server_resources(context);
        return (-1);
}

/*************************************************************************
 * allocate PD, CQ, MR's, and QP for the client
 */
static int
allocate_client_resources(simple_context_t *context)
{
        int                     rc = 0;
	struct ibv_qp_init_attr init_qp_attr;

        context->pd = ibv_alloc_pd(context->id->verbs);
        context->cq = ibv_create_cq(context->id->verbs, 10, context, 0, 0);

        if (!context->pd || !context->cq) { goto error; }

        memset(&init_qp_attr, 0, sizeof(init_qp_attr));
	init_qp_attr.cap.max_send_wr = 1;
	init_qp_attr.cap.max_recv_wr = 1;
	init_qp_attr.cap.max_send_sge = 1;
	init_qp_attr.cap.max_recv_sge = 1;
	init_qp_attr.sq_sig_all = 1;
	init_qp_attr.qp_type = IBV_QPT_RC;
	init_qp_attr.send_cq = context->cq;
	init_qp_attr.recv_cq = context->cq;
	rc = rdma_create_qp(context->id, context->pd, &init_qp_attr);
	if (rc) {
		fprintf(stderr, "unable to create QP: %d\n", rc);
                goto error;
	}

	/**
	 * Allocate the memory regions
	 */
        client_data.dma_region_mr = ibv_reg_mr(context->pd,
                                               client_data.dma_region,
                                               sizeof(client_data.dma_region),
				               IBV_ACCESS_LOCAL_WRITE |
				               IBV_ACCESS_REMOTE_READ |
				               IBV_ACCESS_REMOTE_WRITE);
        if (client_data.dma_region_mr == NULL)
        {
		fprintf(stderr, "unable to register dma_region : %s\n", strerror(errno));
                goto error;
        }
        client_data.rdma_eth_mr = ibv_reg_mr(context->pd,
                                               &client_data.rdma_eth,
                                               sizeof(client_data.rdma_eth),
				               IBV_ACCESS_LOCAL_WRITE |
				               IBV_ACCESS_REMOTE_READ |
				               IBV_ACCESS_REMOTE_WRITE);
        if (client_data.rdma_eth_mr == NULL)
        {
		fprintf(stderr, "unable to register rdma_eth : %s\n", strerror(errno));
                goto error;
        }
        return (0);
error:
        free_client_resources(context);
        return (-1);
}

/*************************************************************************
 * server call
 * post the work request to recieve the RDMA eth
 */
static int
post_server_rec_work_req(simple_context_t *context)
{
	struct ibv_recv_wr *bad_wr;
        struct ibv_recv_wr  rec_wr;
        struct ibv_sge      sg_entry;

	memset(&sg_entry, 0, sizeof(sg_entry));
        sg_entry.addr = (uint64_t)&context->server_data.remote_info;
        sg_entry.length = sizeof(context->server_data.remote_info);
        sg_entry.lkey = context->server_data.remote_info_mr->lkey;

	memset(&rec_wr, 0, sizeof(rec_wr));
        rec_wr.next = NULL;
        rec_wr.sg_list = &sg_entry;
        rec_wr.num_sge = 1;

        return (ibv_post_recv(context->id->qp, &rec_wr, &bad_wr));
}

/*************************************************************************
 * client call
 * post the work request to recieve the notification of the write being
 * complete
 */
static int
post_client_rec_work_req(simple_context_t *context)
{
	struct ibv_recv_wr *bad_wr;
        struct ibv_recv_wr  rec_wr;
        struct ibv_sge      sg_entry;

	memset(&sg_entry, 0, sizeof(sg_entry));
        sg_entry.addr = (uint64_t)&client_data.rdma_eth;
        sg_entry.length = sizeof(client_data.rdma_eth);
        sg_entry.lkey = client_data.rdma_eth_mr->lkey;

	memset(&rec_wr, 0, sizeof(rec_wr));
        rec_wr.next = NULL;
        rec_wr.sg_list = &sg_entry;
        rec_wr.num_sge = 1;

        return (ibv_post_recv(context->id->qp, &rec_wr, &bad_wr));
}

/*************************************************************************
 * client call
 * print the data recieved from the server...
 */
static void
print_recieved_data(simple_context_t *context)
{
	printf("   *** Data Recieved: %s\n", (char *)client_data.dma_region);
}


#if 0
/*************************************************************************
 * Report the RDMA eth recieved
 */
static void
print_write_address(simple_context_t *context, struct ibv_wc *wc)
{
        printf("recieved %d bytes...  remote addr %LX, rkey %X, size %d\n",
              wc->byte_len,
              (unsigned long long)context->server_data.remote_info.addr,
              context->server_data.remote_info.rkey,
              context->server_data.remote_info.size);
}
#endif


/*************************************************************************
 * Tell the client the data has been written
 */
static int
post_write_complete_msg(simple_context_t *context)
{
	struct ibv_send_wr *bad_wr;
	struct ibv_sge	    sg_list;
	struct ibv_send_wr  wr;

	memset(&sg_list, 0, sizeof(sg_list));
	sg_list.addr = (uint64_t)&(context->server_data.remote_info);
	sg_list.length = sizeof(context->server_data.remote_info);
	sg_list.lkey = context->server_data.remote_info_mr->lkey;

	memset(&wr, 0, sizeof(wr));
	wr.next = NULL;
	wr.sg_list = &sg_list;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_SEND;
	wr.send_flags = IBV_SEND_SIGNALED;

	return (ibv_post_send(context->id->qp, &wr, &bad_wr));
}

/*************************************************************************
 * Write the data to the client
 */
static int
post_rdma_write(simple_context_t *context)
{
	struct ibv_send_wr *bad_wr;
	struct ibv_sge	    sg_list;
	struct ibv_send_wr  wr;

	memset(&sg_list, 0, sizeof(sg_list));
	sg_list.addr = (uint64_t)&(context->server_data.dma_region);
	sg_list.length = (context->server_data.remote_info.size < sizeof(context->server_data.dma_region)) ?
		 	  context->server_data.remote_info.size : sizeof(context->server_data.dma_region);
	sg_list.lkey = context->server_data.dma_region_mr->lkey;

	memset(&wr, 0, sizeof(wr));
	wr.next = NULL;
	wr.sg_list = &sg_list;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_RDMA_WRITE;
	wr.wr.rdma.remote_addr = context->server_data.remote_info.addr;
	wr.wr.rdma.rkey = context->server_data.remote_info.rkey;

	return (ibv_post_send(context->id->qp, &wr, &bad_wr));
}

/*************************************************************************
 * handle the incomming wr on the server side.
 */
static void *
handle_server_cq(void *arg)
{
	simple_context_t *context = (simple_context_t *)arg;
        struct ibv_wc  wcs[2];

        while (!context->quit_cq_thread)
        {
                int            rc = 0;

                while ((rc = ibv_poll_cq(context->cq, 2, wcs)) > 0)
                {
			int i = 0;
                        //printf("Poll cq return %d\n", rc);
			for (i = 0; i < rc; i++)
			{
        			struct ibv_wc *wc = &(wcs[i]);
                        	if (wc->status)
                        	{
                                	fprintf(stderr, "WC completion error \"%s\" (%d)\n",
						wc_status_str(wc->status),
                                        	wc->status);
                                	exit (-1);
                        	}
                        	switch (wc->opcode)
                        	{
                                	case IBV_WC_SEND:
						//printf("send completion\n");
						break;
                                	case IBV_WC_RDMA_WRITE:
					{
						//printf("RDMA WRITE comp\n");
						assert(post_write_complete_msg(context) == 0);
						break;
					}
                                	case IBV_WC_RDMA_READ: printf("RDMA READ comp\n"); break;
                                	case IBV_WC_RECV:
                                	{
                                        	//printf("Recieve data.");
                                        	post_server_rec_work_req(context);
                                        	//print_write_address(context, wc);
						assert(post_rdma_write(context) == 0);
                                        	break;
                                	}
                                	default: fprintf(stderr, "UNKNOWN completion event!\n"); break;
                        	}
			}
                }
        }
	printf("server cq done processing\n");
	return (NULL);
}

/*************************************************************************
 * Accept the connection from the client.  This includes allocating a qp and
 * other resources to talk to.
 */
static int connections = 0;
static void
accept_connection(struct rdma_cm_id *id)
{
        struct rdma_conn_param   conn_param;
        simple_context_t        *context = malloc(sizeof(*context));
        if (!context)
        {
                perror("failed to malloc context for connection\n");
		rdma_reject(id, NULL, 0);
                return;
        }

        /* associate this context with this id. */
        context->id = id;
        id->context = context;

	context->quit_cq_thread = 0;

        if (allocate_server_resources(context))
        {
                fprintf(stderr, "failed to allocate resources\n");
		rdma_reject(id, NULL, 0);
                return;
        }

        post_server_rec_work_req(context);

        printf("Accepting connection on id == %p (total connections %d)\n",
			id, ++connections);

        memset(&conn_param, 0, sizeof(conn_param));
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
        rdma_accept(context->id, &conn_param);

	if (query_qp_on_alloc)
	{
		debug_print_qp(context);
	}
}

/*************************************************************************
 * Free the connection when the client disconnects.
 */
static void
free_connection(simple_context_t *context)
{
        free_server_resources(context);
        free(context);
	connections--;
}

/*************************************************************************
 * The server side main loop.
 */
static int
server(void)
{
        simple_context_t      server_context;
        struct sockaddr_in    addr;
        int                   rc = 0;
        struct rdma_cm_event *event;

	if ((server_context.channel = rdma_create_event_channel()) == NULL) {
		fprintf(stderr, "failed to create event channel\n");
		fprintf(stderr, "   (Ensure the \"rdma_ucm\" module is loaded)\n");
		return (errno);
	}

        /* socket */
        if ((rc = rdma_create_id(server_context.channel, &(server_context.id),
					&server_context, RDMA_PS_TCP)))
        {
                fprintf(stderr,
			"Failed to create rdma_cm_id, perhaps the \"rdma_ucm\" module is not loaded?\n");
                return (rc);
        }

        printf("main server id : %p\n", server_context.id);

        /* bind */
        addr.sin_family = PF_INET;
        addr.sin_port = COPY_PORT;
        addr.sin_addr.s_addr = INADDR_ANY;
        if ((rc = rdma_bind_addr(server_context.id, (struct sockaddr *)&addr)))
        {
                fprintf(stderr, "bind failed : %d\n", rc);
                return (rc);
        }

        /* listen */
        if ((rc = rdma_listen(server_context.id, 0)))
        {
                fprintf(stderr, "listen failed : %d\n", rc);
                return (rc);
        }

        printf("main server id : %p\n", server_context.id);

        while (1)
        {
		pthread_t          thread_id;
		pthread_attr_t     thread_attr;
        	simple_context_t  *context = NULL;
		struct rdma_cm_id *id = NULL;
                fprintf(stderr, "Waiting for cm_event... ");
                if ((rc = rdma_get_cm_event(server_context.channel, &event)))
                {
                        fprintf(stderr, "get event failed : %d\n", rc);
                        break;
                }
                fprintf(stderr, "\"%s\"\n", event_type_str(event->event));
                switch (event->event)
                {
                   case RDMA_CM_EVENT_CONNECT_REQUEST:
                        accept_connection(event->id);
                        break;
                   case RDMA_CM_EVENT_ESTABLISHED:
			pthread_attr_init(&thread_attr);
			pthread_create(&thread_id,
					&thread_attr,
					handle_server_cq,
					(void *)(event->id->context));
                        break;
                   case RDMA_CM_EVENT_DISCONNECTED:
                        fprintf(stderr, "Disconnect from id : %p (total connections %d)\n",
					event->id, connections);
        		context = (simple_context_t *)(event->id->context);
        		id = event->id;
                        break;
                   default:
                        break;
                }
                rdma_ack_cm_event(event);
		if (context)
		{
			context->quit_cq_thread = 1;
			pthread_join(thread_id, NULL);
        		rdma_destroy_id(id);
                        free_connection(context);
			context = NULL;
		}
        }

        rdma_destroy_id(server_context.id);
        return (rc);
}

/*************************************************************************
 * Handle the completion queue events on the client side.
 */
static void *
handle_client_cq(simple_context_t *context)
{
	//struct ibv_wc wcs[2];
	struct ibv_wc wcs;
	int           done = 0;

	post_client_rec_work_req(context);

        while (!done)
        {
                int           rc = 0;

                //fprintf(stderr, "got cq events\n");
                //while ((rc = ibv_poll_cq(context->cq, 2, wcs)) > 0)
                while ((rc = ibv_poll_cq(context->cq, 1, &wcs)) == 1)
                {
			//int i = 0;
                        //printf("Poll cq return %d\n", rc);
			//for (i = 0; i < rc; i++)
			//{
				//struct ibv_wc *wc = &(wc[i]);
				struct ibv_wc *wc = &wcs;
                       		if (wc->status)
                       		{
                       		        fprintf(stderr,
                       		              "WC completion error \"%s\" (%d); vendor error %d\n",
		       				wc_status_str(wc->status),
                       		              	wc->status, wc->vendor_err);
		       			exit(-1);
                       		}
                       		switch (wc->opcode)
                       		{
                       		        case IBV_WC_SEND:
		       				//printf("send completion\n");
		       				break;
                       		        case IBV_WC_RDMA_WRITE: printf("RDMA WRITE comp\n"); break;
                       		        case IBV_WC_RDMA_READ: printf("RDMA READ comp\n"); break;
                       		        case IBV_WC_RECV:
		       				//printf("Recieve data.");
		       				post_client_rec_work_req(context);
		       				print_recieved_data(context);
						if (cycles == -1 || cycles > 0)
						{
        	       					assert(send_client_rdma_eth(context) == 0);
						}
						else
						{
							done = 1;
						}
		       				break;
                       		        default: fprintf(stderr, "UNKNOWN completion event!\n"); break;
                       		}
			//}
                }
        }
	return (NULL);
}

static char *
sprint_gid(union ibv_gid *gid, char *str, size_t len)
{
	char *rc = NULL;
	assert(str != NULL);
	assert(gid != NULL);
	rc = (char *)inet_ntop(AF_INET6, gid->raw, str, len);

	if (!rc) {
		perror("inet_ntop failed\n");
		exit(-1);
	}
	return (rc);
}

static void
print_path_rec(simple_context_t *context)
{
	struct rdma_route *route = &(context->id->route);
	int                i = 0;
	char               str[128];

	printf("   Path Information:\n");
	for (i = 0; i < route->num_paths; i++)
	{
		struct ibv_sa_path_rec *rec = &(route->path_rec[i]);

		printf("      Record           : %d\n", i);
		printf("         dgid          : %s\n", sprint_gid(&(rec->dgid), str, 128));
		printf("         sgid          : %s\n", sprint_gid(&(rec->sgid), str, 128));
		printf("         dlid          : %d\n", ntohs(rec->dlid));
		printf("         slid          : %d\n", ntohs(rec->slid));
		printf("         raw           : %d\n", rec->raw_traffic);
		printf("         flow_label    : %d\n", rec->flow_label);
		printf("         hop_limit     : %d\n", rec->hop_limit);
		printf("         traffic_class : %d\n", rec->traffic_class);
		printf("         reversible    : %d\n", ntohl(rec->reversible));
		printf("         numb_path     : %d\n", rec->numb_path);
		printf("         pkey          : 0x%04X\n", rec->pkey);
		printf("         sl            : %d\n", rec->sl);
		printf("         mtu_selector  : %d\n", rec->mtu_selector);
		printf("         mtu           : %d\n", rec->mtu);
		printf("         rate_selector : %d\n", rec->rate_selector);
		printf("         rate          : %d\n", rec->rate);
		printf("         packet_lts    : %d\n", rec->packet_life_time_selector);
		printf("         packet_lt     : %d\n", rec->packet_life_time);
		printf("         preference    : %d\n", rec->preference);
	}


}

/*************************************************************************
 * The client side main loop
 */
static int
client(const char *host)
{
        int                      rc = 0;
        struct addrinfo         *res;
        simple_context_t         client_context;
        struct sockaddr_in       addr;
        struct rdma_conn_param   conn_param;

	memset(client_data.dma_region, 0, sizeof(client_data.dma_region));

        if (getaddrinfo(host, NULL, NULL, &res))
        {
                fprintf(stderr, "getaddrinfo failed : %s\n", strerror(errno));
                return (errno);
        }

	if ((client_context.channel = rdma_create_event_channel()) == NULL) {
		fprintf(stderr, "failed to create event channel\n");
		fprintf(stderr, "   (Ensure the \"rdma_ucm\" module is loaded)\n");
		return (errno);
	}

        /* socket */
        if ((rc = rdma_create_id(client_context.channel, &(client_context.id),
					&client_context, RDMA_PS_TCP)))
        {
                fprintf(stderr, "Failed to create rdma_cm_id\n");
                return (rc);
        }

        /* ??? */
        addr = *(struct sockaddr_in *)res->ai_addr;
        addr.sin_port = COPY_PORT;
        if ( (rc =  rdma_resolve_addr(client_context.id, NULL, (struct sockaddr *)&addr, 2000))
              ||
             (rc = wait_for_event(client_context.channel, RDMA_CM_EVENT_ADDR_RESOLVED)) )
        {
                fprintf(stderr, "failed to resolve addr for : %s\n", host);
                goto error;
        }

        /* ??? */
        if ( (rc =  rdma_resolve_route(client_context.id, 2000))
                ||
             (rc = wait_for_event(client_context.channel, RDMA_CM_EVENT_ROUTE_RESOLVED)) )
        {
                fprintf(stderr, "failed to resolve route to : %s\n", host);
                goto error;
        }

	print_path_rec(&client_context);

        if (allocate_client_resources(&client_context))
        {
                fprintf(stderr, "failed to allocate resources\n");
                goto error;
        }

        /* connect */
        memset(&conn_param, 0, sizeof(conn_param));
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	conn_param.retry_count = 10;
        if ( (rc =  rdma_connect(client_context.id, &conn_param))
                ||
             (rc = wait_for_event(client_context.channel, RDMA_CM_EVENT_ESTABLISHED)) )
        {
                fprintf(stderr, "failed to connect to : %s\n", host);
                goto error_with_free;
        }

        /* our address for the server to write to */
        if (send_client_rdma_eth(&client_context))
        {
                fprintf(stderr, "Failed to send the client address information\n");
                goto error_with_free;
        }

        handle_client_cq(&client_context);

        rc = 0;
error_with_free:
	//printf("Calling client disconnect\n");
	rdma_disconnect(client_context.id);
        free_client_resources(&client_context);
error:
        rdma_destroy_id(client_context.id);
        return (rc);
}

/*************************************************************************
 */
static int
usage(void)
{
        fprintf(stderr,
              "%s [-q -S -f <cycles> -H <host>]\n"
              "Usage: copy some data from client to server.\n"
              "       -q (server) query QP after allocation\n"
              "       -S server mode\n"
              "       -H <host>\n"
              "       -f <cycles> stay connected to the server \"cycle\" times\n"
              "                   (forever == -1, default == 1)\n"
              , argv0
              );
        exit(0);
        return (0);
}

/*************************************************************************
 */
int
main(int argc, char *argv[])
{
        char ch = 0;
        char *host = NULL;
        static char const str_opts[] = "H:Shqf:";
        static const struct option long_opts [] = {
           {"S", 0, 0, 'S'},
           {"q", 0, 0, 'q'},
           {"host", 1, 0, 'H'},
           {"fail", 1, 0, 'f'},
           {"help", 0, 0, 'h'},
           { }
        };

	argv0 = argv[0];

        while ((ch = getopt_long(argc, argv, str_opts, long_opts, NULL))
                != -1)
        {
                switch (ch)
                {
                        case 'q': query_qp_on_alloc = 1; break;
                        case 'S': server_mode = 1; break;
                        case 'H': host = strdup(optarg); break;
                        case 'f': cycles = atoi(optarg); break;
                        case 'h':
                        default:
                        	usage();
                }
	}

        /* establish the connection and branch to mode */
        if (server_mode)
        {
                return server();
        }
        else
        {
                return client(host);
        }
        return (-1);
}

