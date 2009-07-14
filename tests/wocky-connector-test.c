#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <wocky/wocky-connector.h>
#include <wocky/wocky-sasl-auth.h>

#include "wocky-test-connector-server.h"
#include "test-resolver.h"

#define SASL_DB_NAME "sasl-test.db"

#define INVISIBLE_HOST "unreachable.host"
#define VISIBLE_HOST   "reachable.host"
#define REACHABLE      "127.0.0.1"
#define UNREACHABLE    "127.255.255.255"
#define DUFF_H0ST      "no_such_host.at.all"

#define QUIET TRUE
#define NOISY FALSE

#define TLS   TRUE
#define NOTLS FALSE

#define PLAIN  FALSE
#define DIGEST TRUE

#define PORT_XMPP 5222
#define PORT_NONE 0

#define DOMAIN_NONE NULL
#define DOMAIN_ANY  "*any*"
#define DOMAIN_SASL "wocky_sasl_auth_error"
#define DOMAIN_CONN "wocky-connector-error"
#define DOMAIN_XCON "wocky-xmpp-connection-error"
#define DOMAIN_GIO  "g-io-error-quark"
#define DOMAIN_RES  "g-resolver-error-quark"

#define CONNECTOR_INTERNALS_TEST "/connector/basic/internals"

gboolean running_test = FALSE;
static GError *error = NULL;
static GResolver *original;
static GResolver *kludged;
static GMainLoop *mainloop;

typedef struct {
  gchar *desc;
  gboolean quiet;
  struct { gchar *domain; int code; WockySaslAuthMechanism mech; gpointer xmpp; gchar *jid; } result;
  struct {
    struct { gboolean tls; gchar *auth_mech; gchar *version; } features;
    struct { ServerProblem sasl; ConnectorProblem conn; } problem;
    struct { gchar *user; gchar *pass; } auth;
    guint port;
  } server;
  struct { char *srv; guint port; char *host; char *addr; char *srvhost; } dns;
  struct {
    gboolean require_tls;
    struct { gchar *jid; gchar *pass; gboolean secure; gboolean tls; } auth;
    struct { gchar *host; guint port; } options;
  } client;
  pid_t  server_pid;
  WockyConnector *connector;
} test_t;

test_t tests[] =
  { /* basic connection test, no SRV record, no host or port supplied: */
    /*
    { "/name/of/test",
      SUPPRESS_STDERR,
      // result of test:
      { DOMAIN, CODE, AUTH_MECH_USED, XMPP_CONNECTION_PLACEHOLDER },

      // Server Details:
      { { TLS_SUPPORT, AUTH_MECH_OR_NULL_FOR_ALL  },
        { SERVER_PROBLEM..., CONNECTOR_PROBLEM... },
        { USERNAME, PASSWORD },
        SERVER_LISTEN_PORT },

      // Fake DNS Record:
      // SRV_HOSTs SRV record → { HOSTNAME, PORT }
      // HOSTs     A   record → IP_ADDRESS
      // SRV_HOSTs A   record → IP_ADDR_OF_SRV_HOST
      { SRV_HOST, PORT, HOSTNAME, IP_ADDRESS, IP_ADDR_OF_SRV_HOST },

      // Client Details
      { TLS_REQUIRED,
        { BARE_JID, PASSWORD, MUST_BE_SECURE, MUST_BE_DIGEST_AUTH },
        { XMPP_HOSTNAME_OR_NULL, XMPP_PORT_OR_ZERO } }
        SERVER_PROCESS_ID }, */

    /* simple connection, followed by checks on all the internal state *
     * and get/set property methods to make sure they work             */
    { CONNECTOR_INTERNALS_TEST,
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { NULL, 0, "weasel-juice.org", REACHABLE, NULL },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 0 } } },

    /* No SRV or connect host specified */
    { "/connector/basic/noserv/nohost/noport",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { NULL, 0, "weasel-juice.org", REACHABLE, NULL },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 0 } } },

    /* No SRV or connect host specified, connect port specified */
    { "/connector/basic/noserv/nohost/port",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        8222 },
      { NULL, 0, "weasel-juice.org", REACHABLE, NULL },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 8222 } } },

    /* No SRV or connect host specified, bad port specified: FAIL */
    { "/connector/basic/noserv/nohost/duffport",
      NOISY,
      { DOMAIN_GIO, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        8222 },
      { NULL, 0, "weasel-juice.org", REACHABLE, NULL },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 8221 } } },

    /* No SRV record, connect host specified */
    { "/connector/basic/noserv/host/noport",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { NULL, 0, "schadenfreude.org", REACHABLE, NULL },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { "schadenfreude.org", 0 } } },

    /* No SRV record, connect host and port specified */
    { "/connector/basic/noserv/host/port",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5555 },
      { NULL, 0, "meerkats.net", REACHABLE, NULL },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { "meerkats.net", 5555 } } },

    /* No SRV record, connect host and bad port specified: FAIL */
    { "/connector/basic/noserv/host/duffport",
      NOISY,
      { DOMAIN_GIO, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5555 },
      { NULL, 0, "meerkats.net", REACHABLE, NULL },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { "meerkats.net", 5554 } } },

    /* No SRV record, bad connect host: FAIL */
    { "/connector/basic/noserv/duffhost/noport",
      NOISY,
      { DOMAIN_RES, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { NULL, NULL },
        PORT_NONE },
      { NULL, 0, NULL, NULL, NULL },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { DUFF_H0ST, 0 } } },

    /* No SRV record, bad connect host, port specified: FAIL */
    { "/connector/basic/noserv/duffhost/port",
      NOISY,
      { DOMAIN_RES, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { NULL, NULL },
        PORT_NONE },
      { NULL, 0, NULL, NULL, NULL },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { "still.no_such_host.at.all", 23 } } },

    /* SRV record specified */
    { "/connector/basic/serv/nohost/noport",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5050 },
      { "weasel-juice.org", 5050, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 0 } } },

    /* SRV record specified, port specified: ignore SRV and connect */
    { "/connector/basic/serv/nohost/port",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5051 },
      { "weasel-juice.org", 5050, "thud.org", UNREACHABLE, REACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 5051 } } },

    /* SRV record specified, bad port: ignore SRV and FAIL */
    { "/connector/basic/serv/nohost/duffport",
      NOISY,
      { DOMAIN_GIO, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5051 },
      { "weasel-juice.org", 5050, "thud.org", UNREACHABLE, REACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 5050 } } },

    /* SRV record, connect host specified: use connect host */
    { "/connector/basic/serv/host/noport",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", 5050, "thud.org", UNREACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { VISIBLE_HOST, 0 } } },

    /* SRV, connect host and port specified: use host and port */
    { "/connector/basic/serv/host/port",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5656 },
      { "weasel-juice.org", 5050, "thud.org", UNREACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { VISIBLE_HOST, 5656 } } },

    /* SRV record, connect host, bad port: ignore SRV, FAIL */
    { "/connector/basic/serv/host/duffport",
      NOISY,
      { DOMAIN_GIO, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5656 },
      { "weasel-juice.org", 5050, "thud.org", UNREACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { VISIBLE_HOST, 5655 } } },

    /* SRV record, bad connect host: use bad host and FAIL */
    { "/connector/basic/serv/duffhost/noport",
      NOISY,
      { DOMAIN_RES, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, REACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { DUFF_H0ST, 0 } } },

    /* SRV record, bad connect host, connect port: use bad host and FAIL */
    { "/connector/basic/serv/duffhost/port",
      NOISY,
      { DOMAIN_RES, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, REACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { DUFF_H0ST, PORT_XMPP } } },

    /* Bad SRV record: use it and FAIL */
    { "/connector/basic/duffserv/nohost/noport",
      NOISY,
      { DOMAIN_GIO, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", UNREACHABLE, REACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 0 } } },

    /* Bad SRV record, port specified, ignore SRV and connect to domain host */
    { "/connector/basic/duffserv/nohost/port",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5050 },
      { "weasel-juice.org", PORT_XMPP, "thud.org", UNREACHABLE, REACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 5050 } } },

    /* Bad SRV record, bad port specified, ignore SRV and FAIL */
    { "/connector/basic/duffserv/nohost/duffport",
      NOISY,
      { DOMAIN_GIO, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5050 },
      { "weasel-juice.org", PORT_XMPP, "thud.org", UNREACHABLE, REACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 5049 } } },

    /* Bad SRV record, connect host specified, ignore SRV */
    { "/connector/basic/duffserv/host/noport",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", 5050, "thud.org", UNREACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { VISIBLE_HOST, 0 } } },

    /* Bad SRV record, connect host and port given: ignore SRV */
    { "/connector/basic/duffserv/host/port",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_NR_MECHANISMS },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5151 },
      { "weasel-juice.org", 5050, "thud.org", UNREACHABLE, REACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { VISIBLE_HOST, 5151 } } },

    /* Bad SRV record, connect host and bad port, ignore SRV and FAIL */
    { "/connector/basic/duffserv/host/duffport",
      NOISY,
      { DOMAIN_GIO, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5151 },
      { "weasel-juice.org", 5050, "thud.org", UNREACHABLE, REACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { VISIBLE_HOST, 5149 } } },

    /* Bad SRV record, bad host and bad port: Just FAIL */
    { "/connector/basic/duffserv/duffhost/noport",
      NOISY,
      { DOMAIN_GIO, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", 5050, "thud.org", UNREACHABLE, REACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { INVISIBLE_HOST, 0 } } },

    /*Bad SRV and connect host, ignore SRV and FAIL */
    { "/connector/basic/duffserv/duffhost/port",
      NOISY,
      { DOMAIN_GIO, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        5151 },
      { "weasel-juice.org", 5050, "thud.org", UNREACHABLE, REACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { INVISIBLE_HOST, 5151 } } },

    /* ******************************************************************* *
     * that's it for the basic DNS/connection-logic tests                  *
     * now onto the post-tcp-connect stages:                               */
    { "/connector/auth/secure/no-tlsplain/notls/nodigest",
      NOISY,
      { DOMAIN_SASL, WOCKY_SASL_AUTH_ERROR_NO_SUPPORTED_MECHANISMS },
      { { NOTLS, "PLAIN" },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", DIGEST, TLS },
        { NULL, 0 } } },

    { "/connector/auth/secure/no-tlsplain/notls/digest",
      NOISY,
      { NULL, 0, WOCKY_SASL_AUTH_DIGEST_MD5 },
      { { NOTLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", DIGEST, TLS },
        { NULL, 0 } } },

    { "/connector/auth/insecure/no-tlsplain/notls/nodigest",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_PLAIN },
      { { NOTLS, "PLAIN" },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/auth/insecure/no-tlsplain/notls/digest",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_DIGEST_MD5 },
      { { NOTLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 0 } } },

    /* *************************************************************** *
     * This block of tests will fail as we don't advertise TLS support */
    { "/connector/auth/insecure/no-tlsplain/notls/any",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_TLS_UNAVAILABLE },
      { { NOTLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM /**/ },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { TRUE,
        { "moose@weasel-juice.org", "something", FALSE, TLS },
        { NULL, 0 } } },

    { "/connector/auth/insecure/tlsplain/notls/any",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_TLS_UNAVAILABLE },
      { { NOTLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM /**/ },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { TRUE,
        { "moose@weasel-juice.org", "something", FALSE, NOTLS },
        { NULL, 0 } } },

    { "/connector/auth/secure/no-tlsplain/notls/any",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_TLS_UNAVAILABLE },
      { { NOTLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM /**/ },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { TRUE,
        { "moose@weasel-juice.org", "something", TRUE, TLS },
        { NULL, 0 } } },

    { "/connector/auth/secure/tlsplain/notls/any",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_TLS_UNAVAILABLE },
      { { NOTLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM /**/ },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { TRUE,
        { "moose@weasel-juice.org", "something", TRUE, NOTLS },
        { NULL, 0 } } },

    /* **************************************************************** *
     * this will be a mix of failures and sucesses depending on whether *
     * we allow plain auth or not                                       */
    { "/connector/auth/secure/no-tlsplain/tls/plain",
      NOISY,
      { DOMAIN_SASL, WOCKY_SASL_AUTH_ERROR_NO_SUPPORTED_MECHANISMS },
      { { TLS, "PLAIN" },
        { SERVER_PROBLEM_INVALID_PASSWORD, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", DIGEST, TLS },
        { NULL, 0 } } },

    { "/connector/auth/secure/tlsplain/tls/plain",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_PLAIN },
      { { TLS, "PLAIN" },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, TLS },
        { NULL, 0 } } },

    { "/connector/auth/insecure/no-tlsplain/tls/plain",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_PLAIN },
      { { TLS, "PLAIN" },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", DIGEST, NOTLS },
        { NULL, 0 } } },

    { "/connector/auth/insecure/tlsplain/tls/plain",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_PLAIN },
      { { TLS, "PLAIN" },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/tls+auth/secure/no-tlsplain/tls/plain",
      NOISY,
      { DOMAIN_SASL, WOCKY_SASL_AUTH_ERROR_NO_SUPPORTED_MECHANISMS },
      { { TLS, "PLAIN" },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", DIGEST, TLS },
        { NULL, 0 } } },

    { "/connector/tls+auth/secure/tlsplain/tls/plain",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_PLAIN },
      { { TLS, "PLAIN" },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, TLS },
        { NULL, 0 } } },

    { "/connector/tls+auth/insecure/no-tlsplain/tls/plain",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_PLAIN },
      { { TLS, "PLAIN" },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", DIGEST, NOTLS },
        { NULL, 0 } } },

    { "/connector/tls+auth/insecure/tlsplain/tls/plain",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_PLAIN },
      { { TLS, "PLAIN" },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },
    /* **************************************************************** *
     * these should all be digest auth successes                        */
    { "/connector/auth/secure/no-tlsplain/tls/digest",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_DIGEST_MD5 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", DIGEST, TLS },
        { NULL, 0 } } },

    { "/connector/auth/secure/tlsplain/tls/digest",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_DIGEST_MD5 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, TLS },
        { NULL, 0 } } },

    { "/connector/auth/insecure/no-tlsplain/tls/digest",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_DIGEST_MD5 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", DIGEST, NOTLS },
        { NULL, 0 } } },

    { "/connector/auth/insecure/tlsplain/tls/digest",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_DIGEST_MD5 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
          { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/tls+auth/secure/no-tlsplain/tls/digest",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_DIGEST_MD5 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", DIGEST, TLS },
        { NULL, 0 } } },

    { "/connector/tls+auth/secure/tlsplain/tls/digest",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_DIGEST_MD5 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, TLS },
        { NULL, 0 } } },

    { "/connector/tls+auth/insecure/no-tlsplain/tls/digest",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_DIGEST_MD5 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", DIGEST, NOTLS },
        { NULL, 0 } } },

    { "/connector/tls+auth/insecure/tlsplain/tls/digest",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_DIGEST_MD5 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* ***************************************************************** *
     * SASL problems                                                     */
    { "/connector/problem/sasl/bad-pass",
      NOISY,
      { DOMAIN_SASL, WOCKY_SASL_AUTH_ERROR_FAILURE },
      { { TLS, NULL },
        { SERVER_PROBLEM_INVALID_PASSWORD, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "foo", "bar" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "foo@weasel-juice.org", "notbar", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/problem/sasl/bad-user",
      NOISY,
      { DOMAIN_SASL, WOCKY_SASL_AUTH_ERROR_FAILURE },
      { { TLS, NULL },
        { SERVER_PROBLEM_INVALID_USERNAME, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "caribou@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/problem/sasl/no-sasl",
      NOISY,
      { DOMAIN_SASL, WOCKY_SASL_AUTH_ERROR_SASL_NOT_SUPPORTED },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_SASL, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/problem/sas/no-mechanisms",
      NOISY,
      { DOMAIN_SASL, WOCKY_SASL_AUTH_ERROR_SASL_NOT_SUPPORTED },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_MECHANISMS, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/problem/sasl/bad-mechanism",
      NOISY,
      { DOMAIN_SASL, WOCKY_SASL_AUTH_ERROR_NO_SUPPORTED_MECHANISMS },
      { { TLS, "omg-poniez" },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* ********************************************************************* */
    /* TLS error conditions */
    { "/connector/problem/tls/refused",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_TLS_REFUSED },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_TLS_REFUSED },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* ********************************************************************* *
     * Invalid JID                                                           */
    { "/connector/problem/jid/invalid",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_BAD_JID },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_NONE },
      { NULL, 0, "thud.org", REACHABLE },
      { FALSE,
        { "blahblahblah", "something", PLAIN, NOTLS },
        { "weasel-juice.org", PORT_XMPP } } },

    { "/connector/problem/jid/domainless",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_BAD_JID },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_NONE },
      { "weasel-juice.org", 5001, "thud.org", REACHABLE, REACHABLE },
      { FALSE,
        { "moose@", "something", PLAIN, NOTLS },
        { "weasel-juice.org", 0 } } },

    /* ********************************************************************* *
     * XMPP errors                                                           */
    { "/connector/problem/xmpp/version",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_NON_XMPP_V1_SERVER },
      { { TLS, NULL, "3.1415" },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_PROBLEM },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/problem/xmpp/features",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_BAD_FEATURES },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_FEATURES },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* WOCKY_CONNECTOR_ERROR_BIND_UNAVAILABLE */
    { "/connector/problem/xmpp/no-bind",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_BIND_UNAVAILABLE },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_CANNOT_BIND },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* WOCKY_CONNECTOR_ERROR_BIND_INVALID      */
    { "/connector/problem/xmpp/bind/failed",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_BIND_INVALID },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_BIND_INVALID },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* WOCKY_CONNECTOR_ERROR_BIND_DENIED     */
    { "/connector/problem/xmpp/bind/denied",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_BIND_DENIED },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_BIND_DENIED },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* WOCKY_CONNECTOR_ERROR_BIND_CONFLICT      */
    { "/connector/problem/xmpp/bind/conflict",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_BIND_CONFLICT },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_BIND_CONFLICT },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* WOCKY_CONNECTOR_ERROR_BIND_REJECTED    */
    { "/connector/problem/xmpp/bind/rejected",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_BIND_REJECTED },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_BIND_REJECTED },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* WOCKY_CONNECTOR_ERROR_BIND_FAILED    */
    { "/connector/problem/xmpp/bind/failed",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_BIND_FAILED },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_BIND_FAILED },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/problem/xmpp/session/none",
      NOISY,
      { DOMAIN_NONE, 0, WOCKY_SASL_AUTH_DIGEST_MD5 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_NO_SESSION },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* WOCKY_CONNECTOR_ERROR_SESSION_FAILED   */
    { "/connector/problem/xmpp/session/failed",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_SESSION_FAILED },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_SESSION_FAILED },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* WOCKY_CONNECTOR_ERROR_SESSION_DENIED   */
    { "/connector/problem/xmpp/session/denied",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_SESSION_DENIED },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_SESSION_DENIED },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* WOCKY_CONNECTOR_ERROR_SESSION_CONFLICT */
    { "/connector/problem/xmpp/session/conflict",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_SESSION_CONFLICT },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_SESSION_CONFLICT },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* WOCKY_CONNECTOR_ERROR_SESSION_REJECTED */
    { "/connector/problem/xmpp/session/rejected",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_SESSION_REJECTED },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_SESSION_REJECTED },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/problem/xmpp/session/nonsense",
      NOISY,
      { DOMAIN_CONN, WOCKY_CONNECTOR_ERROR_SESSION_FAILED },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_SESSION_NONSENSE },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/econnreset/server-start",
      NOISY,
      { DOMAIN_ANY, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_DIE_SERVER_START },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/econnreset/client-open",
      NOISY,
      { DOMAIN_ANY, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_DIE_CLIENT_OPEN },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/econnreset/server-open",
      NOISY,
      { DOMAIN_ANY, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_DIE_SERVER_OPEN },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/econnreset/features",
      NOISY,
      { DOMAIN_ANY, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_DIE_FEATURES },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    { "/connector/econnreset/tls-negotiate",
      QUIET,
      { DOMAIN_ANY, 0 },
      { { TLS, NULL },
        { SERVER_PROBLEM_NO_PROBLEM, CONNECTOR_PROBLEM_DIE_TLS_NEG },
        { "moose", "something" },
        PORT_XMPP },
      { "weasel-juice.org", PORT_XMPP, "thud.org", REACHABLE, UNREACHABLE },
      { FALSE,
        { "moose@weasel-juice.org", "something", PLAIN, NOTLS },
        { NULL, 0 } } },

    /* we are done, cap the list: */
    { NULL }
  };

/* ************************************************************************* */
#define STRING_OK(x) (((x) != NULL) && (*x != '\0'))

static void
setup_dummy_dns_entries (const test_t *test)
{
  TestResolver *tr = TEST_RESOLVER (kludged);
  guint port = test->dns.port ? test->dns.port : PORT_XMPP;
  const char *domain = test->dns.srv;
  const char *host = test->dns.host;
  const char *addr = test->dns.addr;
  const char *s_ip = test->dns.srvhost;

  test_resolver_reset (tr);

  if (STRING_OK (domain) && STRING_OK (host))
    test_resolver_add_SRV (tr, "xmpp-client", "tcp", domain, host, port);

  if (STRING_OK (domain) && STRING_OK (s_ip))
    test_resolver_add_A (tr, domain, s_ip);

  if (STRING_OK (host) && STRING_OK (addr))
    test_resolver_add_A (tr, host, addr);

  test_resolver_add_A (tr, INVISIBLE_HOST, UNREACHABLE);
  test_resolver_add_A (tr, VISIBLE_HOST, REACHABLE);
}

/* ************************************************************************* */
/* Dummy XMPP server */
static gboolean
client_connected (GIOChannel *channel,
    GIOCondition cond,
    gpointer data)
{
  struct sockaddr_in client;
  socklen_t clen = sizeof (client);
  int ssock = g_io_channel_unix_get_fd (channel);
  int csock = accept (ssock, (struct sockaddr *)&client, &clen);
  GSocket *gsock = g_socket_new_from_fd (csock, NULL);
  test_t *test = data;
  ConnectorProblem cproblem = CONNECTOR_PROBLEM_NO_PROBLEM;

  GSocketConnection *gconn;
  TestConnectorServer *server;
  long flags;

  if (csock < 0)
    {
      perror ("accept() failed");
      g_warning ("accept() failed on socket that should have been ready.");
      return TRUE;
    }

  while (g_source_remove_by_user_data (test));
  g_io_channel_close (channel);

  cproblem |= test->server.problem.conn;
  if (!test->server.features.tls)
      cproblem |= CONNECTOR_PROBLEM_NO_TLS;


  flags = fcntl (csock, F_GETFL );
  flags = flags & ~O_NONBLOCK;
  fcntl (csock, F_SETFL, flags);
  gconn = g_object_new (G_TYPE_SOCKET_CONNECTION, "socket", gsock, NULL);
  server = test_connector_server_new (G_IO_STREAM (gconn),
      test->server.features.auth_mech,
      test->server.auth.user,
      test->server.auth.pass,
      test->server.features.version,
      cproblem,
      test->server.problem.sasl);
  test_connector_server_start (G_OBJECT (server));
  return FALSE;
}

static void
start_dummy_xmpp_server (test_t *test)
{
  int ssock;
  int reuse = 1;
  struct sockaddr_in server;
  GIOChannel *channel;
  pid_t server_pid;
  int res = -1;
  guint port = test->server.port;
  int i;

  if (port == 0)
    return;

  /* setenv ("WOCKY_DEBUG","all",TRUE); */
  memset (&server, 0, sizeof (server));

  server.sin_family = AF_INET;
  inet_aton (REACHABLE, &server.sin_addr);
  server.sin_port = htons (port);
  ssock = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
  setsockopt (ssock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse));

  /* it can take a couple of attempts before the last test run actually *
   * relinquishes the port we want                                      */
  for (res = -1, i = 0; res != 0 && i < 3; i++, sleep (1))
    res = bind (ssock, (struct sockaddr *)&server, sizeof (server));

  if (res != 0)
    {
      int code = errno;
      char *err = g_strdup_printf ("bind to " REACHABLE ":%d failed", port);
      perror (err);
      g_free (err);
      exit (code);
    }

  res = listen (ssock, 1024);
  if (res != 0)
    {
      int code = errno;
      char *err = g_strdup_printf ("listen on " REACHABLE ":%d failed", port);
      perror (err);
      g_free (err);
      exit (code);
    }

  server_pid = fork ();

  if (server_pid < 0)
    {
      perror ("fork() of dummy server failed\n");
      g_main_loop_quit (mainloop);
      exit (server_pid);
    }

  if (server_pid)
    {
      test->server_pid = server_pid;
      close (ssock);
      return;
    }

  /* this test is guaranteed to produce an uninteresting error message
   * from the dummy XMPP server process: mask it (we can't just close stderr
   * because that makes it fail in a way which is detected as a different
   * error, which causes the test to fail) */
  if (test->quiet)
    {
      int nullfd = open ("/dev/null", O_WRONLY);
      int errfd  = fileno (stderr);
      dup2 (nullfd, errfd);
    }
  channel = g_io_channel_unix_new (ssock);
  g_io_add_watch (channel, G_IO_IN|G_IO_PRI, client_connected, (gpointer)test);
  g_main_loop_run (mainloop);
  exit (0);
}
/* ************************************************************************* */
static void
test_done (GObject *source,
    GAsyncResult *res,
    gpointer data)
{
  test_t *test = data;
  WockyConnector *wcon = WOCKY_CONNECTOR (source);
  WockyXmppConnection *conn = NULL;

  error = NULL;
  conn = wocky_connector_connect_finish (wcon, res, &error, &test->result.jid);
  if (conn != NULL)
    test->result.xmpp = g_object_ref (conn);

  if (test->server_pid > 0)
    {
      /* kick it and wait for the thing to die */
      kill (test->server_pid, SIGKILL);
      waitpid (test->server_pid, NULL, WNOHANG);
    }

  if (g_main_loop_is_running (mainloop))
    g_main_loop_quit (mainloop);

  running_test = FALSE;
}

typedef void (*test_func) (gconstpointer);

static gboolean
start_test (gpointer data)
{
  test_t *test = data;
  wocky_connector_connect_async (test->connector, test_done, data);
  return FALSE;
}

static void
run_test (gpointer data)
{
  WockyConnector *wcon = NULL;
  test_t *test = data;
  struct stat dummy;
  gchar base[PATH_MAX + 1];
  char *path;

  /* clean up any leftover messes from previous tests     */
  /* unlink the sasl db tmpfile, it will cause a deadlock */
  path = g_strdup_printf ("%s/__db.%s",
      getcwd (base, sizeof (base)), SASL_DB_NAME);
  g_assert ((stat (path, &dummy) != 0) || (unlink (path) == 0));
  g_free (path);
  /* end of cleanup block */

  start_dummy_xmpp_server (test);
  setup_dummy_dns_entries (test);

  wcon = g_object_new ( WOCKY_TYPE_CONNECTOR,
      "jid"                     , test->client.auth.jid,
      "password"                , test->client.auth.pass,
      "xmpp-server"             , test->client.options.host,
      "xmpp-port"               , test->client.options.port,
      "tls-required"            , test->client.require_tls,
      "encrypted-plain-auth-ok" , !test->client.auth.secure,
      /* this refers to PLAINTEXT vs CRYPT, not PLAIN vs DIGEST */
      "plaintext-auth-allowed"  , !test->client.auth.tls,
      /* insecure tls cert/etc not yet implemented */
      "ignore-ssl-errors"       , FALSE,
      NULL);

  test->connector = wcon;
  running_test = TRUE;
  g_idle_add (start_test, test);

  g_main_loop_run (mainloop);

  if (test->result.domain == NULL)
    {
      if (error)
        fprintf (stderr, "Error: %s.%d: %s\n",
            g_quark_to_string (error->domain),
            error->code,
            error->message);
      g_assert (error == NULL);
      g_assert (test->result.xmpp != NULL);

      /* make sure we selected the right auth mechanism */
      if (test->result.mech < WOCKY_SASL_AUTH_NR_MECHANISMS)
        {
          WockySaslAuthMechanism mech = wocky_connector_auth_mechanism (wcon);
          g_assert (test->result.mech == mech);
        }

      /* we got a JID back, I hope */
      g_assert (test->result.jid != NULL);
      g_assert (*test->result.jid != '\0');
      g_free (test->result.jid);

      /* property get/set functionality */
      if (!strcmp (test->desc, CONNECTOR_INTERNALS_TEST))
        {
          int i;
          gchar *identity = NULL;
          WockyConnector *tmp = wocky_connector_new ("foo@bar.org","abc","xyz");
          WockyXmppStanza *feat = NULL;
          const gchar *prop = NULL;
          const gchar *str_prop[] = { "jid", "password", "xmpp-server", NULL };
          const gchar *str_vals[] = { "abc", "PASSWORD", "xmpp.server", NULL };
          const gchar *boolprop[] = { "ignore-ssl-errors",
                                      "plaintext-auth-allowed",
                                      "encrypted-plain-auth-ok",
                                      "tls-required",
                                      NULL };

          g_object_get (wcon, "identity", &identity, "features", &feat, NULL);
          g_assert (identity != NULL);
          g_assert (*identity |= '\0');
          g_assert (feat != NULL);
          g_assert (G_OBJECT_TYPE (feat) == WOCKY_TYPE_XMPP_STANZA);
          g_free (identity);
          g_object_unref (feat);
          identity = NULL;

          g_object_get (wcon, "resource", &identity, NULL);
          g_assert (identity != NULL);
          g_assert (*identity |= '\0');
          g_free (identity);

          for (i = 0, prop = str_prop[0]; prop; prop = str_prop[++i])
            {
              gchar *val = NULL;
              g_object_set (tmp, prop, str_vals[i], NULL);
              g_object_get (tmp, prop, &val, NULL);
              g_assert (!strcmp (val, str_vals[i]));
              g_assert (val != str_vals[i]);
              g_free (val);
            }

          for (i = 0, prop = boolprop[0]; prop; prop = boolprop[++i])
            {
              gboolean val;
              g_object_set (tmp, prop, TRUE, NULL);
              g_object_get (tmp, prop, &val, NULL);
              g_assert (val);
              g_object_set (tmp, prop, FALSE, NULL);
              g_object_get (tmp, prop, &val, NULL);
              g_assert (!val);
            }

          g_object_set (tmp, "xmpp-port", 31415, NULL);
          g_object_get (tmp, "xmpp-port", &i, NULL);
          g_assert (i == 31415);

          g_object_unref (tmp);
        }
    }
  else if (!strcmp (test->result.domain, DOMAIN_ANY))
    {
      g_assert (test->result.xmpp == NULL);
    }
  else
    {
      GQuark domain = 0;
      domain = g_quark_from_string (test->result.domain);
      if (!g_error_matches (error, domain, test->result.code))
        fprintf (stderr, "ERROR: %s.%d: %s\n",
            g_quark_to_string (error->domain),
            error->code,
            error->message);
      g_assert_error (error, domain, test->result.code);
    }

  if (wcon != NULL)
    g_object_unref (wcon);

  if (error != NULL)
    g_error_free (error);

  if (test->result.xmpp != NULL)
    g_object_unref (test->result.xmpp);

  error = NULL;
}

int
main (int argc,
    char **argv)
{
  int i;
  gchar base[PATH_MAX + 1];
  gchar *path = NULL;
  struct stat dummy;

  g_thread_init (NULL);
  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  /* hook up the fake DNS resolver that lets us divert A and SRV queries *
   * into our local cache before asking the real DNS                     */
  original = g_resolver_get_default ();
  kludged = g_object_new (TEST_TYPE_RESOLVER, "real-resolver", original, NULL);
  g_resolver_set_default (kludged);

  /* unlink the sasl db, we want to test against a fresh one */
  path = g_strdup_printf ("%s/%s", getcwd (base, sizeof (base)), SASL_DB_NAME);
  g_assert ((stat (path, &dummy) != 0) || (unlink (path) == 0));
  g_free (path);


  mainloop = g_main_loop_new (NULL, FALSE);

  for (i = 0; tests[i].desc != NULL; i++)
    g_test_add_data_func (tests[i].desc, &tests[i], (test_func)run_test);

  return g_test_run ();
}
