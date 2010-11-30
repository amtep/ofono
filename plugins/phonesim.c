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

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>

#include <glib.h>
#include <gatmux.h>
#include <gatchat.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/call-barring.h>
#include <ofono/call-forwarding.h>
#include <ofono/call-meter.h>
#include <ofono/call-settings.h>
#include <ofono/call-volume.h>
#include <ofono/cbs.h>
#include <ofono/devinfo.h>
#include <ofono/message-waiting.h>
#include <ofono/netreg.h>
#include <ofono/phonebook.h>
#include <ofono/sim.h>
#include <ofono/stk.h>
#include <ofono/sms.h>
#include <ofono/ssn.h>
#include <ofono/ussd.h>
#include <ofono/voicecall.h>
#include <ofono/gprs.h>
#include <ofono/gprs-context.h>

#include <drivers/atmodem/vendor.h>
#include <drivers/atmodem/sim-poll.h>
#include <drivers/atmodem/atutil.h>

static const char *none_prefix[] = { NULL };
static int next_iface = 0;

struct phonesim_data {
	GAtMux *mux;
	GAtChat *chat;
	gboolean calypso;
	gboolean use_mux;
};

struct gprs_context_data {
	GAtChat *chat;
	char *interface;
};

static void at_cgact_up_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_gprs_context_up_cb_t cb = cbd->cb;
	struct ofono_gprs_context *gc = cbd->user;
	struct gprs_context_data *gcd = ofono_gprs_context_get_data(gc);
	struct ofono_error error;

	decode_at_error(&error, g_at_result_final_response(result));
	cb(&error, ok ? gcd->interface : NULL, FALSE,
			NULL, NULL, NULL, NULL, cbd->data);
}

static void at_cgact_down_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_gprs_context_cb_t cb = cbd->cb;
	struct ofono_error error;

	decode_at_error(&error, g_at_result_final_response(result));
	cb(&error, cbd->data);
}

static void phonesim_activate_primary(struct ofono_gprs_context *gc,
				const struct ofono_gprs_primary_context *ctx,
				ofono_gprs_context_up_cb_t cb, void *data)
{
	struct gprs_context_data *gcd = ofono_gprs_context_get_data(gc);
	struct cb_data *cbd = cb_data_new(cb, data);
	char buf[OFONO_GPRS_MAX_APN_LENGTH + 128];
	int len;

	cbd->user = gc;

	len = snprintf(buf, sizeof(buf), "AT+CGDCONT=%u,\"IP\"", ctx->cid);

	if (ctx->apn)
		snprintf(buf + len, sizeof(buf) - len - 3, ",\"%s\"",
				ctx->apn);

	/* Assume always succeeds */
	if (g_at_chat_send(gcd->chat, buf, none_prefix, NULL, NULL, NULL) == 0)
		goto error;

	sprintf(buf, "AT+CGACT=1,%u", ctx->cid);
	if (g_at_chat_send(gcd->chat, buf, none_prefix,
				at_cgact_up_cb, cbd, g_free) > 0)
		return;

error:
	g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, NULL, 0, NULL, NULL, NULL, NULL, data);
}

static void phonesim_deactivate_primary(struct ofono_gprs_context *gc,
					unsigned int id,
					ofono_gprs_context_cb_t cb, void *data)
{
	struct gprs_context_data *gcd = ofono_gprs_context_get_data(gc);
	struct cb_data *cbd = cb_data_new(cb, data);
	char buf[128];

	if (!cbd)
		goto error;

	cbd->user = gc;

	snprintf(buf, sizeof(buf), "AT+CGACT=0,%u", id);

	if (g_at_chat_send(gcd->chat, buf, none_prefix,
				at_cgact_down_cb, cbd, g_free) > 0)
		return;

error:
	g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, data);
}

static int phonesim_context_probe(struct ofono_gprs_context *gc,
					unsigned int vendor, void *data)
{
	GAtChat *chat = data;
	struct gprs_context_data *gcd;

	gcd = g_try_new0(struct gprs_context_data, 1);
	if (!gcd)
		return -ENOMEM;

	gcd->chat = g_at_chat_clone(chat);
	gcd->interface = g_strdup_printf("dummy%d", next_iface++);

	ofono_gprs_context_set_data(gc, gcd);

	return 0;
}

static void phonesim_context_remove(struct ofono_gprs_context *gc)
{
	struct gprs_context_data *gcd = ofono_gprs_context_get_data(gc);

	DBG("");

	ofono_gprs_context_set_data(gc, NULL);

	g_at_chat_unref(gcd->chat);
	g_free(gcd->interface);

	g_free(gcd);
}

static struct ofono_gprs_context_driver context_driver = {
	.name			= "phonesim",
	.probe			= phonesim_context_probe,
	.remove			= phonesim_context_remove,
	.activate_primary	= phonesim_activate_primary,
	.deactivate_primary	= phonesim_deactivate_primary,
};

static int phonesim_probe(struct ofono_modem *modem)
{
	struct phonesim_data *data;

	DBG("%p", modem);

	data = g_try_new0(struct phonesim_data, 1);
	if (!data)
		return -ENOMEM;

	ofono_modem_set_data(modem, data);

	return 0;
}

static void phonesim_remove(struct ofono_modem *modem)
{
	struct phonesim_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	g_free(data);
	ofono_modem_set_data(modem, NULL);
}

static void phonesim_debug(const char *str, void *user_data)
{
	ofono_info("%s", str);
}

static void cfun_set_on_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;

	DBG("");

	ofono_modem_set_powered(modem, ok);
}

static void phonesim_disconnected(gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct phonesim_data *data = ofono_modem_get_data(modem);

	DBG("");

	ofono_modem_set_powered(modem, FALSE);

	g_at_chat_unref(data->chat);
	data->chat = NULL;

	if (data->mux) {
		g_at_mux_shutdown(data->mux);
		g_at_mux_unref(data->mux);
		data->mux = NULL;
	}
}

static void mux_setup(GAtMux *mux, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct phonesim_data *data = ofono_modem_get_data(modem);
	GIOChannel *io;
	GAtSyntax *syntax;

	DBG("%p", mux);

	if (!mux) {
		ofono_modem_set_powered(modem, FALSE);
		return;
	}

	data->mux = mux;

	if (getenv("OFONO_AT_DEBUG"))
		g_at_mux_set_debug(data->mux, phonesim_debug, NULL);

	g_at_mux_start(mux);
	io = g_at_mux_create_channel(mux);

	if (data->calypso)
		syntax = g_at_syntax_new_gsm_permissive();
	else
		syntax = g_at_syntax_new_gsmv1();

	data->chat = g_at_chat_new(io, syntax);
	g_at_syntax_unref(syntax);
	g_io_channel_unref(io);

	if (getenv("OFONO_AT_DEBUG"))
		g_at_chat_set_debug(data->chat, phonesim_debug, NULL);

	if (data->calypso)
		g_at_chat_set_wakeup_command(data->chat, "AT\r", 500, 5000);

	g_at_chat_send(data->chat, "ATE0", NULL, NULL, NULL, NULL);

	g_at_chat_send(data->chat, "AT+CFUN=1", none_prefix,
					cfun_set_on_cb, modem, NULL);
}

static int phonesim_enable(struct ofono_modem *modem)
{
	struct phonesim_data *data = ofono_modem_get_data(modem);
	GIOChannel *io;
	GAtSyntax *syntax;
	struct sockaddr_in addr;
	const char *address, *value;
	int sk, err, port;

	DBG("%p", modem);

	address = ofono_modem_get_string(modem, "Address");
	if (!address)
		return -EINVAL;

	port = ofono_modem_get_integer(modem, "Port");
	if (port < 0)
		return -EINVAL;

	value = ofono_modem_get_string(modem, "Modem");
	if (!g_strcmp0(value, "calypso"))
		data->calypso = TRUE;

	value = ofono_modem_get_string(modem, "Multiplexer");
	if (!g_strcmp0(value, "internal"))
		data->use_mux = TRUE;

	sk = socket(PF_INET, SOCK_STREAM, 0);
	if (sk < 0)
		return -EINVAL;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(address);
	addr.sin_port = htons(port);

	err = connect(sk, (struct sockaddr *) &addr, sizeof(addr));
	if (err < 0) {
		close(sk);
		return err;
	}

	io = g_io_channel_unix_new(sk);
	if (!io) {
		close(sk);
		return -ENOMEM;
	}

	if (data->calypso)
		syntax = g_at_syntax_new_gsm_permissive();
	else
		syntax = g_at_syntax_new_gsmv1();

	data->chat = g_at_chat_new(io, syntax);

	g_at_syntax_unref(syntax);
	g_io_channel_unref(io);

	if (!data->chat)
		return -ENOMEM;

	if (getenv("OFONO_AT_DEBUG"))
		g_at_chat_set_debug(data->chat, phonesim_debug, NULL);

	g_at_chat_set_disconnect_function(data->chat,
						phonesim_disconnected, modem);

	if (data->calypso) {
		g_at_chat_set_wakeup_command(data->chat, "AT\r", 500, 5000);

		g_at_chat_send(data->chat, "ATE0", NULL, NULL, NULL, NULL);

		g_at_chat_send(data->chat, "AT%CUNS=0",
				NULL, NULL, NULL, NULL);
	}

	if (data->use_mux) {
		g_at_chat_send(data->chat, "ATE0", NULL, NULL, NULL, NULL);

		g_at_mux_setup_gsm0710(data->chat, mux_setup, modem, NULL);

		g_at_chat_unref(data->chat);
		data->chat = NULL;

		return -EINPROGRESS;
	}

	g_at_chat_send(data->chat, "AT+CSCS=\"GSM\"", none_prefix,
			NULL, NULL, NULL);

	return 0;
}

static void set_online_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_modem_online_cb_t callback = cbd->cb;
	struct ofono_error error;

	decode_at_error(&error, g_at_result_final_response(result));

	callback(&error, cbd->data);
}

static void phonesim_set_online(struct ofono_modem *modem, ofono_bool_t online,
				ofono_modem_online_cb_t cb, void *user_data)
{
	struct phonesim_data *data = ofono_modem_get_data(modem);
	struct cb_data *cbd = cb_data_new(cb, user_data);
	char buf[64];

	DBG("%p", modem);

	snprintf(buf, sizeof(buf), "AT+CFUN=%d", online ? 1 : 4);

	if (g_at_chat_send(data->chat, buf, none_prefix,
				set_online_cb, cbd, g_free) > 0)
		return;

	CALLBACK_WITH_FAILURE(cb, user_data);
}

static int phonesim_disable(struct ofono_modem *modem)
{
	struct phonesim_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	g_at_chat_unref(data->chat);
	data->chat = NULL;

	if (data->mux) {
		g_at_mux_shutdown(data->mux);

		g_at_mux_unref(data->mux);
		data->mux = NULL;
	}

	return 0;
}

static void phonesim_pre_sim(struct ofono_modem *modem)
{
	struct phonesim_data *data = ofono_modem_get_data(modem);
	struct ofono_sim *sim;

	DBG("%p", modem);

	ofono_devinfo_create(modem, 0, "atmodem", data->chat);
	sim = ofono_sim_create(modem, 0, "atmodem", data->chat);

	if (data->calypso)
		ofono_voicecall_create(modem, 0, "calypsomodem", data->chat);
	else
		ofono_voicecall_create(modem, 0, "atmodem", data->chat);

	if (sim)
		ofono_sim_inserted_notify(sim, TRUE);
}

static void phonesim_post_sim(struct ofono_modem *modem)
{
	struct phonesim_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	ofono_phonebook_create(modem, 0, "atmodem", data->chat);

	if (!data->calypso)
		ofono_stk_create(modem, OFONO_VENDOR_PHONESIM,
					"atmodem", data->chat);
}

static void phonesim_post_online(struct ofono_modem *modem)
{
	struct phonesim_data *data = ofono_modem_get_data(modem);
	struct ofono_message_waiting *mw;
	struct ofono_gprs *gprs;
	struct ofono_gprs_context *gc1, *gc2;

	DBG("%p", modem);

	ofono_ussd_create(modem, 0, "atmodem", data->chat);
	ofono_call_forwarding_create(modem, 0, "atmodem", data->chat);
	ofono_call_settings_create(modem, 0, "atmodem", data->chat);

	if (data->calypso)
		ofono_netreg_create(modem, OFONO_VENDOR_CALYPSO,
							"atmodem", data->chat);
	else
		ofono_netreg_create(modem, OFONO_VENDOR_PHONESIM,
							"atmodem", data->chat);

	ofono_call_meter_create(modem, 0, "atmodem", data->chat);
	ofono_call_barring_create(modem, 0, "atmodem", data->chat);
	ofono_ssn_create(modem, 0, "atmodem", data->chat);
	ofono_call_volume_create(modem, 0, "atmodem", data->chat);

	if (!data->calypso) {
		ofono_sms_create(modem, 0, "atmodem", data->chat);
		ofono_cbs_create(modem, 0, "atmodem", data->chat);
	}

	gprs = ofono_gprs_create(modem, 0, "atmodem", data->chat);

	gc1 = ofono_gprs_context_create(modem, 0, "phonesim", data->chat);
	if (gprs && gc1)
		ofono_gprs_add_context(gprs, gc1);

	gc2 = ofono_gprs_context_create(modem, 0, "phonesim", data->chat);
	if (gprs && gc2)
		ofono_gprs_add_context(gprs, gc2);

	mw = ofono_message_waiting_create(modem);
	if (mw)
		ofono_message_waiting_register(mw);

}

static struct ofono_modem_driver phonesim_driver = {
	.name		= "phonesim",
	.probe		= phonesim_probe,
	.remove		= phonesim_remove,
	.enable		= phonesim_enable,
	.disable	= phonesim_disable,
	.set_online	= phonesim_set_online,
	.pre_sim	= phonesim_pre_sim,
	.post_sim	= phonesim_post_sim,
	.post_online	= phonesim_post_online,
};

static struct ofono_modem *create_modem(GKeyFile *keyfile, const char *group)
{
	struct ofono_modem *modem;
	char *value;

	DBG("group %s", group);

	modem = ofono_modem_create(group, "phonesim");
	if (!modem)
		return NULL;

	value = g_key_file_get_string(keyfile, group, "Address", NULL);
	if (!value)
		goto error;

	ofono_modem_set_string(modem, "Address", value);
	g_free(value);

	value = g_key_file_get_string(keyfile, group, "Port", NULL);
	if (!value)
		goto error;

	ofono_modem_set_integer(modem, "Port", atoi(value));
	g_free(value);

	value = g_key_file_get_string(keyfile, group, "Modem", NULL);
	if (value) {
		ofono_modem_set_string(modem, "Modem", value);
		g_free(value);
	}

	value = g_key_file_get_string(keyfile, group, "Multiplexer", NULL);
	if (value) {
		ofono_modem_set_string(modem, "Multiplexer", value);
		g_free(value);
	}

	DBG("%p", modem);

	return modem;

error:
	ofono_error("Missing address or port setting for %s", group);

	ofono_modem_remove(modem);

	return NULL;
}

static GSList *modem_list = NULL;

static void parse_config(const char *filename)
{
	GKeyFile *keyfile;
	GError *err = NULL;
	char **modems;
	int i;

	DBG("filename %s", filename);

	keyfile = g_key_file_new();

	g_key_file_set_list_separator(keyfile, ',');

	if (!g_key_file_load_from_file(keyfile, filename, 0, &err)) {
		ofono_warn("Reading of %s failed: %s", filename, err->message);
		g_error_free(err);
		goto done;
	}

	modems = g_key_file_get_groups(keyfile, NULL);

	for (i = 0; modems[i]; i++) {
		struct ofono_modem *modem;

		modem = create_modem(keyfile, modems[i]);
		if (!modem)
			continue;

		modem_list = g_slist_prepend(modem_list, modem);

		ofono_modem_register(modem);
	}

	g_strfreev(modems);

done:
	g_key_file_free(keyfile);
}

static int phonesim_init(void)
{
	int err;

	err = ofono_modem_driver_register(&phonesim_driver);
	if (err < 0)
		return err;

	ofono_gprs_context_driver_register(&context_driver);

	parse_config(CONFIGDIR "/phonesim.conf");

	return 0;
}

static void phonesim_exit(void)
{
	GSList *list;

	for (list = modem_list; list; list = list->next) {
		struct ofono_modem *modem = list->data;

		ofono_modem_remove(modem);
	}

	g_slist_free(modem_list);
	modem_list = NULL;

	ofono_gprs_context_driver_unregister(&context_driver);

	ofono_modem_driver_unregister(&phonesim_driver);
}

OFONO_PLUGIN_DEFINE(phonesim, "Phone Simulator driver", VERSION,
		OFONO_PLUGIN_PRIORITY_DEFAULT, phonesim_init, phonesim_exit)
