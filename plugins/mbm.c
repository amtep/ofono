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
#include <ofono/stk.h>
#include <ofono/sms.h>
#include <ofono/cbs.h>
#include <ofono/ssn.h>
#include <ofono/ussd.h>
#include <ofono/voicecall.h>
#include <ofono/phonebook.h>
#include <ofono/message-waiting.h>
#include <ofono/call-meter.h>
#include <ofono/call-settings.h>
#include <ofono/call-volume.h>
#include <ofono/call-forwarding.h>
#include <ofono/gprs.h>
#include <ofono/gprs-context.h>
#include <ofono/log.h>
#include <drivers/atmodem/vendor.h>

static const char *cfun_prefix[] = { "+CFUN:", NULL };
static const char *crsm_prefix[] = { "+CRSM:", NULL };
static const char *none_prefix[] = { NULL };

struct mbm_data {
	GAtChat *modem_port;
	GAtChat *data_port;
	gboolean have_sim;
};

static int mbm_probe(struct ofono_modem *modem)
{
	struct mbm_data *data;

	DBG("%p", modem);

	data = g_try_new0(struct mbm_data, 1);
	if (!data)
		return -ENOMEM;

	ofono_modem_set_data(modem, data);

	return 0;
}

static void mbm_remove(struct ofono_modem *modem)
{
	struct mbm_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	ofono_modem_set_data(modem, NULL);

	g_at_chat_unref(data->data_port);
	g_at_chat_unref(data->modem_port);
	g_free(data);
}

static void mbm_debug(const char *str, void *user_data)
{
	const char *prefix = user_data;

	ofono_info("%s %s", prefix, str);
}

static void status_check(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct mbm_data *data = ofono_modem_get_data(modem);
	GAtResultIter iter;
	gint sw[2];

	DBG("");

	if (!ok)
		goto poweron;

	/* Modem fakes a 94 04 response from card (File Id not found /
	 * Pattern not found) when there's no card in the slot.
	 */
	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CRSM:"))
		goto poweron;

	g_at_result_iter_next_number(&iter, &sw[0]);
	g_at_result_iter_next_number(&iter, &sw[1]);

	data->have_sim = sw[0] != 0x94 || sw[1] != 0x04;

poweron:
	ofono_modem_set_powered(modem, TRUE);
}

static void cfun_enable(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct mbm_data *data = ofono_modem_get_data(modem);

	DBG("");

	if (!ok) {
		ofono_modem_set_powered(modem, FALSE);
		return;
	}

	g_at_chat_send(data->modem_port, "AT+CRSM=242", crsm_prefix,
			status_check, modem, NULL);
}

static void cfun_query(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct mbm_data *data = ofono_modem_get_data(modem);
	GAtResultIter iter;
	int status;

	DBG("%d", ok);

	if (!ok)
		return;

	g_at_result_iter_init(&iter, result);

	if (g_at_result_iter_next(&iter, "+CFUN:") == FALSE)
		return;

	g_at_result_iter_next_number(&iter, &status);

	if (status == 4) {
		g_at_chat_send(data->modem_port, "AT+CFUN=1", none_prefix,
				cfun_enable, modem, NULL);
		return;
	}

	cfun_enable(TRUE, NULL, modem);
}

static void emrdy_notifier(GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct mbm_data *data = ofono_modem_get_data(modem);
	GAtResultIter iter;
	int status;

	DBG("");

	g_at_result_iter_init(&iter, result);

	if (g_at_result_iter_next(&iter, "*EMRDY:") == FALSE)
		return;

	g_at_result_iter_next_number(&iter, &status);

	if (status != 1)
		return;

	g_at_chat_send(data->modem_port, "AT+CFUN?", cfun_prefix,
					cfun_query, modem, NULL);
}

static void emrdy_query(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct mbm_data *data = ofono_modem_get_data(modem);

	DBG("%d", ok);

	if (ok)
		return;

	/* On some MBM hardware the EMRDY cannot be queried, so if this fails
	 * we try to run CFUN? to check the state.  CFUN? will fail unless
	 * EMRDY: 1 has been sent, in which case the emrdy_notifier should be
	 * triggered eventually and we send CFUN? again.
	 */
	g_at_chat_send(data->modem_port, "AT+CFUN?", cfun_prefix,
					cfun_query, modem, NULL);
};

static GAtChat *create_port(const char *device)
{
	GAtSyntax *syntax;
	GIOChannel *channel;
	GAtChat *chat;

	channel = g_at_tty_open(device, NULL);
	if (!channel)
		return NULL;

	syntax = g_at_syntax_new_gsmv1();
	chat = g_at_chat_new(channel, syntax);
	g_at_syntax_unref(syntax);
	g_io_channel_unref(channel);

	if (!chat)
		return NULL;

	return chat;
}

static int mbm_enable(struct ofono_modem *modem)
{
	struct mbm_data *data = ofono_modem_get_data(modem);
	const char *modem_dev;
	const char *data_dev;

	DBG("%p", modem);

	modem_dev = ofono_modem_get_string(modem, "ModemDevice");
	data_dev = ofono_modem_get_string(modem, "DataDevice");

	DBG("%s, %s", modem_dev, data_dev);

	if (modem_dev == NULL || data_dev == NULL)
		return -EINVAL;

	data->modem_port = create_port(modem_dev);

	if (data->modem_port == NULL)
		return -EIO;

	if (getenv("OFONO_AT_DEBUG"))
		g_at_chat_set_debug(data->modem_port, mbm_debug, "Modem:");

	data->data_port = create_port(data_dev);

	if (data->data_port == NULL) {
		g_at_chat_unref(data->modem_port);
		data->modem_port = NULL;

		return -EIO;
	}

	if (getenv("OFONO_AT_DEBUG"))
		g_at_chat_set_debug(data->data_port, mbm_debug, "Data:");

	g_at_chat_register(data->modem_port, "*EMRDY:", emrdy_notifier,
					FALSE, modem, NULL);

	g_at_chat_send(data->modem_port, "AT&F E0 V1 X4 &C1 +CMEE=1", NULL,
					NULL, NULL, NULL);
	g_at_chat_send(data->modem_port, "AT*EMRDY?", none_prefix,
				emrdy_query, modem, NULL);

	return -EINPROGRESS;
}

static void cfun_disable(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct mbm_data *data = ofono_modem_get_data(modem);

	DBG("");

	g_at_chat_unref(data->modem_port);
	data->modem_port = NULL;

	g_at_chat_unref(data->data_port);
	data->data_port = NULL;

	if (ok)
		ofono_modem_set_powered(modem, FALSE);
}

static int mbm_disable(struct ofono_modem *modem)
{
	struct mbm_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	if (!data->modem_port)
		return 0;

	g_at_chat_cancel_all(data->modem_port);
	g_at_chat_unregister_all(data->modem_port);
	g_at_chat_send(data->modem_port, "AT+CFUN=4", NULL,
					cfun_disable, modem, NULL);

	return -EINPROGRESS;
}

static void mbm_pre_sim(struct ofono_modem *modem)
{
	struct mbm_data *data = ofono_modem_get_data(modem);
	struct ofono_sim *sim;

	DBG("%p", modem);

	ofono_devinfo_create(modem, 0, "atmodem", data->modem_port);
	sim = ofono_sim_create(modem, 0, "atmodem", data->modem_port);
	ofono_voicecall_create(modem, 0, "atmodem", data->modem_port);
	ofono_stk_create(modem, 0, "mbmmodem", data->modem_port);

	if (data->have_sim && sim)
		ofono_sim_inserted_notify(sim, TRUE);
}

static void mbm_post_sim(struct ofono_modem *modem)
{
	struct mbm_data *data = ofono_modem_get_data(modem);
	struct ofono_message_waiting *mw;
	struct ofono_gprs *gprs;
	struct ofono_gprs_context *gc;

	DBG("%p", modem);

	ofono_call_forwarding_create(modem, 0, "atmodem", data->modem_port);
	ofono_call_settings_create(modem, 0, "atmodem", data->modem_port);
	ofono_call_meter_create(modem, 0, "atmodem", data->modem_port);
	ofono_call_volume_create(modem, 0, "atmodem", data->modem_port);

	ofono_ussd_create(modem, 0, "atmodem", data->modem_port);
	ofono_netreg_create(modem, OFONO_VENDOR_MBM, "atmodem",
				data->modem_port);
	ofono_phonebook_create(modem, 0, "atmodem", data->modem_port);
	ofono_ssn_create(modem, 0, "atmodem", data->modem_port);
	ofono_sms_create(modem, 0, "atmodem", data->modem_port);
	ofono_cbs_create(modem, 0, "atmodem", data->modem_port);

	gprs = ofono_gprs_create(modem, 0, "atmodem", data->modem_port);
	gc = ofono_gprs_context_create(modem, 0, "mbm", data->modem_port);

	if (gprs && gc)
		ofono_gprs_add_context(gprs, gc);

	mw = ofono_message_waiting_create(modem);
	if (mw)
		ofono_message_waiting_register(mw);
}

static struct ofono_modem_driver mbm_driver = {
	.name		= "mbm",
	.probe		= mbm_probe,
	.remove		= mbm_remove,
	.enable		= mbm_enable,
	.disable	= mbm_disable,
	.pre_sim	= mbm_pre_sim,
	.post_sim	= mbm_post_sim,
};

static int mbm_init(void)
{
	return ofono_modem_driver_register(&mbm_driver);
}

static void mbm_exit(void)
{
	ofono_modem_driver_unregister(&mbm_driver);
}

OFONO_PLUGIN_DEFINE(mbm, "Ericsson MBM modem driver", VERSION,
			OFONO_PLUGIN_PRIORITY_DEFAULT, mbm_init, mbm_exit)
