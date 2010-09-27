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
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <iba/ib_types.h>
#include <infiniband/ibedgeconf.h>

#ifndef LIBXML_TREE_ENABLED
#error "libxml error: Tree support not compiled in"
#endif

/** =========================================================================
 * Borrow from ibnetdiscover for debugging output
 */
char *dump_linkspeed_compat(uint32_t speed)
{
	switch (speed) {
	case IB_LINK_SPEED_ACTIVE_2_5:
		return ("2.5 Gbps");
		//return ("SDR");
		break;
	case IB_LINK_SPEED_ACTIVE_5:
		return ("5.0 Gbps");
		//return ("DDR");
		break;
	case IB_LINK_SPEED_ACTIVE_10:
		return ("10.0 Gbps");
		//return ("QDR");
		break;
	}
	return ("???");
}

char *dump_linkwidth_compat(uint32_t width)
{
	switch (width) {
	case IB_LINK_WIDTH_ACTIVE_1X:
		return ("1x");
		break;
	case IB_LINK_WIDTH_ACTIVE_4X:
		return ("4x");
		break;
	case IB_LINK_WIDTH_ACTIVE_8X:
		return ("8x");
		break;
	case IB_LINK_WIDTH_ACTIVE_12X:
		return ("12x");
		break;
	}
	return ("??");
}
/** =========================================================================
 * END borrow
 */

struct ibedge_prop {
	uint8_t speed;
	uint8_t width;
};
#define IBCONF_DEFAULT_PROP \
{ \
	speed: IB_LINK_SPEED_ACTIVE_2_5, \
	width: IB_LINK_WIDTH_ACTIVE_1X \
}

#define HTSZ 137
/* hash algo found here: http://www.cse.yorku.ca/~oz/hash.html */
static int
hash_name(char *str)
{
	unsigned long hash = 5381;
	int c;
	
	while ((c = *str++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	
	return (hash % HTSZ);
}

struct ibedge_port {
	struct ibedge_port *next;
	struct ibedge_port *prev;
	char *name;
	int port_num;
	ibedge_prop_t prop;
	struct ibedge_port *remote;
};

struct ibedge_port_list {
	struct ibedge_port *head;
};

struct ibedge_conf {
	char *name;
	struct ibedge_port *ports[HTSZ];
};

static ibedge_port_t *
calloc_port(char *name, int port_num, ibedge_prop_t *prop)
{
	ibedge_port_t *port = calloc(1, sizeof *port);
	if (!port)
		return (NULL);

	port->next = NULL;
	port->prev = NULL;
	port->name = strdup(name);
	port->port_num = port_num;
	port->prop = *prop;
	return (port);
}

static void
free_port(ibedge_port_t *p)
{
	free(p->name);
	free(p);
}

static int
port_equal(ibedge_port_t *port, char *n, int p)
{
	return (strcmp((const char *)port->name, (const char *)n) == 0 && port->port_num == p);
}


static ibedge_port_t *
find_port(ibedge_conf_t *edgeconf, char *n, int p)
{
	ibedge_port_t *cur;
	int h = hash_name(n);
	for (cur = edgeconf->ports[h]; cur; cur = cur->next)
		if (port_equal(cur, n, p))
			return (cur);

	return (NULL);
}

static int
remove_free_port(ibedge_conf_t *edgeconf, ibedge_port_t *port)
{
	if (port->prev)
		port->prev->next = port->next;
	else {
		int h = hash_name(port->name);
		edgeconf->ports[h] = port->next;
	}

	if (port->next)
		port->next->prev = port->prev;

	free(port->name);
	free(port);
	return (0);
}

static int
_add_port(ibedge_conf_t *edgeconf, ibedge_port_t *port)
{
	int h = hash_name(port->name);
	port->next = edgeconf->ports[h];
	port->prev = NULL;
	edgeconf->ports[h] = port;
	return (0);
}

static ibedge_port_t *
add_port(ibedge_conf_t *edgeconf, char *name, int port_num,
	ibedge_prop_t *prop)
{
	ibedge_port_t *port = calloc_port(name, port_num, prop);
	if (!port)
		return (NULL);
	_add_port(edgeconf, port);
	return (port);
}

static int
add_edge(ibedge_conf_t *edgeconf, char *lname, char *lport_str,
	ibedge_prop_t *prop,
	char *rname, char *rport_str)
{
	ibedge_port_t *lport, *rport;
	int lpn = strtol(lport_str, NULL, 0);
	int rpn = strtol(rport_str, NULL, 0);

	lport = find_port(edgeconf, lname, lpn);
	rport = find_port(edgeconf, rname, rpn);

	if (lport) {
		assert(lport->remote->remote == lport);
		if (lport->remote != rport)
			remove_free_port(edgeconf, lport->remote);
		lport->prop = *prop;
	} else {
		lport = add_port(edgeconf, lname, lpn, prop);
		if (!lport) {
			fprintf(stderr, "ERROR: failed to allocated lport\n");
			return (-ENOMEM);
		}
	}

	if (rport) {
		assert(rport->remote->remote == rport);
		if (rport->remote != lport)
			remove_free_port(edgeconf, rport->remote);
		rport->prop = *prop;
	} else {
		rport = add_port(edgeconf, rname, rpn, prop);
		if (!rport) {
			fprintf(stderr, "ERROR: failed to allocated lport\n");
			return (-ENOMEM);
		}
	}

	lport->remote = rport;
	rport->remote = lport;

	return (0);
}

/**
 * Search for and set the properties see in this node
 */
static int
parse_properties(xmlNode *node, ibedge_prop_t *prop)
{
	char *speed = NULL;
	char *width = NULL;

	if ((speed = (char *)xmlGetProp(node, (xmlChar *)"speed"))) {
		if (strcmp(speed, "SDR") == 0)
			prop->speed = IB_LINK_SPEED_ACTIVE_2_5;
		if (strcmp(speed, "DDR") == 0)
			prop->speed = IB_LINK_SPEED_ACTIVE_5;
		if (strcmp(speed, "QDR") == 0)
			prop->speed = IB_LINK_SPEED_ACTIVE_10;
	}

	if ((width = (char *)xmlGetProp(node, (xmlChar *)"width"))) {
		if (strcmp(width, "1x") == 0 ||
		    strcmp(width, "1X") == 0)
			prop->width = IB_LINK_WIDTH_ACTIVE_1X;
		if (strcmp(width, "4x") == 0 ||
		    strcmp(width, "4X") == 0)
			prop->width = IB_LINK_WIDTH_ACTIVE_4X;
		if (strcmp(width, "8x") == 0 ||
		    strcmp(width, "8X") == 0)
			prop->width = IB_LINK_WIDTH_ACTIVE_8X;
		if (strcmp(width, "12x") == 0 ||
		    strcmp(width, "12X") == 0)
			prop->width = IB_LINK_WIDTH_ACTIVE_12X;
	}
	xmlFree(speed);
	xmlFree(width);
	return (0);
}


typedef struct ch_pos_map {
	struct ch_pos_map *next;
	char *pos;
	char *name;
} ch_pos_map_t;

typedef struct ch_map {
	char *name;
	ch_pos_map_t *map;
} ch_map_t;

static char *
map_pos(ch_map_t *ch_map, char *position)
{
	ch_pos_map_t *cur;
	for (cur = ch_map->map; cur; cur = cur->next) {
		if (strcmp(position, cur->pos) == 0)
			return (cur->name);
	}
	return (NULL);
}

static int
remap_edgelist(xmlNode *edgelist, ch_map_t *ch_map)
{
	xmlNode *cur;
	for (cur = edgelist->children; cur; cur = cur->next) {
		if (strcmp((char *)cur->name, "port") == 0) {
			xmlNode *child;
			for (child = cur->children; child; child = child->next) {
				if (strcmp((char *)child->name, "r_node") == 0) {
					char *pos = (char *)xmlNodeGetContent(child);
					char *name = map_pos(ch_map, pos);
					if (name)
						xmlNodeSetContent(child, (xmlChar *)name);
					else {
						char n[256];
						snprintf(n, 256, "%s %s",
							ch_map->name, pos);
						xmlNodeSetContent(child, (xmlChar *)n);
					}
					xmlFree(pos);
				}
			}
		}
	}
	return (0);
}

static int
remap_chassis_doc(xmlNode *chassis, ch_map_t *ch_map)
{
	xmlNode *cur;
	for (cur = chassis->children; cur; cur = cur->next) {
		if (strcmp((char *)cur->name, "edgelist") == 0) {
			char *pos = (char *)xmlGetProp(cur, (xmlChar *)"position");
			char *name = map_pos(ch_map, pos);
			if (name)
				xmlSetProp(cur, (xmlChar *)"name", (xmlChar *)name);
			else {
				char n[256];
				snprintf(n, 256, "%s %s", ch_map->name, pos);
				xmlSetProp(cur, (xmlChar *)"name", (xmlChar *)n);
			}
			xmlFree(pos);

			remap_edgelist(cur, ch_map);
		}
	}
	return (0);
}

static int
parse_port(char *node_name, xmlNode *portNode, ibedge_prop_t *parent_prop,
		ibedge_conf_t *edgeconf)
{
	xmlNode *cur = NULL;
	char *port = (char *)xmlGetProp(portNode, (xmlChar *)"num");
	/* inherit the properties from our parent */
	ibedge_prop_t prop = *parent_prop;
	char *r_port = NULL;
	char *r_node = NULL;

	if (!port)
		return (-EIO);

	parse_properties(portNode, &prop);

	for (cur = portNode->children;
	     cur;
	     cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			if (strcmp((char *)cur->name, "r_port") == 0) {
				r_port = (char *)xmlNodeGetContent(cur);
			}
			if (strcmp((char *)cur->name, "r_node") == 0) {
				r_node = (char *)xmlNodeGetContent(cur);
			}
		}
	}

	add_edge(edgeconf, (char *)node_name, (char *)port, &prop,
		(char *)r_node, (char *)r_port);

	xmlFree(port);
	xmlFree(r_port);
	xmlFree(r_node);

	return (0);
}

static int
parse_edgelist(xmlNode *edgelist, ibedge_prop_t *parent_prop,
		ibedge_conf_t *edgeconf)
{
	xmlNode *cur = NULL;
	char *edgelist_name = (char *)xmlGetProp(edgelist, (xmlChar *)"name");
	ibedge_prop_t prop = *parent_prop; /* inherit the properties from our parent */

	if (!edgelist_name)
		return (-EIO);

	parse_properties(edgelist, &prop);

	for (cur = edgelist->children;
	     cur;
	     cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			if (strcmp((char *)cur->name, "port") == 0) {
				parse_port(edgelist_name, cur, &prop, edgeconf);
			}
		}
	}
	xmlFree(edgelist_name);
	return (0);
}

static int parse_chassismap(xmlNode *chassis, ibedge_prop_t *parent_prop,
				ibedge_conf_t *edgeconf)
{
	int rc = 0;
	xmlNode *cur;
	for (cur = chassis->children; cur; cur = cur->next) {
		if (strcmp((char *)cur->name, "edgelist") == 0) {
			rc = parse_edgelist(cur, parent_prop, edgeconf);
		}
		if (rc)
			break;
	}
	return (rc);
}

static int
process_chassis_model(ch_map_t *ch_map, char *model,
			ibedge_prop_t *parent_prop, ibedge_conf_t *edgeconf)
{
	xmlDoc *chassis_doc = NULL;
	xmlNode *root_element = NULL;
	xmlNode *cur = NULL;
	ibedge_prop_t prop = *parent_prop;
	int rc = 0;
	char *file = malloc(strlen(IBEDGE_CONFIG_DIR) + strlen(model)
				+strlen("/.xml   "));

	snprintf(file, 512, IBEDGE_CONFIG_DIR"/%s.xml", model);
	
	/*parse the file and get the DOM */
	chassis_doc = xmlReadFile(file, NULL, 0);

	if (chassis_doc == NULL) {
		fprintf(stderr, "ERROR: could not parse chassis file %s\n", file);
		rc = -EIO;
		goto exit;
	}

	/*Get the root element node */
	root_element = xmlDocGetRootElement(chassis_doc);

	/* replace all the content of this tree wit our ch_pos_map */
	for (cur = root_element; cur; cur = cur->next)
		if (cur->type == XML_ELEMENT_NODE)
			if (strcmp((char *)cur->name, "chassismap") == 0) {
				char *model_name = (char *)xmlGetProp(cur, (xmlChar *)"model");
				if (!model_name || strcmp(model_name, model) != 0) {
					fprintf(stderr, "ERROR processing %s; Model name does not "
						"match: %s != %s\n",
						file, model_name, model);
					rc = -EIO;
					goto exit;
				}

				remap_chassis_doc(cur, ch_map);
				parse_chassismap(cur, &prop, edgeconf);
			}

#if 0
FILE *f = fopen("debug.xml", "w+");
xmlDocDump(f, chassis_doc);
fclose(f);
#endif

	xmlFreeDoc(chassis_doc);
exit:
	free(file);
	return (rc);
}

/**
 * Parse chassis
 */
static int
parse_chassis(xmlNode *chassis, ibedge_prop_t *parent_prop,
		ibedge_conf_t *edgeconf)
{
	int rc = 0;
	xmlNode *cur = NULL;
	ibedge_prop_t prop = *parent_prop; /* inherit the properties from our parent */
	xmlChar *chassis_name = xmlGetProp(chassis, (xmlChar *)"name");
	xmlChar *chassis_model = xmlGetProp(chassis, (xmlChar *)"model");
	ch_map_t *ch_map = calloc(1, sizeof *ch_map);

	if (!ch_map) {
		rc = -ENOMEM;
		goto free_xmlChar;
	}

	if (!chassis_name || !chassis_model) {
		fprintf(stderr, "chassis_[name|model] not defined\n");
		rc = -EIO;
		goto free_xmlChar;
	}

	ch_map->name = (char *)chassis_name;

	parse_properties(chassis, &prop);

	/* first get a position/name map */
	for (cur = chassis->children;
	     cur;
	     cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			if (strcmp((char *)cur->name, "node") == 0) {
				ch_pos_map_t *n = malloc(sizeof *n);
				n->pos = (char *)xmlGetProp(cur, (xmlChar *)"position");
				n->name = (char *)xmlNodeGetContent(cur);
				n->next = ch_map->map;
				ch_map->map = n;
			}
		}
	}

	/* then use that map to create real edges with those names */
	/* read the model config */
	rc = process_chassis_model(ch_map, (char *)chassis_model, &prop, edgeconf);

	/* free our position/name map */
	while (ch_map->map) {
		ch_pos_map_t *tmp = ch_map->map;
		ch_map->map = ch_map->map->next;
		xmlFree(tmp->pos);
		xmlFree(tmp->name);
		free(tmp);
	}

free_xmlChar:
	/* frees the memory pointed to in ch_map_t as well */
	xmlFree(chassis_name);
	xmlFree(chassis_model);

	return (rc);
}


static int
parse_fabric(xmlNode *fabric, ibedge_prop_t *parent_prop, ibedge_conf_t *edgeconf)
{
	int rc = 0;
	xmlNode *cur = NULL;
	xmlAttr *attr = NULL;
	ibedge_prop_t prop = *parent_prop;
	xmlChar *fabric_name = xmlGetProp(fabric, (xmlChar *)"name");

	if (fabric_name) {
		edgeconf->name = strdup((char *)fabric_name);
		xmlFree(fabric_name);
	} else
		edgeconf->name = strdup("fabric");

	parse_properties(fabric, &prop);

	for (cur = fabric->children; cur; cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			if (strcmp((char *)cur->name, "chassis") == 0)
				rc = parse_chassis(cur, &prop, edgeconf);
			else if (strcmp((char *)cur->name, "edgelist") == 0)
				rc = parse_edgelist(cur, &prop, edgeconf);
			else if (strcmp((char *)cur->name, "subfabric") == 0)
				rc = parse_fabric(cur, &prop, edgeconf);
			else {
				xmlChar * cont = xmlNodeGetContent(cur);
				fprintf(stderr, "UNKNOWN XML node found\n");
				fprintf(stderr, "%s = %s\n", cur->name, (char *)cont);
				xmlFree(cont);
				/* xmlGetProp(node, "key") could work as well */
				for (attr = cur->properties; attr; attr = attr->next) {
					fprintf(stderr, "   %s=%s\n",
					(char *)attr->name, (char *)attr->children->content);
				}
			}
		}
		if (rc)
			break;
	}
	return (rc);
}

/**
 */
static int
parse_file(xmlNode * a_node, ibedge_conf_t *edgeconf)
{
	xmlNode *cur = NULL;
	ibedge_prop_t prop = IBCONF_DEFAULT_PROP;
	
	for (cur = a_node; cur; cur = cur->next)
		if (cur->type == XML_ELEMENT_NODE)
			if (strcmp((char *)cur->name, "fabric") == 0)
				return (parse_fabric(cur, &prop, edgeconf));

	return (-EIO);
}


/* debug */
#if 0
static void
debug_dump_edge_conf(ibedge_conf_t *edgeconf)
{
	int i = 0;
	char prop[256];
	char rprop[256];
	printf("Name: %s\n", edgeconf->name);
	for (i = 0; i < HTSZ; i++) {
		ibedge_port_t *port = NULL;
		printf("   ports (%d)\n", i);
		for (port = edgeconf->ports[i]; port; port = port->next) {
			printf ("\"%s\":%d --(%s:%s)--> \"%s\":%d\n",
				port->name, port->port_num,
				ibedge_prop_str(port, prop, 256),
				ibedge_prop_str(port->remote, rprop, 256),
				port->remote->name, port->remote->port_num);
		}
	}
}
#endif


/** =========================================================================
 * External interface functions
 */

/* accessor function */
char *ibedge_conf_get_name(ibedge_conf_t *conf) { return (conf->name); }
int ibedge_prop_get_speed(ibedge_prop_t *prop) { return (prop->speed); }
int ibedge_prop_get_width(ibedge_prop_t *prop) { return (prop->width); }
char *ibedge_port_get_name(ibedge_port_t *port) { return (port->name); }
int   ibedge_port_get_port_num(ibedge_port_t *port) { return (port->port_num); }
ibedge_prop_t *ibedge_port_get_prop(ibedge_port_t *port)
	{ return (&port->prop); }
ibedge_port_t *ibedge_port_get_remote(ibedge_port_t *port)
	{ return (port->remote); }

char *
ibedge_prop_str(ibedge_port_t *port, char ret[], unsigned n)
{
	if (!n)
		return (NULL);

	ret[0] = '\0';
	snprintf(ret, n, "%s %s",
		dump_linkwidth_compat(port->prop.width),
		dump_linkspeed_compat(port->prop.speed));
	return (ret);
}


/* interface functions */
ibedge_conf_t *
ibedge_alloc_conf(void)
{
	return (malloc(sizeof (ibedge_conf_t)));
}

void
ibedge_free(ibedge_conf_t *edgeconf)
{
	int i = 0;
	for (i = 0; i < HTSZ; i++) {
		ibedge_port_t *port = edgeconf->ports[i];
		while (port) {
			ibedge_port_t *tmp = port;
			port = port->next;
			free_port(tmp);
		}
	}
	free(edgeconf);
}

int
ibedge_parse_file(char *file, ibedge_conf_t *edgeconf)
{
	xmlDoc *doc = NULL;
	xmlNode *root_element = NULL;
	int rc = 0;

	if (!edgeconf)
		return (-EINVAL);
	
	/* initialize the library */
	LIBXML_TEST_VERSION

	if (!file) {
		file = IBEDGE_DEF_CONFIG;
	}
	
	/* parse the file and get the DOM */
	doc = xmlReadFile(file, NULL, 0);
	if (doc == NULL) {
		fprintf(stderr, "error: could not parse file %s\n", file);
		return (-EIO);
	}
	
	/*Get the root element node */
	root_element = xmlDocGetRootElement(doc);

	/* process the file */
	rc = parse_file(root_element, edgeconf);
	
	/*free the document */
	xmlFreeDoc(doc);
	
	/*
	 *Free the global variables that may
	 *have been allocated by the parser.
	 */
	xmlCleanupParser();

	return (rc);
}

ibedge_port_t *
ibedge_get_port(ibedge_conf_t *edgeconf, char *name, int p_num)
{
	return (find_port(edgeconf, name, p_num));
}

static void
_ibedge_free_port_list(ibedge_port_t *head)
{
	while (head) {
		ibedge_port_t *tmp = head;
		head = head->next;
		free_port(tmp);
	}
}

int
ibedge_get_port_list(ibedge_conf_t *edgeconf, char *name,
		ibedge_port_list_t **list)
{
	ibedge_port_list_t *port_list = NULL;
	ibedge_port_t *cur = NULL;
	*list = NULL;
	int h = hash_name(name);
	for (cur = edgeconf->ports[h]; cur; cur = cur->next)
		if (strcmp((const char *)cur->name, (const char *)name) == 0) {
			ibedge_port_t *tmp = NULL;
			if (!port_list) {
				port_list = calloc(1, sizeof *port_list);
				if (!port_list)
					return (-ENOMEM);
			}
			tmp = calloc_port(cur->name, cur->port_num, &cur->prop);
			if (!tmp) {
				_ibedge_free_port_list(port_list->head);
				free(port_list);
				return (-ENOMEM);
			}
			tmp->remote = cur->remote;
			tmp->next = port_list->head;
			port_list->head = tmp;
		}

	*list = port_list;
	return (0);
}

void
ibedge_free_port_list(ibedge_port_list_t *port_list)
{
	_ibedge_free_port_list(port_list->head);
}

void
ibedge_iter_ports(ibedge_conf_t *edgeconf, process_port_func func,
		void *user_data)
{
	int i = 0;
	for (i = 0; i < HTSZ; i++) {
		ibedge_port_t *port = NULL;
		for (port = edgeconf->ports[i]; port; port = port->next)
			func(port, user_data);
	}
}

void
ibedge_iter_port_list(ibedge_port_list_t *port_list,
			process_port_func func, void *user_data)
{
	ibedge_port_t *port = NULL;
	for (port = port_list->head; port; port = port->next)
		func(port, user_data);
}

