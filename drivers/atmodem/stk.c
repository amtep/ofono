/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2010  Intel Corporation. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <glib.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/stk.h>

#include "gatchat.h"
#include "gatresult.h"

#include "atmodem.h"

struct stk_data {
	GAtChat *chat;
};

static const char *csim_prefix[] = { "+CSIM:", NULL };

static void at_csim_envelope_cb(gboolean ok, GAtResult *result,
				gpointer user_data)
{
	struct cb_data *cbd = user_data;
	GAtResultIter iter;
	ofono_stk_envelope_cb_t cb = cbd->cb;
	struct ofono_error error;
	const guint8 *response;
	gint rlen, len;

	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok)
		goto error;

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CSIM:"))
		goto error;

	if (!g_at_result_iter_next_number(&iter, &rlen))
		goto error;

	if (!g_at_result_iter_next_hexstring(&iter, &response, &len))
		goto error;

	if (rlen != len * 2 || len < 2)
		goto error;

	if (response[len - 2] != 0x90 && response[len - 2] != 0x91)
		goto error;

	if (response[len - 2] == 0x90 && response[len - 1] != 0)
		goto error;

	DBG("csim_envelope_cb: %i", len);

	cb(&error, response, len - 2, cbd->data);
	return;

error:
	CALLBACK_WITH_FAILURE(cb, NULL, 0, cbd->data);
}

static void at_stk_envelope(struct ofono_stk *stk, int length,
				const unsigned char *command,
				ofono_stk_envelope_cb_t cb, void *data)
{
	struct stk_data *sd = ofono_stk_get_data(stk);
	struct cb_data *cbd = cb_data_new(cb, data);
	char *buf = g_try_new(char, 64 + length * 2);
	int len, ret;

	if (!cbd || !buf)
		goto error;

	len = sprintf(buf, "AT+CSIM=%i,A0C20000%02hhX",
			12 + length * 2, length);

	for (; length; length--)
		len += sprintf(buf + len, "%02hhX", *command++);

	len += sprintf(buf + len, "FF");

	ret = g_at_chat_send(sd->chat, buf, csim_prefix,
				at_csim_envelope_cb, cbd, g_free);

	g_free(buf);
	buf = NULL;

	if (ret > 0)
		return;

error:
	if (buf)
		g_free(buf);

	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, NULL, 0, data);
}

static void at_csim_terminal_response_cb(gboolean ok, GAtResult *result,
						gpointer user_data)
{
	struct cb_data *cbd = user_data;
	GAtResultIter iter;
	ofono_stk_generic_cb_t cb = cbd->cb;
	struct ofono_error error;
	const guint8 *response;
	gint rlen, len;

	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok)
		goto error;

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CSIM:"))
		goto error;

	if (!g_at_result_iter_next_number(&iter, &rlen))
		goto error;

	if (!g_at_result_iter_next_hexstring(&iter, &response, &len))
		goto error;

	if (rlen != len * 2 || len < 2)
		goto error;

	if (response[len - 2] != 0x90 && response[len - 2] != 0x91)
		goto error;

	if (response[len - 2] == 0x90 && response[len - 1] != 0)
		goto error;

	DBG("csim_terminal_response_cb: %i", len);

	cb(&error, cbd->data);
	return;

error:
	CALLBACK_WITH_FAILURE(cb, cbd->data);
}

static void at_stk_terminal_response(struct ofono_stk *stk, int length,
					const unsigned char *value,
					ofono_stk_generic_cb_t cb,
					void *data)
{
	struct stk_data *sd = ofono_stk_get_data(stk);
	struct cb_data *cbd = cb_data_new(cb, data);
	char *buf = g_try_new(char, 64 + length * 2);
	int len, ret;

	if (!cbd || !buf)
		goto error;

	len = sprintf(buf, "AT+CSIM=%i,A0140000%02hhX",
			10 + length * 2, length);

	for (; length; length--)
		len += sprintf(buf + len, "%02hhX", *value++);

	ret = g_at_chat_send(sd->chat, buf, csim_prefix,
				at_csim_terminal_response_cb, cbd, g_free);

	g_free(buf);
	buf = NULL;

	if (ret > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, data);
}

static gboolean at_stk_register(gpointer user)
{
	struct ofono_stk *stk = user;

	ofono_stk_register(stk);

	return FALSE;
}

static int at_stk_probe(struct ofono_stk *stk, unsigned int vendor, void *data)
{
	GAtChat *chat = data;
	struct stk_data *sd;

	sd = g_new0(struct stk_data, 1);
	sd->chat = chat;

	ofono_stk_set_data(stk, sd);
	g_idle_add(at_stk_register, stk);

	return 0;
}

static void at_stk_remove(struct ofono_stk *stk)
{
	struct stk_data *sd = ofono_stk_get_data(stk);

	ofono_stk_set_data(stk, NULL);

	g_free(sd);
}

static struct ofono_stk_driver driver = {
	.name			= "atmodem",
	.probe			= at_stk_probe,
	.remove			= at_stk_remove,
	.envelope		= at_stk_envelope,
	.terminal_response	= at_stk_terminal_response,
};

void at_stk_init()
{
	ofono_stk_driver_register(&driver);
}

void at_stk_exit()
{
	ofono_stk_driver_unregister(&driver);
}
