/*
 *
 *  oFono - Open Source Telephony
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
#include <ofono/netreg.h>

#include "gatchat.h"
#include "gatresult.h"

#include "atmodem.h"
#include "vendor.h"

static const char *none_prefix[] = { NULL };
static const char *creg_prefix[] = { "+CREG:", NULL };
static const char *cops_prefix[] = { "+COPS:", NULL };
static const char *csq_prefix[] = { "+CSQ:", NULL };

struct netreg_data {
	GAtChat *chat;
	char mcc[OFONO_MAX_MCC_LENGTH + 1];
	char mnc[OFONO_MAX_MNC_LENGTH + 1];
	unsigned int vendor;
};

static void extract_mcc_mnc(const char *str, char *mcc, char *mnc)
{
	/* Three digit country code */
	strncpy(mcc, str, OFONO_MAX_MCC_LENGTH);
	mcc[OFONO_MAX_MCC_LENGTH] = '\0';

	/* Usually a 2 but sometimes 3 digit network code */
	strncpy(mnc, str + OFONO_MAX_MCC_LENGTH, OFONO_MAX_MNC_LENGTH);
	mnc[OFONO_MAX_MNC_LENGTH] = '\0';
}

static void at_creg_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	GAtResultIter iter;
	ofono_netreg_status_cb_t cb = cbd->cb;
	int status;
	const char *str;
	int lac = -1, ci = -1, tech = -1;
	struct ofono_error error;

	dump_response("at_creg_cb", ok, result);
	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok) {
		cb(&error, -1, -1, -1, -1, cbd->data);
		return;
	}

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CREG:")) {
		CALLBACK_WITH_FAILURE(cb, -1, -1, -1, -1, cbd->data);
		return;
	}

	/* Skip <n> the unsolicited result code */
	g_at_result_iter_skip_next(&iter);

	g_at_result_iter_next_number(&iter, &status);

	if (g_at_result_iter_next_string(&iter, &str) == TRUE)
		lac = strtol(str, NULL, 16);
	else
		goto out;

	if (g_at_result_iter_next_string(&iter, &str) == TRUE)
		ci = strtol(str, NULL, 16);
	else
		goto out;

	g_at_result_iter_next_number(&iter, &tech);

out:
	ofono_debug("creg_cb: %d, %d, %d, %d", status, lac, ci, tech);

	cb(&error, status, lac, ci, tech, cbd->data);
}

static void at_registration_status(struct ofono_netreg *netreg,
					ofono_netreg_status_cb_t cb,
					void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct cb_data *cbd = cb_data_new(cb, data);

	if (!cbd)
		goto error;

	if (g_at_chat_send(nd->chat, "AT+CREG?", creg_prefix,
				at_creg_cb, cbd, g_free) > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, -1, -1, -1, -1, data);
}

static void cops_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	struct netreg_data *nd = ofono_netreg_get_data(cbd->user);
	ofono_netreg_operator_cb_t cb = cbd->cb;
	struct ofono_network_operator op;
	GAtResultIter iter;
	int format, tech;
	const char *name;
	struct ofono_error error;

	dump_response("cops_cb", ok, result);
	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok || nd->mcc[0] == '\0' || nd->mnc[0] == '\0') {
		cb(&error, NULL, cbd->data);
		goto out;
	}

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+COPS:"))
		goto error;

	g_at_result_iter_skip_next(&iter);

	ok = g_at_result_iter_next_number(&iter, &format);

	if (ok == FALSE || format != 0)
		goto error;

	if (g_at_result_iter_next_string(&iter, &name) == FALSE)
		goto error;

	/* Default to GSM */
	if (g_at_result_iter_next_number(&iter, &tech) == FALSE)
		tech = 0;

	strncpy(op.name, name, OFONO_MAX_OPERATOR_NAME_LENGTH);
	op.name[OFONO_MAX_OPERATOR_NAME_LENGTH] = '\0';

	strncpy(op.mcc, nd->mcc, OFONO_MAX_MCC_LENGTH);
	op.mcc[OFONO_MAX_MCC_LENGTH] = '\0';

	strncpy(op.mnc, nd->mnc, OFONO_MAX_MNC_LENGTH);
	op.mnc[OFONO_MAX_MNC_LENGTH] = '\0';

	op.status = -1;
	op.tech = tech;

	ofono_debug("cops_cb: %s, %s %s %d", name, nd->mcc, nd->mnc, tech);

	cb(&error, &op, cbd->data);

out:
	g_free(cbd);

	return;

error:
	CALLBACK_WITH_FAILURE(cb, NULL, cbd->data);

	g_free(cbd);
}

static void cops_numeric_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	struct netreg_data *nd = ofono_netreg_get_data(cbd->user);
	GAtResultIter iter;
	const char *str;
	int format;

	dump_response("cops_numeric_cb", ok, result);

	if (!ok)
		goto error;

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+COPS:"))
		goto error;

	g_at_result_iter_skip_next(&iter);

	ok = g_at_result_iter_next_number(&iter, &format);

	if (ok == FALSE || format != 2)
		goto error;

	if (g_at_result_iter_next_string(&iter, &str) == FALSE ||
		strlen(str) == 0)
		goto error;

	extract_mcc_mnc(str, nd->mcc, nd->mnc);

	ofono_debug("Cops numeric got mcc: %s, mnc: %s", nd->mcc, nd->mnc);

	return;

error:
	nd->mcc[0] = '\0';
	nd->mnc[0] = '\0';
}

static void at_current_operator(struct ofono_netreg *netreg,
				ofono_netreg_operator_cb_t cb, void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct cb_data *cbd = cb_data_new(cb, data);
	gboolean ok;

	if (!cbd)
		goto error;

	cbd->user = netreg;

	ok = g_at_chat_send(nd->chat, "AT+COPS=3,2", none_prefix,
				NULL, NULL, NULL);

	if (ok)
		ok = g_at_chat_send(nd->chat, "AT+COPS?", cops_prefix,
					cops_numeric_cb, cbd, NULL);

	if (ok)
		ok = g_at_chat_send(nd->chat, "AT+COPS=3,0", none_prefix,
					NULL, NULL, NULL);

	if (ok)
		ok = g_at_chat_send(nd->chat, "AT+COPS?", cops_prefix,
					cops_cb, cbd, NULL);

	if (ok)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, NULL, data);
}

static void cops_list_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_netreg_operator_list_cb_t cb = cbd->cb;
	struct ofono_network_operator *list;
	GAtResultIter iter;
	int num = 0;
	struct ofono_error error;

	dump_response("cops_list_cb", ok, result);
	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok) {
		cb(&error, 0, NULL, cbd->data);
		return;
	}

	g_at_result_iter_init(&iter, result);

	while (g_at_result_iter_next(&iter, "+COPS:")) {
		while (g_at_result_iter_skip_next(&iter))
			num += 1;
	}

	ofono_debug("Got %d elements", num);

	list = g_try_new0(struct ofono_network_operator, num);

	if (!list) {
		CALLBACK_WITH_FAILURE(cb, 0, NULL, cbd->data);
		return;
	}

	num = 0;
	g_at_result_iter_init(&iter, result);

	while (g_at_result_iter_next(&iter, "+COPS:")) {
		int status, tech;
		const char *l, *s, *n;
		gboolean have_long = FALSE;

		while (1) {
			if (!g_at_result_iter_open_list(&iter))
				break;

			if (!g_at_result_iter_next_number(&iter, &status))
				break;

			list[num].status = status;

			if (!g_at_result_iter_next_string(&iter, &l))
				break;

			if (strlen(l) > 0) {
				have_long = TRUE;
				strncpy(list[num].name, l,
					OFONO_MAX_OPERATOR_NAME_LENGTH);
			}

			if (!g_at_result_iter_next_string(&iter, &s))
				break;

			if (strlen(s) > 0 && !have_long)
				strncpy(list[num].name, s,
					OFONO_MAX_OPERATOR_NAME_LENGTH);

			list[num].name[OFONO_MAX_OPERATOR_NAME_LENGTH] = '\0';

			if (!g_at_result_iter_next_string(&iter, &n))
				break;

			extract_mcc_mnc(n, list[num].mcc, list[num].mnc);

			if (!g_at_result_iter_next_number(&iter, &tech))
				tech = 0;

			list[num].tech = tech;

			if (!g_at_result_iter_close_list(&iter))
				break;

			num += 1;
		}
	}

	ofono_debug("Got %d operators", num);

{
	int i = 0;

	for (; i < num; i++) {
		ofono_debug("Operator: %s, %s, %s, status: %d, %d",
				list[i].name, list[i].mcc, list[i].mnc,
				list[i].status, list[i].tech);
	}
}

	cb(&error, num, list, cbd->data);

	g_free(list);
}

static void at_list_operators(struct ofono_netreg *netreg,
				ofono_netreg_operator_list_cb_t cb, void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct cb_data *cbd = cb_data_new(cb, data);

	if (!cbd)
		goto error;

	if (g_at_chat_send(nd->chat, "AT+COPS=?", cops_prefix,
				cops_list_cb, cbd, g_free) > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, 0, NULL, data);
}

static void register_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_netreg_register_cb_t cb = cbd->cb;
	struct ofono_error error;

	dump_response("register_cb", ok, result);
	decode_at_error(&error, g_at_result_final_response(result));

	cb(&error, cbd->data);
}

static void at_register_auto(struct ofono_netreg *netreg,
				ofono_netreg_register_cb_t cb, void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct cb_data *cbd = cb_data_new(cb, data);

	if (!cbd)
		goto error;

	if (g_at_chat_send(nd->chat, "AT+COPS=0", none_prefix,
				register_cb, cbd, g_free) > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, data);
}

static void at_register_manual(struct ofono_netreg *netreg,
				const struct ofono_network_operator *oper,
				ofono_netreg_register_cb_t cb, void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct cb_data *cbd = cb_data_new(cb, data);
	char buf[128];

	if (!cbd)
		goto error;

	sprintf(buf, "AT+COPS=1,2,\"%s%s\"", oper->mcc, oper->mnc);

	if (g_at_chat_send(nd->chat, buf, none_prefix,
				register_cb, cbd, g_free) > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, data);
}

static void at_deregister(struct ofono_netreg *netreg,
				ofono_netreg_register_cb_t cb, void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct cb_data *cbd = cb_data_new(cb, data);

	if (!cbd)
		goto error;

	if (g_at_chat_send(nd->chat, "AT+COPS=2", none_prefix,
				register_cb, cbd, g_free) > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, data);
}

static inline void report_signal_strength(struct ofono_netreg *netreg,
						int strength)
{
	ofono_debug("csq_notify: %d", strength);

	if (strength == 99)
		strength = -1;
	else
		strength = (strength * 100) / 31;

	ofono_netreg_strength_notify(netreg, strength);
}

static void csq_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_netreg *netreg = user_data;
	int strength;
	GAtResultIter iter;

	dump_response("csq_notify", TRUE, result);

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CSQ:"))
		return;

	if (!g_at_result_iter_next_number(&iter, &strength))
		return;

	report_signal_strength(netreg, strength);
}

static void calypso_csq_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_netreg *netreg = user_data;
	int strength;
	GAtResultIter iter;

	dump_response("calypso_csq_notify", TRUE, result);

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "%CSQ:"))
		return;

	if (!g_at_result_iter_next_number(&iter, &strength))
		return;

	report_signal_strength(netreg, strength);
}

static void csq_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_netreg_strength_cb_t cb = cbd->cb;
	int strength;
	GAtResultIter iter;
	struct ofono_error error;

	dump_response("csq_cb", ok, result);
	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok) {
		cb(&error, -1, cbd->data);
		return;
	}

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CSQ:")) {
		CALLBACK_WITH_FAILURE(cb, -1, cbd->data);
		return;
	}

	g_at_result_iter_next_number(&iter, &strength);

	ofono_debug("csq_cb: %d", strength);

	if (strength == 99)
		strength = -1;
	else
		strength = (strength * 100) / 31;

	cb(&error, strength, cbd->data);
}

static void at_signal_strength(struct ofono_netreg *netreg,
				ofono_netreg_strength_cb_t cb, void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct cb_data *cbd = cb_data_new(cb, data);

	if (!cbd)
		goto error;

	if (g_at_chat_send(nd->chat, "AT+CSQ", csq_prefix,
				csq_cb, cbd, g_free) > 0)
		return;

error:
	if (cbd)
		g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, -1, data);
}

static void creg_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_netreg *netreg = user_data;
	GAtResultIter iter;
	int status;
	int lac = -1, ci = -1, tech = -1;
	const char *str;

	dump_response("creg_notify", TRUE, result);

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CREG:"))
		return;

	g_at_result_iter_next_number(&iter, &status);

	if (g_at_result_iter_next_string(&iter, &str) == TRUE)
		lac = strtol(str, NULL, 16);
	else
		goto out;

	if (g_at_result_iter_next_string(&iter, &str) == TRUE)
		ci = strtol(str, NULL, 16);
	else
		goto out;

	g_at_result_iter_next_number(&iter, &tech);

out:
	ofono_debug("creg_notify: %d, %d, %d, %d", status, lac, ci, tech);

	ofono_netreg_status_notify(netreg, status, lac, ci, tech);
}

static void at_network_registration_initialized(gboolean ok, GAtResult *result,
							gpointer user_data)
{
	struct ofono_netreg *netreg = user_data;
	struct netreg_data *nd = ofono_netreg_get_data(netreg);

	if (!ok) {
		ofono_error("Unable to initialize Network Registration");
		ofono_netreg_remove(netreg);
		return;
	}

	g_at_chat_register(nd->chat, "+CREG:",
				creg_notify, FALSE, netreg, NULL);
	g_at_chat_register(nd->chat, "+CSQ:",
				csq_notify, FALSE, netreg, NULL);

	if (nd->vendor == OFONO_VENDOR_CALYPSO)
		g_at_chat_register(nd->chat, "%CSQ:", calypso_csq_notify,
					FALSE, netreg, NULL);

	ofono_netreg_register(netreg);
}

static int at_netreg_probe(struct ofono_netreg *netreg, unsigned int vendor,
				void *data)
{
	GAtChat *chat = data;
	struct netreg_data *nd;

	nd = g_new0(struct netreg_data, 1);

	nd->chat = chat;
	nd->vendor = vendor;
	ofono_netreg_set_data(netreg, nd);

	if (nd->vendor == OFONO_VENDOR_CALYPSO)
		g_at_chat_send(chat, "AT%CSQ=1", NULL, NULL, NULL, NULL);

	g_at_chat_send(chat, "AT+CREG=2", NULL,
				at_network_registration_initialized,
				netreg, NULL);
	return 0;
}

static void at_netreg_remove(struct ofono_netreg *netreg)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);

	g_free(nd);
}

static struct ofono_netreg_driver driver = {
	.name				= "atmodem",
	.probe				= at_netreg_probe,
	.remove				= at_netreg_remove,
	.registration_status 		= at_registration_status,
	.current_operator 		= at_current_operator,
	.list_operators			= at_list_operators,
	.register_auto			= at_register_auto,
	.register_manual		= at_register_manual,
	.deregister			= at_deregister,
	.strength			= at_signal_strength,
};

void at_netreg_init()
{
	ofono_netreg_driver_register(&driver);
}

void at_netreg_exit()
{
	ofono_netreg_driver_unregister(&driver);
}
