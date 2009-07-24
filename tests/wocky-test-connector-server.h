/*
 * wocky-test-sasl-auth-server.h - Header for TestSaslAuthServer
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

#ifndef __TEST_CONNECTOR_SERVER_H__
#define __TEST_CONNECTOR_SERVER_H__

#include <glib-object.h>
#include <gio/gnio.h>

#include "wocky-test-sasl-auth-server.h"

G_BEGIN_DECLS

#define CONNPROBLEM(x) (0x1 << x)

typedef enum
{
  XMPP_PROBLEM_NONE        = 0,
  XMPP_PROBLEM_BAD_XMPP    = CONNPROBLEM (0),
  XMPP_PROBLEM_NO_TLS      = CONNPROBLEM (1),
  XMPP_PROBLEM_TLS_REFUSED = CONNPROBLEM (2),
  XMPP_PROBLEM_FEATURES    = CONNPROBLEM (3),
  XMPP_PROBLEM_OLD_SERVER  = CONNPROBLEM (4),
  XMPP_PROBLEM_WEAK_SSL    = CONNPROBLEM (5),
  XMPP_PROBLEM_OLD_SSL     = CONNPROBLEM (6),
  XMPP_PROBLEM_OTHER_HOST  = CONNPROBLEM (7),
  XMPP_PROBLEM_TLS_LOAD    = CONNPROBLEM (8),
  XMPP_PROBLEM_NO_SESSION  = CONNPROBLEM (10),
  XMPP_PROBLEM_CANNOT_BIND = CONNPROBLEM (11),
  XMPP_PROBLEM_OLD_AUTH_FEATURE = CONNPROBLEM (12),

} XmppProblem;

typedef enum
{
  BIND_PROBLEM_NONE = 0,
  BIND_PROBLEM_INVALID     = CONNPROBLEM(0),
  BIND_PROBLEM_DENIED      = CONNPROBLEM(1),
  BIND_PROBLEM_CONFLICT    = CONNPROBLEM(2),
  BIND_PROBLEM_CLASH       = CONNPROBLEM(3),
  BIND_PROBLEM_REJECTED    = CONNPROBLEM(4),
  BIND_PROBLEM_FAILED      = CONNPROBLEM(5),
  BIND_PROBLEM_NO_JID      = CONNPROBLEM(6),
} BindProblem;

typedef enum
{
  SESSION_PROBLEM_NONE = 0,
  SESSION_PROBLEM_NO_SESSION = CONNPROBLEM(0),
  SESSION_PROBLEM_FAILED     = CONNPROBLEM(1),
  SESSION_PROBLEM_DENIED     = CONNPROBLEM(2),
  SESSION_PROBLEM_CONFLICT   = CONNPROBLEM(3),
  SESSION_PROBLEM_REJECTED   = CONNPROBLEM(4),
  SESSION_PROBLEM_NONSENSE   = CONNPROBLEM(5),
} SessionProblem;

typedef enum
{
  SERVER_DEATH_NONE = 0,
  SERVER_DEATH_SERVER_START = CONNPROBLEM(0),
  SERVER_DEATH_CLIENT_OPEN  = CONNPROBLEM(1),
  SERVER_DEATH_SERVER_OPEN  = CONNPROBLEM(2),
  SERVER_DEATH_FEATURES     = CONNPROBLEM(3),
  SERVER_DEATH_TLS_NEG      = CONNPROBLEM(4),
} ServerDeath;

typedef enum
{
  JABBER_PROBLEM_NONE         = 0,
  JABBER_PROBLEM_AUTH_REJECT  = CONNPROBLEM (0),
  JABBER_PROBLEM_AUTH_BIND    = CONNPROBLEM (1),
  JABBER_PROBLEM_AUTH_PARTIAL = CONNPROBLEM (2),
  JABBER_PROBLEM_AUTH_FAILED  = CONNPROBLEM (3),
  JABBER_PROBLEM_AUTH_STRANGE = CONNPROBLEM (4),
  JABBER_PROBLEM_AUTH_NIH     = CONNPROBLEM (5),
} JabberProblem;

typedef struct
{
  XmppProblem xmpp;
  BindProblem bind;
  SessionProblem session;
  ServerDeath death;
  JabberProblem jabber;
} ConnectorProblem;

typedef struct _TestConnectorServer TestConnectorServer;
typedef struct _TestConnectorServerClass TestConnectorServerClass;

struct _TestConnectorServerClass {
    GObjectClass parent_class;
};

struct _TestConnectorServer {
    GObject parent;
};

GType test_connector_server_get_type (void);

/* TYPE MACROS */
#define TEST_TYPE_CONNECTOR_SERVER \
  (test_connector_server_get_type ())
#define TEST_CONNECTOR_SERVER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_CONNECTOR_SERVER, \
   TestConnectorServer))
#define TEST_CONNECTOR_SERVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TEST_TYPE_CONNECTOR_SERVER, \
   TestConnectorServerClass))
#define TEST_IS_CONNECTOR_SERVER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TEST_TYPE_CONNECTOR_SERVER))
#define TEST_IS_CONNECTOR_SERVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TEST_TYPE_CONNECTOR_SERVER))
#define TEST_CONNECTOR_SERVER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_CONNECTOR_SERVER, \
   TestConnectorServerClass))

TestConnectorServer * test_connector_server_new (GIOStream *stream,
    gchar *mech,
    const gchar *user,
    const gchar *pass,
    const gchar *version,
    ConnectorProblem *problem,
    ServerProblem sasl_problem);

void test_connector_server_start (GObject *object);

G_END_DECLS

#endif /* #ifndef __TEST_CONNECTOR_SERVER_H__*/
