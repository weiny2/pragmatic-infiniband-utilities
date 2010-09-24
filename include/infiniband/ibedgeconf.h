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

#ifndef _IBEDGECONF_H_
#define _IBEDGECONF_H_

/* These are opaque data types */
typedef struct ibedge_prop ibedge_prop_t;
typedef struct ibedge_port ibedge_port_t;
typedef struct ibedge_conf ibedge_conf_t;

int ibedge_prop_get_speed(ibedge_prop_t *prop);
int ibedge_prop_get_width(ibedge_prop_t *prop);

char *ibedge_conf_get_name(ibedge_conf_t *conf);

char *ibedge_port_get_name(ibedge_port_t *port);
int   ibedge_port_get_port_num(ibedge_port_t *port);
ibedge_prop_t *ibedge_port_get_prop(ibedge_port_t *port);
ibedge_port_t *ibedge_port_get_remote(ibedge_port_t *port);
char *ibedge_prop_str(ibedge_port_t *port, char ret[], unsigned n);

ibedge_conf_t *ibedge_alloc_conf(void);
void ibedge_free(ibedge_conf_t *edgeconf);

int ibedge_parse_file(char *file, ibedge_conf_t *edgeconf);

ibedge_port_t *ibedge_get_port(ibedge_conf_t *edgeconf, char *name, int p_num);

typedef void (*process_port_func)(ibedge_port_t *port, void *user_data);
void ibedge_iter_ports(ibedge_conf_t *edgeconf, process_port_func func,
                     void *user_data);

#endif /* _IBEDGECONF_H_ */

