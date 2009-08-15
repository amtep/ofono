/*
 *
 *  AT chat library with GLib integration
 *
 *  Copyright (C) 2008-2009  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __GATCHAT_H
#define __GATCHAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gatresult.h"
#include "gatsyntax.h"

struct _GAtChat;

typedef struct _GAtChat GAtChat;

typedef void (*GAtResultFunc)(gboolean success, GAtResult *result,
				gpointer user_data);
typedef void (*GAtNotifyFunc)(GAtResult *result, gpointer user_data);
typedef void (*GAtDisconnectFunc)(gpointer user_data);
typedef void (*GAtDebugFunc)(const char *str, gpointer user_data);

GAtChat *g_at_chat_new(GIOChannel *channel, GAtSyntax *syntax);
GAtChat *g_at_chat_new_from_tty(const char *device, GAtSyntax *syntax);

GAtChat *g_at_chat_ref(GAtChat *chat);
void g_at_chat_unref(GAtChat *chat);

gboolean g_at_chat_shutdown(GAtChat *chat);

gboolean g_at_chat_set_disconnect_function(GAtChat *chat,
			GAtDisconnectFunc disconnect, gpointer user_data);

/*!
 * If the function is not NULL, then on every read/write from the GIOChannel
 * provided to GAtChat the logging function will be called with the
 * input/output string and user data
 */
gboolean g_at_chat_set_debug(GAtChat *chat, GAtDebugFunc func, gpointer user);

/*!
 * Queue an AT command for execution.  The command contents are given
 * in cmd.  Once the command executes, the callback function given by
 * func is called with user provided data in user_data.
 *
 * Returns an id of the queued command which can be canceled using
 * g_at_chat_cancel.  If an error occurred, an id of 0 is returned.
 *
 * This function can be used in three ways:
 * 	- Send a simple command such as g_at_chat_send(p, "AT+CGMI?", ...
 *
 * 	- Send a compound command: g_at_chat_send(p, "AT+CMD1;+CMD2", ...
 *
 * 	- Send a command requiring a prompt.  The command up to '\r' is sent
 * 	  after which time a '> ' prompt is expected from the modem.  Further
 * 	  contents of the command are sent until a '\r' or end of string is
 * 	  encountered.  If end of string is encountered, the Ctrl-Z character
 * 	  is sent automatically.  There is no need to include the Ctrl-Z
 * 	  by the caller.
 *
 * The valid_resp field can be used to send an array of strings which will
 * be accepted as a valid response for this command.  This is treated as a
 * simple prefix match.  If a response line comes in from the modem and it
 * does not match any of the prefixes in valid_resp, it is treated as an
 * unsolicited notification.  If valid_resp is NULL, then all response
 * lines after command submission and final response line are treated as
 * part of the command response.  This can be used to get around broken
 * modems which send unsolicited notifications during command processing.
 */
guint g_at_chat_send(GAtChat *chat, const char *cmd,
				const char **valid_resp, GAtResultFunc func,
				gpointer user_data, GDestroyNotify notify);

/*!
 * Same as the above command, except that the caller wishes to receive the
 * intermediate responses immediately through the GAtNotifyFunc callback.
 * The final response will still be sent to GAtResultFunc callback.  The
 * final GAtResult will not contain any lines from the intermediate responses.
 * This is useful for listing commands such as CMGL or CPBR.
 */
guint g_at_chat_send_listing(GAtChat *chat, const char *cmd,
				const char **valid_resp,
				GAtNotifyFunc listing, GAtResultFunc func,
				gpointer user_data, GDestroyNotify notify);

gboolean g_at_chat_cancel(GAtChat *chat, guint id);

guint g_at_chat_register(GAtChat *chat, const char *prefix,
				GAtNotifyFunc func, gboolean expect_pdu,
				gpointer user_data, GDestroyNotify notify);

gboolean g_at_chat_unregister(GAtChat *chat, guint id);

gboolean g_at_chat_set_wakeup_command(GAtChat *chat, const char *cmd,
					guint timeout, guint msec);


#ifdef __cplusplus
}
#endif

#endif /* __GATCHAT_H */
