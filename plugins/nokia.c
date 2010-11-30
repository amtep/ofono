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

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include <glib.h>
#include <gatchat.h>
#include <gattty.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/modem.h>
#include <ofono/devinfo.h>
#include <ofono/netreg.h>
#include <ofono/sim.h>
#include <ofono/sms.h>
#include <ofono/ussd.h>
#include <ofono/gprs.h>
#include <ofono/gprs-context.h>
#include <ofono/phonebook.h>
#include <ofono/log.h>

#include <drivers/atmodem/vendor.h>

static const char *none_prefix[] = { NULL };

struct nokia_data {
	GAtChat *modem;
	GAtChat *control;
	struct ofono_gprs *gprs;
	struct ofono_gprs_context *gc;
};

static int nokia_probe(struct ofono_modem *modem)
{
	struct nokia_data *data;

	DBG("%p", modem);

	data = g_try_new0(struct nokia_data, 1);
	if (!data)
		return -ENOMEM;

	ofono_modem_set_data(modem, data);

	return 0;
}

static void nokia_remove(struct ofono_modem *modem)
{
	struct nokia_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	ofono_modem_set_data(modem, NULL);

	g_at_chat_unref(data->modem);
	g_at_chat_unref(data->control);

	g_free(data);
}

static void nokia_debug(const char *str, void *user_data)
{
        const char *prefix = user_data;

        ofono_info("%s%s", prefix, str);
}

static GAtChat *open_device(struct ofono_modem *modem,
				const char *key, char *debug)
{
	const char *device;
	GAtSyntax *syntax;
	GIOChannel *channel;
	GAtChat *chat;

	device = ofono_modem_get_string(modem, key);
	if (device == NULL)
		return NULL;

	DBG("%s %s", key, device);

	channel = g_at_tty_open(device, NULL);
	if (!channel)
		return NULL;

	syntax = g_at_syntax_new_gsm_permissive();
	chat = g_at_chat_new(channel, syntax);
	g_at_syntax_unref(syntax);
	g_io_channel_unref(channel);

	if (!chat)
		return NULL;

	if (getenv("OFONO_AT_DEBUG"))
		g_at_chat_set_debug(chat, nokia_debug, debug);

	return chat;
}

static void nokia_disconnect(gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct nokia_data *data = ofono_modem_get_data(modem);

	DBG("");

	if (data->gc)
		ofono_gprs_context_remove(data->gc);

	g_at_chat_unref(data->modem);
	data->modem = NULL;

	data->modem = open_device(modem, "Modem", "Modem: ");
	if (!data->modem)
		return;

	g_at_chat_set_disconnect_function(data->modem,
						nokia_disconnect, modem);

	ofono_info("Reopened GPRS context channel");

	data->gc = ofono_gprs_context_create(modem, 0, "atmodem", data->modem);

	if (data->gprs && data->gc)
		ofono_gprs_add_context(data->gprs, data->gc);
}

static void cfun_enable(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;

	DBG("");

	ofono_modem_set_powered(modem, ok);
}

static int nokia_enable(struct ofono_modem *modem)
{
	struct nokia_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	data->modem = open_device(modem, "Modem", "Modem: ");
	if (data->modem == NULL)
		return -EINVAL;

	g_at_chat_set_disconnect_function(data->modem,
						nokia_disconnect, modem);

	data->control = open_device(modem, "Control", "Control: ");
	if (data->control == NULL) {
		g_at_chat_unref(data->modem);
		data->modem = NULL;
		return -EIO;
	}

	g_at_chat_send(data->control, "ATE0 +CMEE=1", none_prefix,
						NULL, NULL, NULL);

	g_at_chat_send(data->control, "AT+CFUN=1", none_prefix,
					cfun_enable, modem, NULL);

	return -EINPROGRESS;
}

static void cfun_disable(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct nokia_data *data = ofono_modem_get_data(modem);

	DBG("");

	g_at_chat_unref(data->control);
	data->control = NULL;

	if (ok)
		ofono_modem_set_powered(modem, FALSE);
}

static int nokia_disable(struct ofono_modem *modem)
{
	struct nokia_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	if (data->modem) {
		g_at_chat_cancel_all(data->modem);
		g_at_chat_unregister_all(data->modem);
		g_at_chat_unref(data->modem);
		data->modem = NULL;
	}

	if (!data->control)
		return 0;

	g_at_chat_cancel_all(data->control);
	g_at_chat_unregister_all(data->control);
	g_at_chat_send(data->control, "AT+CFUN=4", none_prefix,
					cfun_disable, modem, NULL);

	return -EINPROGRESS;
}

static void nokia_pre_sim(struct ofono_modem *modem)
{
	struct nokia_data *data = ofono_modem_get_data(modem);
	struct ofono_sim *sim;

	DBG("%p", modem);

	ofono_devinfo_create(modem, 0, "atmodem", data->control);
	sim = ofono_sim_create(modem, 0, "atmodem", data->control);

	if (sim)
		ofono_sim_inserted_notify(sim, TRUE);
}

static void nokia_post_sim(struct ofono_modem *modem)
{
	struct nokia_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	ofono_netreg_create(modem, OFONO_VENDOR_NOKIA,
					"atmodem", data->control);

	ofono_sms_create(modem, OFONO_VENDOR_OPTION_HSO,
					"atmodem", data->control);
	ofono_ussd_create(modem, OFONO_VENDOR_QUALCOMM_MSM,
					"atmodem", data->control);
	ofono_phonebook_create(modem, 0, "atmodem", data->control);

	data->gprs = ofono_gprs_create(modem, OFONO_VENDOR_NOKIA,
					"atmodem", data->control);

	data->gc = ofono_gprs_context_create(modem, 0, "atmodem", data->modem);

	if (data->gprs && data->gc)
		ofono_gprs_add_context(data->gprs, data->gc);
}

static struct ofono_modem_driver nokia_driver = {
	.name		= "nokia",
	.probe		= nokia_probe,
	.remove		= nokia_remove,
	.enable		= nokia_enable,
	.disable	= nokia_disable,
	.pre_sim	= nokia_pre_sim,
	.post_sim	= nokia_post_sim,
};

static int nokia_init(void)
{
	return ofono_modem_driver_register(&nokia_driver);
}

static void nokia_exit(void)
{
	ofono_modem_driver_unregister(&nokia_driver);
}

OFONO_PLUGIN_DEFINE(nokia, "Nokia Datacard modem driver", VERSION,
		OFONO_PLUGIN_PRIORITY_DEFAULT, nokia_init, nokia_exit)
