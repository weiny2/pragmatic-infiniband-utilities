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
#include <infiniband/ibfabricconf.h>

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
		return ("1X");
		break;
	case IB_LINK_WIDTH_ACTIVE_4X:
		return ("4X");
		break;
	case IB_LINK_WIDTH_ACTIVE_8X:
		return ("8X");
		break;
	case IB_LINK_WIDTH_ACTIVE_12X:
		return ("12X");
		break;
	}
	return ("??");
}
/** =========================================================================
 * END borrow
 */

struct ibfc_prop {
	uint8_t speed;
	uint8_t width;
	char *length;
} IBCONF_DEFAULT_PROP =
{
	speed: IB_LINK_SPEED_ACTIVE_2_5,
	width: IB_LINK_WIDTH_ACTIVE_1X,
	length: ""
};

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

struct ibfc_port {
	struct ibfc_port *next;
	struct ibfc_port *prev;
	char *name;
	int port_num;
	ibfc_prop_t prop;
	void *user_data;
	struct ibfc_port *remote;
};

struct ibfc_port_list {
	struct ibfc_port *head;
};

struct ibfc_conf {
	struct ibfc_port *ports[HTSZ];
	char *name;
	FILE *err_fd;
	int warn_dup;
};

/**
 * duplicate properties.
 */
static int
dup_prop(ibfc_prop_t *dest, ibfc_prop_t *src)
{
	int rc = 0;

	dest->speed = src->speed;
	dest->width = src->width;
	dest->length = strdup(src->length);
	if (!dest->length) {
		rc = -ENOMEM;
		dest->length = IBCONF_DEFAULT_PROP.length;
	}
	return (rc);
}

static ibfc_port_t *
calloc_port(char *name, int port_num, ibfc_prop_t *prop)
{
	ibfc_port_t *port = calloc(1, sizeof *port);
	if (!port)
		return (NULL);

	port->next = NULL;
	port->prev = NULL;
	port->name = strdup(name);
	port->port_num = port_num;
	dup_prop(&port->prop, prop);
	return (port);
}

static void
_prop_free_chars(ibfc_prop_t *p)
{
	if (p->length != IBCONF_DEFAULT_PROP.length)
		free(p->length);
}
static void
free_port(ibfc_port_t *p)
{
	free(p->name);
	_prop_free_chars(&p->prop);
	free(p);
}

static int
port_equal(ibfc_port_t *port, char *n, int p)
{
	return (strcmp((const char *)port->name, (const char *)n) == 0 && port->port_num == p);
}

static int
_port_num_dont_care(int p)
{
	return (p == IBFC_PORT_NUM_DONT_CARE);
}

static int
_port_name_dont_care(char *n)
{
	return (n && strcmp(n, IBFC_PORT_NAME_DONT_CARE) == 0);
}

int
ibfc_port_num_dont_care(ibfc_port_t *port)
{
	return (_port_num_dont_care(port->port_num));
}

int
ibfc_port_name_dont_care(ibfc_port_t *port)
{
	return (_port_name_dont_care(port->name));
}

static ibfc_port_t *
find_port(ibfc_conf_t *fabricconf, char *n, int p)
{
	ibfc_port_t *cur;
	int h = hash_name(n);

	if (_port_name_dont_care(n) || _port_num_dont_care(p))
		return (NULL);

	for (cur = fabricconf->ports[h]; cur; cur = cur->next)
		if (port_equal(cur, n, p))
			return (cur);

	return (NULL);
}

static int
remove_free_port(ibfc_conf_t *fabricconf, ibfc_port_t *port)
{
	if (port->prev)
		port->prev->next = port->next;
	else {
		int h = hash_name(port->name);
		fabricconf->ports[h] = port->next;
	}

	if (port->next)
		port->next->prev = port->prev;

	free_port(port);
	return (0);
}

static int
add_port(ibfc_conf_t *fabricconf, ibfc_port_t *port)
{
	ibfc_port_t *prev = NULL;
	ibfc_port_t *last = NULL;
	int h = hash_name(port->name);
	port->next = fabricconf->ports[h];
	port->prev = NULL;
	if (fabricconf->ports[h])
	{
		last = fabricconf->ports[h];
		prev = fabricconf->ports[h];
		while (last) {
			prev = last;
			last = last->next;
		}
		port->prev = prev;
		prev->next = port;
		port->next = NULL;
	} else
		fabricconf->ports[h] = port;
	return (0);
}

static ibfc_port_t *
calloc_add_port(ibfc_conf_t *fabricconf, char *name, int port_num,
	ibfc_prop_t *prop)
{
	ibfc_port_t *port = calloc_port(name, port_num, prop);
	if (!port)
		return (NULL);
	add_port(fabricconf, port);
	return (port);
}

static int
add_link(ibfc_conf_t *fabricconf, char *lname, char *lport_str,
	ibfc_prop_t *prop, char *rname, char *rport_str)
{
	int found = 0;
	ibfc_port_t *lport, *rport;
	int lpn = IBFC_PORT_NUM_DONT_CARE;
	int rpn = IBFC_PORT_NUM_DONT_CARE;

	if (strcmp(lport_str, "-") != 0)
		lpn = strtol(lport_str, NULL, 0);
	if (strcmp(rport_str, "-") != 0)
		rpn = strtol(rport_str, NULL, 0);

	lport = find_port(fabricconf, lname, lpn);
	rport = find_port(fabricconf, rname, rpn);

	if (lport) {
		assert(lport->remote->remote == lport);
		if (fabricconf->warn_dup) {
			fprintf(fabricconf->err_fd,
				"WARN: redefining port "
				"\"%s\":%d <-> %d:\"%s\"\n",
				lport->name, lport->port_num,
				lport->remote->port_num, lport->remote->name);
			found = 1;
		}
		if (lport->remote != rport)
			remove_free_port(fabricconf, lport->remote);
		dup_prop(&lport->prop, prop);
	} else {
		lport = calloc_add_port(fabricconf, lname, lpn, prop);
		if (!lport) {
			fprintf(fabricconf->err_fd, "ERROR: failed to allocated lport\n");
			return (-ENOMEM);
		}
	}

	if (rport) {
		assert(rport->remote->remote == rport);
		if (fabricconf->warn_dup) {
			fprintf(fabricconf->err_fd, "WARN: redefining port "
				"\"%s\":%d <-> %d:\"%s\"\n",
				rport->name, rport->port_num,
				rport->remote->port_num, rport->remote->name);
			found = 1;
		}
		if (rport->remote != lport)
			remove_free_port(fabricconf, rport->remote);
		dup_prop(&rport->prop, prop);
	} else {
		rport = calloc_add_port(fabricconf, rname, rpn, prop);
		if (!rport) {
			fprintf(fabricconf->err_fd, "ERROR: failed to allocated lport\n");
			return (-ENOMEM);
		}
	}

	if (found) {
		fprintf(fabricconf->err_fd, "      NOW: \"%s\":%d <-> %d:\"%s\"\n",
			lport->name, lport->port_num,
			rport->port_num, rport->name);
	}

	lport->remote = rport;
	rport->remote = lport;

	return (0);
}

/**
 * Search for and set the properties see in this node
 */
static int
parse_properties(xmlNode *node, ibfc_prop_t *prop)
{
	int rc = 0;
	char *prop_str = NULL;

	if ((prop_str = (char *)xmlGetProp(node, (xmlChar *)"speed"))) {
		if (strcmp(prop_str, "SDR") == 0)
			prop->speed = IB_LINK_SPEED_ACTIVE_2_5;
		if (strcmp(prop_str, "DDR") == 0)
			prop->speed = IB_LINK_SPEED_ACTIVE_5;
		if (strcmp(prop_str, "QDR") == 0)
			prop->speed = IB_LINK_SPEED_ACTIVE_10;
	}
	xmlFree(prop_str);

	if ((prop_str = (char *)xmlGetProp(node, (xmlChar *)"width"))) {
		if (strcmp(prop_str, "1x") == 0 ||
		    strcmp(prop_str, "1X") == 0)
			prop->width = IB_LINK_WIDTH_ACTIVE_1X;
		if (strcmp(prop_str, "4x") == 0 ||
		    strcmp(prop_str, "4X") == 0)
			prop->width = IB_LINK_WIDTH_ACTIVE_4X;
		if (strcmp(prop_str, "8x") == 0 ||
		    strcmp(prop_str, "8X") == 0)
			prop->width = IB_LINK_WIDTH_ACTIVE_8X;
		if (strcmp(prop_str, "12x") == 0 ||
		    strcmp(prop_str, "12X") == 0)
			prop->width = IB_LINK_WIDTH_ACTIVE_12X;
	}
	xmlFree(prop_str);

	if ((prop_str = (char *)xmlGetProp(node, (xmlChar *)"length"))) {
		prop->length = strdup((char *)prop_str);
		if (!prop->length) {
			rc = -ENOMEM;
			prop->length = IBCONF_DEFAULT_PROP.length;
		}
	}
	xmlFree(prop_str);

	return (rc);
}

/** =========================================================================
 * Properties sitting on the stack may have memory allocated when they are
 * parsed into:
 * free this memory if necessary
 */
static void
free_stack_prop(ibfc_prop_t *p)
{
	_prop_free_chars(p);
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
remap_linklist(xmlNode *linklist, ch_map_t *ch_map)
{
	xmlNode *cur;
	for (cur = linklist->children; cur; cur = cur->next) {
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
		if (strcmp((char *)cur->name, "linklist") == 0) {
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

			remap_linklist(cur, ch_map);
		}
	}
	return (0);
}

static int
parse_port(char *node_name, xmlNode *portNode, ibfc_prop_t *parent_prop,
		ibfc_conf_t *fabricconf)
{
	xmlNode *cur = NULL;
	char *port = (char *)xmlGetProp(portNode, (xmlChar *)"num");
	ibfc_prop_t prop;
	char *r_port = NULL;
	char *r_node = NULL;

	if (!port)
		return (-EIO);

	dup_prop(&prop, parent_prop); /* inherit the properties from our parent */
	parse_properties(portNode, &prop); /* fill in with anything new */

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

	add_link(fabricconf, (char *)node_name, (char *)port, &prop,
		(char *)r_node, (char *)r_port);

	xmlFree(port);
	xmlFree(r_port);
	xmlFree(r_node);
	free_stack_prop(&prop);

	return (0);
}

static int
parse_linklist(xmlNode *linklist, ibfc_prop_t *parent_prop,
		ibfc_conf_t *fabricconf)
{
	xmlNode *cur = NULL;
	char *linklist_name = (char *)xmlGetProp(linklist, (xmlChar *)"name");
	ibfc_prop_t prop;

	if (!linklist_name)
		return (-EIO);

	dup_prop(&prop, parent_prop); /* inherit the properties from our parent */
	parse_properties(linklist, &prop); /* fill in with anything new */

	for (cur = linklist->children;
	     cur;
	     cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			if (strcmp((char *)cur->name, "port") == 0) {
				parse_port(linklist_name, cur, &prop, fabricconf);
			}
		}
	}
	free_stack_prop(&prop);
	xmlFree(linklist_name);
	return (0);
}

static int parse_chassismap(xmlNode *chassis, ibfc_prop_t *parent_prop,
				ibfc_conf_t *fabricconf)
{
	int rc = 0;
	xmlNode *cur;
	for (cur = chassis->children; cur; cur = cur->next) {
		if (strcmp((char *)cur->name, "linklist") == 0) {
			rc = parse_linklist(cur, parent_prop, fabricconf);
		}
		if (rc)
			break;
	}
	return (rc);
}

static int
process_chassis_model(ch_map_t *ch_map, char *model,
			ibfc_prop_t *parent_prop, ibfc_conf_t *fabricconf)
{
	xmlDoc *chassis_doc = NULL;
	xmlNode *root_element = NULL;
	xmlNode *cur = NULL;
	ibfc_prop_t prop = *parent_prop;
	int rc = 0;
	char *file = malloc(strlen(IBFC_CONFIG_DIR) + strlen(model)
				+strlen("/.xml   "));

	snprintf(file, 512, IBFC_CONFIG_DIR"/%s.xml", model);
	
	/*parse the file and get the DOM */
	chassis_doc = xmlReadFile(file, NULL, 0);

	if (chassis_doc == NULL) {
		fprintf(fabricconf->err_fd, "ERROR: could not parse chassis file %s\n", file);
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
					fprintf(fabricconf->err_fd, "ERROR processing %s; Model name does not "
						"match: %s != %s\n",
						file, model_name, model);
					rc = -EIO;
					goto exit;
				}

				remap_chassis_doc(cur, ch_map);
				parse_chassismap(cur, &prop, fabricconf);
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
parse_chassis(xmlNode *chassis, ibfc_prop_t *parent_prop,
		ibfc_conf_t *fabricconf)
{
	int rc = 0;
	xmlNode *cur = NULL;
	ibfc_prop_t prop;
	xmlChar *chassis_name = xmlGetProp(chassis, (xmlChar *)"name");
	xmlChar *chassis_model = xmlGetProp(chassis, (xmlChar *)"model");
	ch_map_t *ch_map = calloc(1, sizeof *ch_map);

	if (!ch_map) {
		rc = -ENOMEM;
		goto free_xmlChar;
	}

	if (!chassis_name || !chassis_model) {
		fprintf(fabricconf->err_fd, "chassis_[name|model] not defined\n");
		rc = -EIO;
		goto free_xmlChar;
	}

	ch_map->name = (char *)chassis_name;

	dup_prop(&prop, parent_prop); /* inherit the properties from our parent */
	parse_properties(chassis, &prop); /* fill in with anything new */

	/* first get a position/name map */
	for (cur = chassis->children;
	     cur;
	     cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			if (strcmp((char *)cur->name, "node") == 0) {
				ch_pos_map_t *n = NULL;
				char *pos = (char *)xmlGetProp(cur, (xmlChar *)"position");
				char *name = (char *)xmlNodeGetContent(cur);

				if (!pos || !name)
				{
					fprintf(fabricconf->err_fd, "Error "
						"processing chassis \"%s\": "
						"node \"%s\" position \"%s\""
						"\n",
						ch_map->name,
						name ? name : "<unknown>",
						pos ? pos : "<unknown>");
						rc = -EIO;
						goto free_pos_name_map;
				}

				n = malloc(sizeof *n);
				n->pos = pos;
				n->name = name;
				n->next = ch_map->map;
				ch_map->map = n;
			}
		}
	}

	/* then use that map to create real links with those names */
	/* read the model config */
	rc = process_chassis_model(ch_map, (char *)chassis_model, &prop, fabricconf);

free_pos_name_map:
	/* free our position/name map */
	while (ch_map->map) {
		ch_pos_map_t *tmp = ch_map->map;
		ch_map->map = ch_map->map->next;
		xmlFree(tmp->pos);
		xmlFree(tmp->name);
		free(tmp);
	}

	free_stack_prop(&prop);
	free(ch_map);

free_xmlChar:
	/* frees the memory pointed to in ch_map_t as well */
	xmlFree(chassis_name);
	xmlFree(chassis_model);

	return (rc);
}


static int
parse_fabric(xmlNode *fabric, ibfc_prop_t *parent_prop,
		ibfc_conf_t *fabricconf)
{
	int rc = 0;
	xmlNode *cur = NULL;
	xmlAttr *attr = NULL;
	ibfc_prop_t prop;
	xmlChar *fabric_name = xmlGetProp(fabric, (xmlChar *)"name");

	if (fabric_name) {
		fabricconf->name = strdup((char *)fabric_name);
		xmlFree(fabric_name);
	} else
		fabricconf->name = strdup("fabric");

	dup_prop(&prop, parent_prop); /* inherit the properties from our parent */
	parse_properties(fabric, &prop); /* fill in with anything new */

	for (cur = fabric->children; cur; cur = cur->next) {
		if (cur->type == XML_ELEMENT_NODE) {
			if (strcmp((char *)cur->name, "chassis") == 0)
				rc = parse_chassis(cur, &prop, fabricconf);
			else if (strcmp((char *)cur->name, "linklist") == 0)
				rc = parse_linklist(cur, &prop, fabricconf);
			else if (strcmp((char *)cur->name, "subfabric") == 0)
				rc = parse_fabric(cur, &prop, fabricconf);
			else {
				xmlChar * cont = xmlNodeGetContent(cur);
				fprintf(fabricconf->err_fd, "UNKNOWN XML node found\n");
				fprintf(fabricconf->err_fd, "%s = %s\n", cur->name, (char *)cont);
				xmlFree(cont);
				/* xmlGetProp(node, "key") could work as well */
				for (attr = cur->properties; attr; attr = attr->next) {
					fprintf(fabricconf->err_fd, "   %s=%s\n",
					(char *)attr->name, (char *)attr->children->content);
				}
			}
		}
		if (rc)
			break;
	}
	free_stack_prop(&prop);
	return (rc);
}

/**
 */
static int
parse_file(xmlNode * a_node, ibfc_conf_t *fabricconf)
{
	xmlNode *cur = NULL;
	ibfc_prop_t prop = IBCONF_DEFAULT_PROP;
	
	for (cur = a_node; cur; cur = cur->next)
		if (cur->type == XML_ELEMENT_NODE)
			if (strcmp((char *)cur->name, "fabric") == 0)
				return (parse_fabric(cur, &prop, fabricconf));

	return (-EIO);
}


/* debug */
#if 0
static void
debug_dump_link_conf(ibfc_conf_t *fabricconf)
{
	int i = 0;
	char prop[256];
	char rprop[256];
	printf("Name: %s\n", fabricconf->name);
	for (i = 0; i < HTSZ; i++) {
		ibfc_port_t *port = NULL;
		printf("   ports (%d)\n", i);
		for (port = fabricconf->ports[i]; port; port = port->next) {
			printf ("\"%s\":%d --(%s:%s)--> \"%s\":%d\n",
				port->name, port->port_num,
				ibfc_prop_str(port, prop, 256),
				ibfc_prop_str(port->remote, rprop, 256),
				port->remote->name, port->remote->port_num);
		}
	}
}
#endif


/** =========================================================================
 * External interface functions
 */

/* accessor function */
char *ibfc_conf_get_name(ibfc_conf_t *conf) { return (conf->name); }
int ibfc_prop_get_speed(ibfc_prop_t *prop) { return (prop->speed); }
int ibfc_prop_get_width(ibfc_prop_t *prop) { return (prop->width); }
char *ibfc_port_get_name(ibfc_port_t *port)
{
	if (strcmp(port->name, IBFC_PORT_NAME_DONT_CARE) == 0)
		return ("<don't care>");
	return (port->name);
}
int   ibfc_port_get_port_num(ibfc_port_t *port) { return (port->port_num); }
ibfc_prop_t *ibfc_port_get_prop(ibfc_port_t *port)
	{ return (&port->prop); }
ibfc_port_t *ibfc_port_get_remote(ibfc_port_t *port)
	{ return (port->remote); }
void  ibfc_port_set_user(ibfc_port_t *port, void *user_data)
	{ port->user_data = user_data; }
void *ibfc_port_get_user(ibfc_port_t *port) { return (port->user_data); }

char *
ibfc_prop_str(ibfc_port_t *port, char ret[], unsigned n)
{
	if (!n)
		return (NULL);

	ret[0] = '\0';
	snprintf(ret, n, "%s %s %s",
		dump_linkwidth_compat(port->prop.width),
		dump_linkspeed_compat(port->prop.speed),
		port->prop.length);
	return (ret);
}


/* interface functions */
ibfc_conf_t *
ibfc_alloc_conf(void)
{
	ibfc_conf_t *rc = calloc(1, sizeof *rc);
	if (!rc)
		return (NULL);

	rc->err_fd = stderr;
	rc->warn_dup = 0;
	return (rc);
}

static void
ibfc_free_ports(ibfc_conf_t *fabricconf)
{
	int i = 0;
	for (i = 0; i < HTSZ; i++) {
		ibfc_port_t *port = fabricconf->ports[i];
		while (port) {
			ibfc_port_t *tmp = port;
			port = port->next;
			free_port(tmp);
		}
		fabricconf->ports[i] = NULL;
	}
}

void
ibfc_free(ibfc_conf_t *fabricconf)
{
	if (!fabricconf)
		return;
	ibfc_free_ports(fabricconf);
	free(fabricconf);
}

void ibfc_set_stderr(ibfc_conf_t *fabricconf, FILE *f)
{
	if (fabricconf)
		fabricconf->err_fd = f;
}
void ibfc_set_warn_dup(ibfc_conf_t *fabricconf, int warn_dup)
{
	if (fabricconf)
		fabricconf->warn_dup = warn_dup;
}

int
ibfc_parse_file(char *file, ibfc_conf_t *fabricconf)
{
	xmlDoc *doc = NULL;
	xmlNode *root_element = NULL;
	int rc = 0;

	if (!fabricconf)
		return (-EINVAL);

	ibfc_free_ports(fabricconf);
	
	/* initialize the library */
	LIBXML_TEST_VERSION

	if (!file) {
		file = IBFC_DEF_CONFIG;
	}
	
	/* parse the file and get the DOM */
	doc = xmlReadFile(file, NULL, 0);
	if (doc == NULL) {
		fprintf(fabricconf->err_fd, "error: could not parse file %s\n", file);
		return (-EIO);
	}
	
	/*Get the root element node */
	root_element = xmlDocGetRootElement(doc);

	/* process the file */
	rc = parse_file(root_element, fabricconf);
	
	/*free the document */
	xmlFreeDoc(doc);
	
	/*
	 *Free the global variables that may
	 *have been allocated by the parser.
	 */
	xmlCleanupParser();

	return (rc);
}

ibfc_port_t *
ibfc_get_port(ibfc_conf_t *fabricconf, char *name, int p_num)
{
	return (find_port(fabricconf, name, p_num));
}

void
ibfc_free_port_list(ibfc_port_list_t *port_list)
{
	ibfc_port_t *head = port_list->head;
	while (head) {
		ibfc_port_t *tmp = head;
		head = head->next;
		free_port(tmp);
	}
	free(port_list);
}

int
ibfc_get_port_list(ibfc_conf_t *fabricconf, char *name,
		ibfc_port_list_t **list)
{
	ibfc_port_list_t *port_list = NULL;
	ibfc_port_t *cur = NULL;
	*list = NULL;
	int h = hash_name(name);

	port_list = calloc(1, sizeof *port_list);
	if (!port_list)
		return (-ENOMEM);

	for (cur = fabricconf->ports[h]; cur; cur = cur->next)
		if (strcmp((const char *)cur->name, (const char *)name) == 0) {
			ibfc_port_t *tmp = NULL;
			tmp = calloc_port(cur->name, cur->port_num, &cur->prop);
			if (!tmp) {
				ibfc_free_port_list(port_list);
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
ibfc_iter_ports(ibfc_conf_t *fabricconf, process_port_func func,
		void *user_data)
{
	int i = 0;
	for (i = 0; i < HTSZ; i++) {
		ibfc_port_t *port = NULL;
		for (port = fabricconf->ports[i]; port; port = port->next)
			func(port, user_data);
	}
}

void
ibfc_iter_port_list(ibfc_port_list_t *port_list,
			process_port_func func, void *user_data)
{
	ibfc_port_t *port = NULL;
	for (port = port_list->head; port; port = port->next)
		func(port, user_data);
}

char *
ibfc_prop_get_length(ibfc_prop_t *prop)
{
	return (prop->length);
}

