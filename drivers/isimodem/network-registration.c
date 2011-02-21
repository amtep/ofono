/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2009-2010 Nokia Corporation and/or its subsidiary(-ies).
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib.h>

#include <gisi/message.h>
#include <gisi/client.h>
#include <gisi/iter.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/netreg.h>

#include "isimodem.h"
#include "isiutil.h"
#include "network.h"
#include "debug.h"

struct reg_info {
	uint8_t status;
	uint8_t mode;
};

struct gsm_info {
	uint16_t lac;
	uint32_t ci;
	uint8_t egprs;
	uint8_t hsdpa;
	uint8_t hsupa;
};

struct rat_info {
	uint8_t rat;
	uint8_t compact;
};

struct network_time {
	uint8_t year;
	uint8_t mon;
	uint8_t mday;
	uint8_t hour;
	uint8_t min;
	uint8_t sec;
	uint8_t utc;
	uint8_t dst;
};

struct netreg_data {
	GIsiClient *client;
	struct reg_info reg;
	struct gsm_info gsm;
	struct rat_info rat;
};

static inline guint8 *mccmnc_to_bcd(const char *mcc, const char *mnc,
						guint8 *bcd)
{
	bcd[0] = (mcc[0] - '0') | (mcc[1] - '0') << 4;
	bcd[1] = (mcc[2] - '0');
	bcd[1] |= (mnc[2] == '\0' ? 0x0f : (mnc[2] - '0')) << 4;
	bcd[2] = (mnc[0] - '0') | (mnc[1] - '0') << 4;
	return bcd;
}

static inline int isi_status_to_at_status(struct reg_info *reg)
{
	switch (reg->status) {
	case NET_REG_STATUS_NOSERV:
	case NET_REG_STATUS_NOSERV_NOTSEARCHING:
	case NET_REG_STATUS_NOSERV_NOSIM:
	case NET_REG_STATUS_POWER_OFF:
	case NET_REG_STATUS_NSPS:
	case NET_REG_STATUS_NSPS_NO_COVERAGE:
		return 0;

	case NET_REG_STATUS_HOME:
		return 1;

	case NET_REG_STATUS_NOSERV_SEARCHING:
		return 2;

	case NET_REG_STATUS_NOSERV_SIM_REJECTED_BY_NW:
		return 3;

	case NET_REG_STATUS_ROAM_BLINK:
	case NET_REG_STATUS_ROAM:
		return 5;
	}
	return 4;
}

static inline int isi_to_at_tech(struct rat_info *rat, struct gsm_info *gsm)
{
	int tech = -1;

	if (rat == NULL || gsm == NULL)
		return -1;

	if (rat->rat == NET_GSM_RAT)
		tech = 0;

	if (rat->compact)
		tech = 1;

	if (rat->rat == NET_UMTS_RAT)
		tech = 2;

	if (gsm->egprs)
		tech = 3;

	if (gsm->hsdpa)
		tech = 4;

	if (gsm->hsupa)
		tech = 5;

	if (gsm->hsdpa && gsm->hsupa)
		tech = 6;

	return tech;
}

static gboolean check_response_status(const GIsiMessage *msg, uint8_t msgid)
{
	uint8_t cause;

	if (g_isi_msg_error(msg) < 0) {
		DBG("Error: %s", strerror(-g_isi_msg_error(msg)));
		return FALSE;
	}

	if (g_isi_msg_id(msg) != msgid) {
		DBG("Unexpected msg: %s",
			net_message_id_name(g_isi_msg_id(msg)));
		return FALSE;
	}

	if (!g_isi_msg_data_get_byte(msg, 0, &cause))
		return FALSE;

	if (cause != NET_CAUSE_OK) {
		DBG("Request failed: %s", net_isi_cause_name(cause));
		return FALSE;
	}
	return TRUE;
}

static gboolean parse_common_info(GIsiSubBlockIter *iter, struct reg_info *reg)
{
	return reg && g_isi_sb_iter_get_byte(iter, &reg->status, 2) &&
		g_isi_sb_iter_get_byte(iter, &reg->mode, 3);
}

static gboolean parse_gsm_info(GIsiSubBlockIter *iter, struct gsm_info *gsm)
{
	return gsm && g_isi_sb_iter_get_word(iter, &gsm->lac, 2) &&
			g_isi_sb_iter_get_dword(iter, &gsm->ci, 4) &&
			g_isi_sb_iter_get_byte(iter, &gsm->egprs, 17) &&
			g_isi_sb_iter_get_byte(iter, &gsm->hsdpa, 20) &&
			g_isi_sb_iter_get_byte(iter, &gsm->hsupa, 21);
}

static void reg_status_ind_cb(const GIsiMessage *msg, void *data)
{
	struct ofono_netreg *netreg = data;
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	GIsiSubBlockIter iter;

	if (netreg == NULL || nd == NULL)
		return;

	if (g_isi_msg_id(msg) != NET_REG_STATUS_IND)
		return;

	for (g_isi_sb_iter_init(&iter, msg, 2);
			g_isi_sb_iter_is_valid(&iter);
			g_isi_sb_iter_next(&iter)) {

		switch (g_isi_sb_iter_get_id(&iter)) {
		case NET_REG_INFO_COMMON:

			if (!parse_common_info(&iter, &nd->reg))
				return;
			break;

		case NET_GSM_REG_INFO:

			if (!parse_gsm_info(&iter, &nd->gsm))
				return;
			break;
		}
	}

	ofono_netreg_status_notify(netreg, isi_status_to_at_status(&nd->reg),
					nd->gsm.lac, nd->gsm.ci,
					isi_to_at_tech(&nd->rat, &nd->gsm));
}

static gboolean parse_rat_info(GIsiSubBlockIter *iter, struct rat_info *rat)
{
	uint8_t len;

	if (!g_isi_sb_iter_get_byte(iter, &rat->rat, 2))
		return FALSE;

	if (!g_isi_sb_iter_get_byte(iter, &len, 3))
		return FALSE;

	if (len != 0 && !g_isi_sb_iter_get_byte(iter, &rat->compact, 4))
		return FALSE;

	return TRUE;
}

static void rat_ind_cb(const GIsiMessage *msg, void *data)
{
	struct ofono_netreg *netreg = data;
	struct netreg_data *nd = ofono_netreg_get_data(netreg);

	GIsiSubBlockIter iter;

	if (nd == NULL || g_isi_msg_id(msg) != NET_RAT_IND)
		return;

	for (g_isi_sb_iter_init(&iter, msg, 2);
			g_isi_sb_iter_is_valid(&iter);
			g_isi_sb_iter_next(&iter)) {

		if (g_isi_sb_iter_get_id(&iter) != NET_RAT_INFO)
			continue;

		if (!parse_rat_info(&iter, &nd->rat))
			return;
	}

	ofono_netreg_status_notify(netreg, isi_status_to_at_status(&nd->reg),
					nd->gsm.lac, nd->gsm.ci,
					isi_to_at_tech(&nd->rat, &nd->gsm));
}

static void reg_status_resp_cb(const GIsiMessage *msg, void *data)
{
	struct isi_cb_data *cbd = data;
	struct ofono_netreg *netreg = cbd->user;
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	ofono_netreg_status_cb_t cb = cbd->cb;

	GIsiSubBlockIter iter;

	if (!check_response_status(msg, NET_REG_STATUS_GET_RESP))
		goto error;

	for (g_isi_sb_iter_init(&iter, msg, 2);
			g_isi_sb_iter_is_valid(&iter);
			g_isi_sb_iter_next(&iter)) {

		switch (g_isi_sb_iter_get_id(&iter)) {
		case NET_REG_INFO_COMMON:

			if (!parse_common_info(&iter, &nd->reg))
				goto error;
			break;

		case NET_GSM_REG_INFO:

			if (!parse_gsm_info(&iter, &nd->gsm))
				goto error;
			break;
		}
	}

	CALLBACK_WITH_SUCCESS(cb, isi_status_to_at_status(&nd->reg),
				nd->gsm.lac, nd->gsm.ci,
				isi_to_at_tech(&nd->rat, &nd->gsm),
				cbd->data);
	g_free(cbd);
	return;

error:
	CALLBACK_WITH_FAILURE(cb, -1, -1, -1, -1, cbd->data);
	g_free(cbd);
}

static void rat_resp_cb(const GIsiMessage *msg, void *data)
{
	struct isi_cb_data *cbd = data;
	struct ofono_netreg *netreg = cbd->user;
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	ofono_netreg_status_cb_t cb = cbd->cb;

	const uint8_t req[] = {
		NET_REG_STATUS_GET_REQ
	};
	GIsiSubBlockIter iter;

	if (cbd == NULL || nd == NULL)
		goto error;

	if (!check_response_status(msg, NET_RAT_RESP))
		goto error;

	for (g_isi_sb_iter_init(&iter, msg, 2);
			g_isi_sb_iter_is_valid(&iter);
			g_isi_sb_iter_next(&iter)) {

		if (g_isi_sb_iter_get_id(&iter) != NET_RAT_INFO)
			continue;

		if (!parse_rat_info(&iter, &nd->rat))
			goto error;
	}

	if (g_isi_client_send(nd->client, req, sizeof(req),
				reg_status_resp_cb, cbd, NULL))
		return;

error:
	CALLBACK_WITH_FAILURE(cb, -1, -1, -1, -1, data);
	g_free(cbd);
}

static void isi_registration_status(struct ofono_netreg *netreg,
					ofono_netreg_status_cb_t cb, void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct isi_cb_data *cbd = isi_cb_data_new(netreg, cb, data);

	/*
	 * Current technology depends on the current RAT as well as
	 * the services reported by the current cell.  Therefore we
	 * need a pair of queries to deduce the full registration
	 * status: first query for the RAT then the actual
	 * registration status.
	 */
	const uint8_t rat[] = {
		NET_RAT_REQ,
		NET_CURRENT_RAT
	};

	if (nd == NULL || cbd == NULL)
		goto error;

	if (g_isi_client_send(nd->client, rat, sizeof(rat),
				rat_resp_cb, cbd, NULL))
		return;

error:
	CALLBACK_WITH_FAILURE(cb, -1, -1, -1, -1, data);
	g_free(cbd);
}

static void name_get_resp_cb(const GIsiMessage *msg, void *data)
{
	struct isi_cb_data *cbd = data;
	ofono_netreg_operator_cb_t cb = cbd->cb;
	struct ofono_network_operator op;

	GIsiSubBlockIter iter;
	uint8_t len = 0;
	char *tag = NULL;

	memset(&op, 0, sizeof(struct ofono_network_operator));

	if (!check_response_status(msg, NET_OPER_NAME_READ_RESP))
		goto error;

	for (g_isi_sb_iter_init(&iter, msg, 6);
			g_isi_sb_iter_is_valid(&iter);
			g_isi_sb_iter_next(&iter)) {

		switch (g_isi_sb_iter_get_id(&iter)) {
		case NET_GSM_OPERATOR_INFO:

			if (!g_isi_sb_iter_get_oper_code(&iter, op.mcc,
								op.mnc, 2))
				goto error;
			break;

		case NET_OPER_NAME_INFO:

			if (!g_isi_sb_iter_get_byte(&iter, &len, 3))
				goto error;

			/* Name is UCS-2 encoded */
			len *= 2;

			if (!g_isi_sb_iter_get_alpha_tag(&iter, &tag, len, 4))
				goto error;

			strncpy(op.name, tag, OFONO_MAX_OPERATOR_NAME_LENGTH);
			op.name[OFONO_MAX_OPERATOR_NAME_LENGTH] = '\0';
			g_free(tag);
			break;
		}
	}

	CALLBACK_WITH_SUCCESS(cb, &op, cbd->data);
	return;

error:
	CALLBACK_WITH_FAILURE(cb, NULL, cbd->data);
}


static void isi_current_operator(struct ofono_netreg *netreg,
					ofono_netreg_operator_cb_t cb,
					void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct isi_cb_data *cbd = isi_cb_data_new(netreg, cb, data);

	const uint8_t msg[] = {
		NET_OPER_NAME_READ_REQ,
		NET_HARDCODED_LATIN_OPER_NAME,
		OFONO_MAX_OPERATOR_NAME_LENGTH,
		0x00, 0x00,  /* Index not used */
		0x00,  /* Filler */
		0x00  /* No sub-blocks */
	};

	if (cbd == NULL || nd == NULL)
		goto error;

	if (g_isi_client_send(nd->client, msg, sizeof(msg),
				name_get_resp_cb, cbd, g_free))
		return;

error:
	CALLBACK_WITH_FAILURE(cb, NULL, data);
	g_free(cbd);
}


static void available_resp_cb(const GIsiMessage *msg, void *data)
{
	struct isi_cb_data *cbd = data;
	ofono_netreg_operator_list_cb_t cb = cbd->cb;
	struct ofono_network_operator *list = NULL;

	GIsiSubBlockIter iter;
	uint8_t sb_count;

	int total = 0;
	int common = 0;
	int detail = 0;

	if (!check_response_status(msg, NET_AVAILABLE_GET_RESP))
		goto error;

	if (!g_isi_msg_data_get_byte(msg, 1, &sb_count))
		goto error;

	/* Each description of an operator has a pair of sub-blocks */
	total = sb_count / 2;
	list = alloca(total * sizeof(struct ofono_network_operator));

	for (g_isi_sb_iter_init(&iter, msg, 2);
			g_isi_sb_iter_is_valid(&iter);
			g_isi_sb_iter_next(&iter)) {

		struct ofono_network_operator *op;
		char *tag = NULL;
		uint8_t taglen = 0;
		uint8_t status = 0;
		uint8_t umts = 0;

		switch (g_isi_sb_iter_get_id(&iter)) {

		case NET_AVAIL_NETWORK_INFO_COMMON:

			if (!g_isi_sb_iter_get_byte(&iter, &status, 2)
				|| !g_isi_sb_iter_get_byte(&iter, &taglen, 5)
				|| !g_isi_sb_iter_get_alpha_tag(&iter, &tag,
						taglen * 2, 6))
				goto error;

			op = list + common++;
			op->status = status;

			strncpy(op->name, tag, OFONO_MAX_OPERATOR_NAME_LENGTH);
			op->name[OFONO_MAX_OPERATOR_NAME_LENGTH] = '\0';
			g_free(tag);
			break;

		case NET_DETAILED_NETWORK_INFO:
			op = list + detail++;

			if (!g_isi_sb_iter_get_oper_code(&iter, op->mcc,
								op->mnc, 2)
				|| !g_isi_sb_iter_get_byte(&iter, &umts, 7))
				goto error;

			op->tech = umts ? 2 : 3;
			break;
		}
	}

	if (common == detail && detail == total) {
		CALLBACK_WITH_SUCCESS(cb, total, list, cbd->data);
		return;
	}

error:
	CALLBACK_WITH_FAILURE(cb, 0, NULL, cbd->data);
}

static void isi_list_operators(struct ofono_netreg *netreg,
				ofono_netreg_operator_list_cb_t cb,
				void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct isi_cb_data *cbd = isi_cb_data_new(netreg, cb, data);

	const unsigned char msg[] = {
		NET_AVAILABLE_GET_REQ,
		NET_MANUAL_SEARCH,
		0x01,  /* Sub-block count */
		NET_GSM_BAND_INFO,
		0x04,  /* Sub-block length */
		NET_GSM_BAND_ALL_SUPPORTED_BANDS,
		0x00
	};

	if (cbd == NULL || nd == NULL)
		goto error;

	if (g_isi_client_send_with_timeout(nd->client, msg, sizeof(msg),
				NETWORK_SCAN_TIMEOUT, available_resp_cb, cbd,
				g_free))
		return;

error:
	CALLBACK_WITH_FAILURE(cb, 0, NULL, data);
	g_free(cbd);
}

static void set_auto_resp_cb(const GIsiMessage *msg, void *data)
{
	struct isi_cb_data *cbd = data;
	struct netreg_data *nd = cbd->user;
	ofono_netreg_register_cb_t cb = cbd->cb;

	if (!check_response_status(msg, NET_SET_RESP)) {
		CALLBACK_WITH_FAILURE(cb, cbd->data);
		return;
	}

	nd->reg.mode = NET_SELECT_MODE_AUTOMATIC;
	CALLBACK_WITH_SUCCESS(cb, cbd->data);
}

static void isi_register_auto(struct ofono_netreg *netreg,
				ofono_netreg_register_cb_t cb,
				void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct isi_cb_data *cbd = isi_cb_data_new(netreg, cb, data);

	const unsigned char msg[] = {
		NET_SET_REQ,
		0x00,  /* Registered in another protocol? */
		0x01,  /* Sub-block count */
		NET_OPERATOR_INFO_COMMON,
		0x04,  /* Sub-block length */
		nd->reg.mode == NET_SELECT_MODE_AUTOMATIC
			? NET_SELECT_MODE_USER_RESELECTION
			: NET_SELECT_MODE_AUTOMATIC,
		0x00  /* Index not used */
	};

	if (nd == NULL || cbd == NULL)
		goto error;

	if (g_isi_client_send_with_timeout(nd->client, msg, sizeof(msg),
				NETWORK_SET_TIMEOUT,
				set_auto_resp_cb, cbd, g_free))
		return;

error:
	CALLBACK_WITH_FAILURE(cb, data);
	g_free(cbd);
}

static void set_manual_resp_cb(const GIsiMessage *msg, void *data)
{
	struct isi_cb_data *cbd = data;
	struct ofono_netreg *netreg = cbd->user;
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	ofono_netreg_register_cb_t cb = cbd->cb;

	if (!check_response_status(msg, NET_SET_RESP)) {
		CALLBACK_WITH_FAILURE(cb, cbd->data);
		return;
	}

	nd->reg.mode = NET_SELECT_MODE_MANUAL;
	CALLBACK_WITH_SUCCESS(cb, cbd->data);
}

static void isi_register_manual(struct ofono_netreg *netreg,
				const char *mcc, const char *mnc,
				ofono_netreg_register_cb_t cb, void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct isi_cb_data *cbd = isi_cb_data_new(netreg, cb, data);

	guint8 buffer[3] = { 0 };
	guint8 *bcd = mccmnc_to_bcd(mcc, mnc, buffer);

	const unsigned char msg[] = {
		NET_SET_REQ,
		0x00,  /* Registered in another protocol? */
		0x02,  /* Sub-block count */
		NET_OPERATOR_INFO_COMMON,
		0x04,  /* Sub-block length */
		NET_SELECT_MODE_MANUAL,
		0x00,  /* Index not used */
		NET_GSM_OPERATOR_INFO,
		0x08,  /* Sub-block length */
		bcd[0], bcd[1], bcd[2],
		NET_GSM_BAND_INFO_NOT_AVAIL,  /* Pick any supported band */
		0x00, 0x00  /* Filler */
	};

	if (cbd == NULL || nd == NULL)
		goto error;

	if (g_isi_client_send_with_timeout(nd->client, msg, sizeof(msg),
				NETWORK_SET_TIMEOUT,
				set_manual_resp_cb, cbd, g_free))
		return;

error:
	CALLBACK_WITH_FAILURE(cb, data);
	g_free(cbd);
}

static void rssi_ind_cb(const GIsiMessage *msg, void *data)
{
	struct ofono_netreg *netreg = data;
	uint8_t rssi;

	if (g_isi_msg_id(msg) != NET_RSSI_IND)
		return;

	if (!g_isi_msg_data_get_byte(msg, 0, &rssi))
		return;

	ofono_netreg_strength_notify(netreg, rssi ? rssi : -1);
}

static gboolean parse_nettime(GIsiSubBlockIter *iter,
				struct ofono_network_time *info)
{
	struct network_time *time;
	size_t len = sizeof(struct network_time);

	if (!g_isi_sb_iter_get_struct(iter, (void **) &time, len, 2))
		return FALSE;

	/* Value is years since last turn of century */
	info->year = time->year != NET_INVALID_TIME ? time->year : -1;
	info->year += 2000;

	info->mon = time->mon != NET_INVALID_TIME ? time->mon : -1;
	info->mday = time->mday != NET_INVALID_TIME ? time->mday : -1;
	info->hour = time->hour != NET_INVALID_TIME ? time->hour : -1;
	info->min = time->min != NET_INVALID_TIME ? time->min : -1;
	info->sec = time->sec != NET_INVALID_TIME ? time->sec : -1;

	/* Most significant bit set indicates negative offset. The
	 * second most significant bit is 'reserved'. The value is the
	 * offset from UTCin a count of 15min intervals, possibly
	 * including the current DST adjustment. */
	info->utcoff = (time->utc & 0x3F) * 15 * 60;
	if (time->utc & 0x80)
		info->utcoff *= -1;

	info->dst = time->dst != NET_INVALID_TIME ? time->dst : -1;
	return TRUE;
}

static void time_ind_cb(const GIsiMessage *msg, void *data)
{
	struct ofono_netreg *netreg = data;
	struct ofono_network_time info;
	GIsiSubBlockIter iter;

	if (g_isi_msg_id(msg) != NET_TIME_IND)
		return;

	for (g_isi_sb_iter_init(&iter, msg, 2);
			g_isi_sb_iter_is_valid(&iter);
			g_isi_sb_iter_next(&iter)) {

		if (g_isi_sb_iter_get_id(&iter) != NET_TIME_INFO)
			continue;

		if (!parse_nettime(&iter, &info))
			return;

		ofono_netreg_time_notify(netreg, &info);
		return;
	}
}

static void rssi_resp_cb(const GIsiMessage *msg, void *data)
{
	struct isi_cb_data *cbd = data;
	ofono_netreg_strength_cb_t cb = cbd->cb;
	uint8_t rssi;

	GIsiSubBlockIter iter;

	if (!check_response_status(msg, NET_RSSI_GET_RESP)) {
		CALLBACK_WITH_FAILURE(cb, -1, cbd->data);
		return;
	}

	for (g_isi_sb_iter_init(&iter, msg, 2);
			g_isi_sb_iter_is_valid(&iter);
			g_isi_sb_iter_next(&iter)) {

		if (g_isi_sb_iter_get_id(&iter) != NET_RSSI_CURRENT)
			continue;

		if (!g_isi_sb_iter_get_byte(&iter, &rssi, 2))
			break;

		CALLBACK_WITH_SUCCESS(cb, rssi ? rssi : -1, cbd->data);
		return;
	}
}

static void isi_strength(struct ofono_netreg *netreg,
				ofono_netreg_strength_cb_t cb,
				void *data)
{
	struct netreg_data *nd = ofono_netreg_get_data(netreg);
	struct isi_cb_data *cbd = isi_cb_data_new(netreg, cb, data);

	const uint8_t msg[] = {
		NET_RSSI_GET_REQ,
		NET_CS_GSM,
		NET_CURRENT_CELL_RSSI,
	};

	if (nd == NULL || cbd == NULL)
		goto error;

	if (g_isi_client_send(nd->client, msg, sizeof(msg),
				rssi_resp_cb, cbd, g_free))
		return;

error:
	CALLBACK_WITH_FAILURE(cb, -1, data);
	g_free(cbd);
}

static void reachable_cb(const GIsiMessage *msg, void *data)
{
	struct ofono_netreg *netreg = data;
	struct netreg_data *nd = ofono_netreg_get_data(netreg);

	if (g_isi_msg_error(msg) < 0)
		return;

	ISI_VERSION_DBG(msg);

	g_isi_client_ind_subscribe(nd->client, NET_RSSI_IND, rssi_ind_cb,
					netreg);
	g_isi_client_ind_subscribe(nd->client, NET_REG_STATUS_IND,
					reg_status_ind_cb, netreg);
	g_isi_client_ind_subscribe(nd->client, NET_RAT_IND, rat_ind_cb, netreg);
	g_isi_client_ind_subscribe(nd->client, NET_TIME_IND, time_ind_cb,
					netreg);

	ofono_netreg_register(netreg);
}

static int isi_netreg_probe(struct ofono_netreg *netreg, unsigned int vendor,
				void *user)
{
	GIsiModem *modem = user;
	struct netreg_data *nd;

	nd = g_try_new0(struct netreg_data, 1);
	if (nd == NULL)
		return -ENOMEM;

	nd->client = g_isi_client_create(modem, PN_NETWORK);
	if (nd->client == NULL) {
		g_free(nd);
		return -ENOMEM;
	}

	ofono_netreg_set_data(netreg, nd);

	g_isi_client_verify(nd->client, reachable_cb, netreg, NULL);

	return 0;
}

static void isi_netreg_remove(struct ofono_netreg *netreg)
{
	struct netreg_data *data = ofono_netreg_get_data(netreg);

	ofono_netreg_set_data(netreg, NULL);

	if (data == NULL)
		return;

	g_isi_client_destroy(data->client);
	g_free(data);
}

static struct ofono_netreg_driver driver = {
	.name			= "isimodem",
	.probe			= isi_netreg_probe,
	.remove			= isi_netreg_remove,
	.registration_status	= isi_registration_status,
	.current_operator	= isi_current_operator,
	.list_operators		= isi_list_operators,
	.register_auto		= isi_register_auto,
	.register_manual	= isi_register_manual,
	.strength		= isi_strength,
};

void isi_netreg_init(void)
{
	ofono_netreg_driver_register(&driver);
}

void isi_netreg_exit(void)
{
	ofono_netreg_driver_unregister(&driver);
}
