/*
 * wocky-xmpp-connection.c - Source for WockyXmppConnection
 * Copyright (C) 2006-2009 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>

#include "wocky-xmpp-connection.h"
#include "wocky-signals-marshal.h"

#include "wocky-xmpp-reader.h"
#include "wocky-xmpp-writer.h"
#include "wocky-xmpp-stanza.h"

#define BUFFER_SIZE 1024

static void _xmpp_connection_received_data (GObject *source,
    GAsyncResult *result, gpointer user_data);

G_DEFINE_TYPE(WockyXmppConnection, wocky_xmpp_connection, G_TYPE_OBJECT)

/* properties */
enum
{
  PROP_BASE_STREAM = 1,
};

/* private structure */
typedef struct _WockyXmppConnectionPrivate WockyXmppConnectionPrivate;

struct _WockyXmppConnectionPrivate
{
  gboolean dispose_has_run;
  WockyXmppReader *reader;
  WockyXmppWriter *writer;

  guint8 stream_flags;

  GIOStream *stream;

  GSimpleAsyncResult *input_result;
  GCancellable *input_cancellable;

  GSimpleAsyncResult *output_result;
  GCancellable *output_cancellable;

  guint8 buffer[BUFFER_SIZE];

  guint last_id;
};

/**
 * wocky_xmpp_connection_error_quark
 *
 * Get the error quark used by the connection.
 *
 * Returns: the quark for connection errors.
 */
GQuark
wocky_xmpp_connection_error_quark (void)
{
  static GQuark quark = 0;

  if (quark == 0)
    quark = g_quark_from_static_string ("wocky-xmpp-connection-error");

  return quark;
}

#define WOCKY_XMPP_CONNECTION_GET_PRIVATE(o)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), WOCKY_TYPE_XMPP_CONNECTION, \
    WockyXmppConnectionPrivate))

static GObject *
wocky_xmpp_connection_constructor (GType type,
    guint n_props, GObjectConstructParam *props)
{
  GObject *obj;
  WockyXmppConnectionPrivate *priv;

  obj = G_OBJECT_CLASS (wocky_xmpp_connection_parent_class)->
        constructor (type, n_props, props);

  priv = WOCKY_XMPP_CONNECTION_GET_PRIVATE (obj);

  priv->writer = wocky_xmpp_writer_new ();
  priv->reader = wocky_xmpp_reader_new ();

  return obj;
}

static void
wocky_xmpp_connection_init (WockyXmppConnection *obj)
{
}

static void wocky_xmpp_connection_dispose (GObject *object);
static void wocky_xmpp_connection_finalize (GObject *object);

static void
wocky_xmpp_connection_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  WockyXmppConnection *connection = WOCKY_XMPP_CONNECTION (object);
  WockyXmppConnectionPrivate *priv =
      WOCKY_XMPP_CONNECTION_GET_PRIVATE (connection);

  switch (property_id)
    {
      case PROP_BASE_STREAM:
        g_assert (priv->stream == NULL);
        priv->stream = g_value_dup_object (value);
        g_assert (priv->stream != NULL);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
wocky_xmpp_connection_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  WockyXmppConnection *connection = WOCKY_XMPP_CONNECTION (object);
  WockyXmppConnectionPrivate *priv =
      WOCKY_XMPP_CONNECTION_GET_PRIVATE (connection);

  switch (property_id)
    {
      case PROP_BASE_STREAM:
        g_value_set_object (value, priv->stream);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
wocky_xmpp_connection_class_init (
    WockyXmppConnectionClass *wocky_xmpp_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (wocky_xmpp_connection_class);
  GParamSpec *spec;

  g_type_class_add_private (wocky_xmpp_connection_class,
      sizeof (WockyXmppConnectionPrivate));

  object_class->set_property = wocky_xmpp_connection_set_property;
  object_class->get_property = wocky_xmpp_connection_get_property;
  object_class->dispose = wocky_xmpp_connection_dispose;
  object_class->finalize = wocky_xmpp_connection_finalize;

  object_class->constructor = wocky_xmpp_connection_constructor;

  spec = g_param_spec_object ("base-stream", "base stream",
    "the stream that the XMPP connection communicates over",
    G_TYPE_IO_STREAM,
    G_PARAM_READWRITE |
    G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_property (object_class, PROP_BASE_STREAM, spec);
}

void
wocky_xmpp_connection_dispose (GObject *object)
{
  WockyXmppConnection *self = WOCKY_XMPP_CONNECTION (object);
  WockyXmppConnectionPrivate *priv =
      WOCKY_XMPP_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
  if (priv->stream != NULL)
    {
      g_object_unref (priv->stream);
      priv->stream = NULL;
    }

  if (priv->reader != NULL)
    {
      g_object_unref (priv->reader);
      priv->reader = NULL;
    }

  if (priv->writer != NULL)
    {
      g_object_unref (priv->writer);
      priv->writer = NULL;
    }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (wocky_xmpp_connection_parent_class)->dispose)
    G_OBJECT_CLASS (wocky_xmpp_connection_parent_class)->dispose (object);
}

void
wocky_xmpp_connection_finalize (GObject *object)
{
  G_OBJECT_CLASS (wocky_xmpp_connection_parent_class)->finalize (object);
}

WockyXmppConnection *
wocky_xmpp_connection_new (GIOStream *stream)
{
  WockyXmppConnection * result;

  result = g_object_new (WOCKY_TYPE_XMPP_CONNECTION,
    "base-stream", stream, NULL);

  return result;
}

void
wocky_xmpp_connection_send_open_async (WockyXmppConnection *connection,
    const gchar *to,
    const gchar *from,
    const gchar *version,
    const gchar *lang,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  /* stub */
}

gboolean
wocky_xmpp_connection_send_open_finish (WockyXmppConnection *connection,
    GAsyncResult *result,
    GError **error)
{
  /* stub */
  return TRUE;
}


static void
wocky_xmpp_connection_do_read (WockyXmppConnection *self)
{
  WockyXmppConnectionPrivate *priv =
      WOCKY_XMPP_CONNECTION_GET_PRIVATE (self);
  GInputStream *input = g_io_stream_get_input_stream (priv->stream);

  g_input_stream_read_async (input,
    priv->buffer, BUFFER_SIZE,
    G_PRIORITY_DEFAULT,
    priv->input_cancellable,
    _xmpp_connection_received_data,
    self);

  g_object_unref (input);
}

static void
_xmpp_connection_received_data (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyXmppConnection *self = WOCKY_XMPP_CONNECTION (user_data);
  WockyXmppConnectionPrivate *priv = WOCKY_XMPP_CONNECTION_GET_PRIVATE (self);
  gssize size;
  GError *error = NULL;

  size = g_input_stream_read_finish (G_INPUT_STREAM (source),
    result, &error);

  if (G_UNLIKELY (size < 0))
    {
      g_simple_async_result_set_from_error (priv->input_result, error);
      g_error_free (error);

      goto finished;
    }

  if (G_UNLIKELY (size == 0))
    {
      g_simple_async_result_set_error (priv->input_result,
        WOCKY_XMPP_CONNECTION_ERROR, WOCKY_XMPP_CONNECTION_ERROR_EOS,
        "Connection got disconnected" );
      goto finished;
    }


  wocky_xmpp_reader_push (priv->reader, priv->buffer, size);

  if (!(priv->stream_flags & WOCKY_XMPP_CONNECTION_STREAM_RECEIVED)
      && (wocky_xmpp_reader_get_state (priv->reader) ==
          WOCKY_XMPP_READER_STATE_OPENED))
    {
      /* stream was opened, can only be as a result of calling recv_open */
      priv->stream_flags |= WOCKY_XMPP_CONNECTION_STREAM_RECEIVED;
      goto finished;
    }

  if (wocky_xmpp_reader_peek_stanza (priv->reader) != NULL)
    goto finished;

  switch (wocky_xmpp_reader_get_state (priv->reader))
    {
      case WOCKY_XMPP_READER_STATE_CLOSED:
      case WOCKY_XMPP_READER_STATE_ERROR:
        goto finished;
      default:
        /* Need more data */
        break;
    }

  wocky_xmpp_connection_do_read (self);

  return;

finished:
  {
    GSimpleAsyncResult *r = priv->input_result;

    priv->input_cancellable = NULL;
    priv->input_result = NULL;

    g_simple_async_result_complete (r);
    g_object_unref (r);
  }
}

void
wocky_xmpp_connection_recv_open_async (WockyXmppConnection *connection,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  WockyXmppConnectionPrivate *priv =
    WOCKY_XMPP_CONNECTION_GET_PRIVATE (connection);

  g_assert (
    (priv->stream_flags &  WOCKY_XMPP_CONNECTION_STREAM_RECEIVED) == 0);
  g_assert (priv->input_result == NULL);
  g_assert (priv->input_cancellable == NULL);

  priv->input_result = g_simple_async_result_new (G_OBJECT (connection),
    callback, user_data, wocky_xmpp_connection_recv_open_finish);

  priv->input_cancellable = cancellable;

  wocky_xmpp_connection_do_read (connection);
}

gboolean
wocky_xmpp_connection_recv_open_finish (WockyXmppConnection *connection,
    GAsyncResult *result,
    const gchar **to,
    const gchar **from,
    const gchar **version,
    const gchar **lang,
    GError **error)
{
  WockyXmppConnectionPrivate *priv;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (connection), wocky_xmpp_connection_recv_open_finish), FALSE);

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
      error))
    return FALSE;

  priv = WOCKY_XMPP_CONNECTION_GET_PRIVATE (connection);

  if (to != NULL)
    g_object_get (priv->reader, "to", to, NULL);

  if (from != NULL)
    g_object_get (priv->reader, "from", from, NULL);

  if (version != NULL)
    g_object_get (priv->reader, "version", version, NULL);

  if (lang != NULL)
    g_object_get (priv->reader, "lang", lang, NULL);

  return TRUE;
}

void
wocky_xmpp_connection_send_stanza_async (WockyXmppConnection *connection,
    WockyXmppStanza *stanza,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  /* stub */
}

gboolean
wocky_xmpp_connection_send_stanza_async_finish (
    WockyXmppConnection *connection,
    GAsyncResult *result,
    GError **error)
{
  /* stub */
  return TRUE;
}

void
wocky_xmpp_connection_recv_stanza_async (WockyXmppConnection *connection,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  WockyXmppConnectionPrivate *priv =
    WOCKY_XMPP_CONNECTION_GET_PRIVATE (connection);

  g_assert (
    (priv->stream_flags &  WOCKY_XMPP_CONNECTION_STREAM_RECEIVED) != 0);
  g_assert (priv->input_result == NULL);
  g_assert (priv->input_cancellable == NULL);

  priv->input_result = g_simple_async_result_new (G_OBJECT (connection),
    callback, user_data, wocky_xmpp_connection_recv_stanza_finish);

  priv->input_cancellable = cancellable;

  wocky_xmpp_connection_do_read (connection);
}

WockyXmppStanza *
wocky_xmpp_connection_recv_stanza_finish (WockyXmppConnection *connection,
    GAsyncResult *result,
    GError **error)
{
  WockyXmppConnectionPrivate *priv;
  WockyXmppStanza *stanza = NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (connection), wocky_xmpp_connection_recv_stanza_finish), NULL);

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
      error))
    return NULL;

  priv = WOCKY_XMPP_CONNECTION_GET_PRIVATE (connection);

  switch (wocky_xmpp_reader_get_state (priv->reader))
    {
      case WOCKY_XMPP_READER_STATE_INITIAL:
        g_assert_not_reached ();
        break;
      case WOCKY_XMPP_READER_STATE_OPENED:
        stanza = wocky_xmpp_reader_pop_stanza (priv->reader);
        break;
      case WOCKY_XMPP_READER_STATE_CLOSED:
        g_set_error_literal (error, WOCKY_XMPP_CONNECTION_ERROR,
          WOCKY_XMPP_CONNECTION_ERROR_CLOSED,
          "Stream closed");
        break;
      case WOCKY_XMPP_READER_STATE_ERROR:
        {
          GError *e;

          e = wocky_xmpp_reader_get_error (priv->reader);

          g_assert (e != NULL);

          g_propagate_error (error, e);
          g_error_free (e);

          break;
        }
    }

  return stanza;
}


void
wocky_xmpp_connection_send_close_async (WockyXmppConnection *connection,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  /* stub */
}

gboolean
wocky_xmpp_connection_send_close_finish (WockyXmppConnection *connection,
    GAsyncResult *result,
    GError **error)
{
  /* stub */
  return TRUE;
}

gchar *
wocky_xmpp_connection_new_id (WockyXmppConnection *self)
{
  WockyXmppConnectionPrivate *priv =
    WOCKY_XMPP_CONNECTION_GET_PRIVATE (self);
  GTimeVal tv;
  glong val;

  g_get_current_time (&tv);
  val = (tv.tv_sec & tv.tv_usec) + priv->last_id++;

  return g_strdup_printf ("%ld%ld", val, tv.tv_usec);
}
