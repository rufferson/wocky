#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>

#include <wocky/wocky-session.h>
#include <wocky/wocky-utils.h>

#include "wocky-test-helper.h"

static void
test_instantiation (void)
{
  WockyTestStream *stream;
  WockyXmppConnection *connection;
  WockySession *session;

  stream = g_object_new (WOCKY_TYPE_TEST_STREAM, NULL);
  connection = wocky_xmpp_connection_new (stream->stream0);

  session = wocky_session_new (connection);
  g_assert (session != NULL);
  g_assert (WOCKY_IS_SESSION (session));

  g_object_unref (stream);
  g_object_unref (connection);
  g_object_unref (session);
}

static void
test_get_porter (void)
{
  test_data_t *test = setup_test ();
  WockySession *session;
  WockyPorter *porter;

  session = wocky_session_new (test->in);

  porter = wocky_session_get_porter (session);
  g_assert (WOCKY_IS_PORTER (porter));

  g_object_unref (session);
  teardown_test (test);
}

static void
test_get_contact_factory (void)
{
  test_data_t *test = setup_test ();
  WockySession *session;
  WockyContactFactory *factory;

  session = wocky_session_new (test->in);

  factory = wocky_session_get_contact_factory (session);
  g_assert (WOCKY_IS_CONTACT_FACTORY (factory));

  g_object_unref (session);
  teardown_test (test);
}

static void
test_get_roster (void)
{
  test_data_t *test = setup_test ();
  WockySession *session;
  WockyRoster *roster;

  session = wocky_session_new (test->in);

  roster = wocky_session_get_roster (session);
  g_assert (WOCKY_IS_ROSTER (roster));

  g_object_unref (session);
  teardown_test (test);
}

int
main (int argc, char **argv)
{
  int result;

  test_init (argc, argv);

  g_test_add_func ("/session/instantiation", test_instantiation);
  g_test_add_func ("/session/get-porter", test_get_porter);
  g_test_add_func ("/session/get-contact-factory", test_get_contact_factory);
  g_test_add_func ("/session/get-roster", test_get_roster);

  result = g_test_run ();
  test_deinit ();
  return result;
}
