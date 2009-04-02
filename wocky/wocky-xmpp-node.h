/*
 * wocky-xmpp-node.h - Header for Wocky xmpp nodes
 * Copyright (C) 2006 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd@luon.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __WOCKY_XMPP_NODE_H__
#define __WOCKY_XMPP_NODE_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _WockyXmppNode WockyXmppNode;

struct _WockyXmppNode {
  gchar *name;
  gchar *content;

  /* Private */
  gchar *language;
  GQuark ns;
  GSList *attributes;
  GSList *children;
};

typedef gboolean (*wocky_xmpp_node_each_attr_func )(const gchar *key,
    const gchar *value, const gchar *ns, gpointer user_data);

typedef gboolean (*wocky_xmpp_node_each_child_func) (WockyXmppNode *node,
    gpointer user_data);

void wocky_xmpp_node_each_attribute (WockyXmppNode *node,
    wocky_xmpp_node_each_attr_func func, gpointer user_data);

void wocky_xmpp_node_each_child (WockyXmppNode *node,
    wocky_xmpp_node_each_child_func func, gpointer user_data);

const gchar *wocky_xmpp_node_get_attribute (WockyXmppNode *node,
    const gchar *key);

const gchar *wocky_xmpp_node_get_attribute_ns (WockyXmppNode *node,
    const gchar *key, const gchar *ns);

void  wocky_xmpp_node_set_attribute (WockyXmppNode *node, const gchar *key,
    const gchar *value);

void  wocky_xmpp_node_set_attribute_ns (WockyXmppNode *node,
    const gchar *key, const gchar *value, const gchar *ns);

/* Set attribute with the given size for the value */
void wocky_xmpp_node_set_attribute_n (WockyXmppNode *node, const gchar *key,
    const gchar *value, gsize value_size);

void wocky_xmpp_node_set_attribute_n_ns (WockyXmppNode *node,
    const gchar *key, const gchar *value, gsize value_size,
                                         const gchar *ns);

/* Getting children */
WockyXmppNode *wocky_xmpp_node_get_child (WockyXmppNode *node,
    const gchar *name);

WockyXmppNode *wocky_xmpp_node_get_child_ns (WockyXmppNode *node,
    const gchar *name, const gchar *ns);

/* Creating child nodes */
WockyXmppNode *wocky_xmpp_node_add_child (WockyXmppNode *node,
    const gchar *name);

WockyXmppNode *wocky_xmpp_node_add_child_ns (WockyXmppNode *node,
    const gchar *name, const gchar *ns);

WockyXmppNode *wocky_xmpp_node_add_child_with_content (WockyXmppNode *node,
    const gchar *name, const char *content);

WockyXmppNode *wocky_xmpp_node_add_child_with_content_ns (
    WockyXmppNode *node, const gchar *name, const gchar *content,
    const gchar *ns);

/* Setting/Getting namespaces */
void wocky_xmpp_node_set_ns (WockyXmppNode *node, const gchar *ns);
const gchar *wocky_xmpp_node_get_ns (WockyXmppNode *node);

/* Setting/Getting language */
const gchar *wocky_xmpp_node_get_language (WockyXmppNode *node);
void wocky_xmpp_node_set_language (WockyXmppNode *node, const gchar *lang);
void wocky_xmpp_node_set_language_n (WockyXmppNode *node, const gchar *lang,
    gsize lang_size);


/* Setting or adding content */
void wocky_xmpp_node_set_content (WockyXmppNode *node, const gchar *content);
void wocky_xmpp_node_append_content (WockyXmppNode *node,
    const gchar *content);

void wocky_xmpp_node_append_content_n (WockyXmppNode *node,
    const gchar *content, gsize size);

/* Return a reading friendly representation of a node and its children.
 * Should be use for debugging purpose only. */
gchar *wocky_xmpp_node_to_string (WockyXmppNode *node);

/* Create a new standalone node, usually only used by the stanza object */
WockyXmppNode *wocky_xmpp_node_new (const char *name);

/* Frees the node and all it's children! */
void wocky_xmpp_node_free (WockyXmppNode *node);

/* Compare two nodes and all their children */
gboolean wocky_xmpp_node_equal (WockyXmppNode *node0,
    WockyXmppNode *node1);

G_END_DECLS

#endif /* #ifndef __WOCKY_XMPP_NODE_H__*/
