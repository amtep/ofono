/*
 *
 *  oFono - Open Source Telephony - RIL Modem Support
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
 *  Copyright (C) 2012 Canonical Ltd.
 *  Copyright (C) 2013 Jolla Ltd.
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
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <glib.h>
#include <gril.h>
#include <parcel.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/sms.h>
#include "smsutil.h"
#include "util.h"

#include "rilmodem.h"

struct sms_data {
	GRil *ril;
	unsigned int vendor;
	guint timer_id;
};


static void ril_csca_set_cb(struct ril_msg *message, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_sms_sca_set_cb_t cb = cbd->cb;

	if (message->error == RIL_E_SUCCESS)
		CALLBACK_WITH_SUCCESS(cb, cbd->data);
	else
		CALLBACK_WITH_FAILURE(cb, cbd->data);
}

static void ril_csca_set(struct ofono_sms *sms,
			const struct ofono_phone_number *sca,
			ofono_sms_sca_set_cb_t cb, void *user_data)
{
	struct sms_data *data = ofono_sms_get_data(sms);
	struct cb_data *cbd = cb_data_new(cb, user_data);
	struct parcel rilp;
	int ret = -1;
	char number[OFONO_MAX_PHONE_NUMBER_LENGTH + 4];

	if (sca->type == 129)
		snprintf(number, sizeof(number), "\"%s\"", sca->number);
	else
		snprintf(number, sizeof(number), "\"+%s\"", sca->number);

	DBG("Setting sca: %s", number);

	parcel_init(&rilp);
	parcel_w_string(&rilp, number);

	/* Send request to RIL */
	ret = g_ril_send(data->ril, RIL_REQUEST_SET_SMSC_ADDRESS, rilp.data,
				rilp.size, ril_csca_set_cb, cbd, g_free);
	parcel_free(&rilp);

	/* In case of error free cbd and return the cb with failure */
	if (ret <= 0) {
		g_free(cbd);
		CALLBACK_WITH_FAILURE(cb, user_data);
	}
}

static void ril_csca_query_cb(struct ril_msg *message, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_sms_sca_query_cb_t cb = cbd->cb;
	struct ofono_error error;
	struct ofono_phone_number sca;
	struct parcel rilp;
	gchar *number, *temp_buf;

	if (message->error == RIL_E_SUCCESS) {
		decode_ril_error(&error, "OK");
	} else {
		decode_ril_error(&error, "FAIL");
		cb(&error, NULL, cbd->data);
		return;
	}

	ril_util_init_parcel(message, &rilp);
	temp_buf = parcel_r_string(&rilp);

	if (temp_buf != NULL) {
		/* RIL gives address in quotes */
		number = strtok(temp_buf, "\"");

		if (number[0] == '+') {
			number = number + 1;
			sca.type = 145;
		} else {
			sca.type = 129;
		}
		strncpy(sca.number, number, OFONO_MAX_PHONE_NUMBER_LENGTH);
		sca.number[OFONO_MAX_PHONE_NUMBER_LENGTH] = '\0';

		DBG("csca_query_cb: %s, %d", sca.number, sca.type);

		cb(&error, &sca, cbd->data);
	} else {
		CALLBACK_WITH_FAILURE(cb, NULL, cbd->data);
	}
}

static void ril_csca_query(struct ofono_sms *sms, ofono_sms_sca_query_cb_t cb,
					void *user_data)
{
	struct sms_data *data = ofono_sms_get_data(sms);
	struct cb_data *cbd = cb_data_new(cb, user_data);
	int ret = -1;

	DBG("Sending csca_query");

	ret = g_ril_send(data->ril, RIL_REQUEST_GET_SMSC_ADDRESS, NULL, 0,
					ril_csca_query_cb, cbd, g_free);

	if (ret <= 0) {
		g_free(cbd);
		CALLBACK_WITH_FAILURE(cb, NULL, user_data);
	}

}

static void submit_sms_cb(struct ril_msg *message, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	struct ofono_error error;
	ofono_sms_submit_cb_t cb = cbd->cb;
	struct sms_data *sd = cbd->user;
	int mr;

	if (message->error == RIL_E_SUCCESS) {
		decode_ril_error(&error, "OK");
	} else {
		decode_ril_error(&error, "FAIL");
	}

	mr = ril_util_parse_sms_response(sd->ril, message);

	cb(&error, mr, cbd->data);
}

static void ril_cmgs(struct ofono_sms *sms, const unsigned char *pdu,
			int pdu_len, int tpdu_len, int mms,
			ofono_sms_submit_cb_t cb, void *user_data)
{
	struct sms_data *sd = ofono_sms_get_data(sms);
	struct cb_data *cbd = cb_data_new(cb, user_data);
	struct parcel rilp;
	char *tpdu;
	int request = RIL_REQUEST_SEND_SMS;
	int ret, smsc_len;

	cbd->user = sd;

        DBG("pdu_len: %d, tpdu_len: %d mms: %d", pdu_len, tpdu_len, mms);

	/* TODO: if (mms) { ... } */

	parcel_init(&rilp);
	parcel_w_int32(&rilp, 2);     /* Number of strings */

	/* SMSC address:
	 *
	 * smsc_len == 1, then zero-length SMSC was spec'd
	 * RILD expects a NULL string in this case instead
	 * of a zero-length string.
	 */
	smsc_len = pdu_len - tpdu_len;
	if (smsc_len > 1) {
		/* TODO: encode SMSC & write to parcel */
		DBG("SMSC address specified (smsc_len %d); NOT-IMPLEMENTED", smsc_len);
	}

	parcel_w_string(&rilp, NULL); /* SMSC address; NULL == default */

	/* TPDU:
	 *
	 * 'pdu' is a raw hexadecimal string
	 *  encode_hex() turns it into an ASCII/hex UTF8 buffer
	 *  parcel_w_string() encodes utf8 -> utf16
	 */
	tpdu = encode_hex(pdu + smsc_len, tpdu_len, 0);
	parcel_w_string(&rilp, tpdu);

	ret = g_ril_send(sd->ril,
				request,
				rilp.data,
				rilp.size,
				submit_sms_cb, cbd, g_free);

	g_ril_append_print_buf(sd->ril, "(%s)", tpdu);
	g_ril_print_request(sd->ril, ret, request);

	parcel_free(&rilp);

	if (ret <= 0) {
		g_free(cbd);
		CALLBACK_WITH_FAILURE(cb, -1, user_data);
	}
}

static void ril_ack_delivery_cb(struct ril_msg *message, gpointer user_data)
{
	if (message->error != RIL_E_SUCCESS)
		ofono_error(
			"SMS acknowledgement failed: Further SMS reception is not guaranteed");
}

static void ril_ack_delivery(struct ofono_sms *sms)
{
	struct sms_data *sd = ofono_sms_get_data(sms);
	struct parcel rilp;
	int ret;
	int request = RIL_REQUEST_SMS_ACKNOWLEDGE;

	parcel_init(&rilp);
	parcel_w_int32(&rilp, 2); /* Number of int32 values in array */
	parcel_w_int32(&rilp, 1); /* Successful receipt */
	parcel_w_int32(&rilp, 0); /* error code */

	/* TODO: should ACK be sent for either of the error cases? */

	/* ACK the incoming NEW_SMS */
	ret = g_ril_send(sd->ril, request,
			rilp.data,
			rilp.size,
			ril_ack_delivery_cb, NULL, NULL);

	g_ril_append_print_buf(sd->ril, "(1,0)");
	g_ril_print_request(sd->ril, ret, request);

	parcel_free(&rilp);
}

static void ril_sms_notify(struct ril_msg *message, gpointer user_data)
{
	struct ofono_sms *sms = user_data;
	struct sms_data *sd = ofono_sms_get_data(sms);
	struct parcel rilp;
	char *ril_pdu;
	int ril_pdu_len;
	unsigned int smsc_len;
	long ril_buf_len;
	guchar *ril_data;

	DBG("req: %d; data_len: %d", message->req, message->buf_len);

	switch (message->req) {
	case RIL_UNSOL_RESPONSE_NEW_SMS:
	case RIL_UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT:
		break;
	default:
		goto error;
	}

	ril_util_init_parcel(message, &rilp);

	ril_pdu = parcel_r_string(&rilp);
	if (ril_pdu == NULL)
		goto error;

	ril_pdu_len = strlen(ril_pdu);

	DBG("ril_pdu_len is %d", ril_pdu_len);
	ril_data = decode_hex(ril_pdu, ril_pdu_len, &ril_buf_len, -1);
	if (ril_data == NULL)
		goto error;

	/* The first octect in the pdu contains the SMSC address length
	 * which is the X following octects it reads. We add 1 octet to
	 * the read length to take into account this read octet in order
	 * to calculate the proper tpdu length.
	 */
	smsc_len = ril_data[0] + 1;
	DBG("smsc_len is %d", smsc_len);

	g_ril_append_print_buf(sd->ril, "(%s)", ril_pdu);
	g_ril_print_unsol(sd->ril, message);

	if (message->req == RIL_UNSOL_RESPONSE_NEW_SMS) {
		/* Last parameter is 'tpdu_len' ( substract SMSC length ) */
		ofono_sms_deliver_notify(sms, ril_data,
				ril_buf_len,
				ril_buf_len - smsc_len);
	} else if (message->req == RIL_UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT) {
		ofono_sms_status_notify(sms, ril_data, ril_buf_len,
						ril_buf_len - smsc_len);
	}

	ril_ack_delivery(sms);

	return;

error:
	ofono_error("Unable to parse NEW_SMS notification");
}

static gboolean ril_delayed_register(gpointer user_data)
{
	struct ofono_sms *sms = user_data;
	struct sms_data *data = ofono_sms_get_data(sms);

	DBG("");

	data->timer_id = 0;

	ofono_sms_register(sms);

	g_ril_register(data->ril, RIL_UNSOL_RESPONSE_NEW_SMS,
			ril_sms_notify, sms);
	g_ril_register(data->ril, RIL_UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT,
			ril_sms_notify, sms);

	/* This makes the timeout a single-shot */
	return FALSE;
}

static int ril_sms_probe(struct ofono_sms *sms, unsigned int vendor,
				void *user)
{
	GRil *ril = user;
	struct sms_data *data;

	data = g_new0(struct sms_data, 1);
	data->ril = g_ril_clone(ril);
	data->vendor = vendor;

	ofono_sms_set_data(sms, data);

	/*
	 * TODO: analyze if capability check is needed
	 * and/or timer should be adjusted.
	 *
	 * ofono_sms_register() needs to be called after
	 * the driver has been set in ofono_sms_create(), which
	 * calls this function.  Most other drivers make some
	 * kind of capabilities query to the modem, and then
	 * call register in the callback; we use a timer instead.
	 */
	data->timer_id = g_timeout_add_seconds(2, ril_delayed_register, sms);

	return 0;
}

static void ril_sms_remove(struct ofono_sms *sms)
{
	struct sms_data *data = ofono_sms_get_data(sms);

	DBG("");

	if (data->timer_id > 0)
		g_source_remove(data->timer_id);

	g_ril_unref(data->ril);
	g_free(data);

	ofono_sms_set_data(sms, NULL);
}

static struct ofono_sms_driver driver = {
	.name		= "rilmodem",
	.probe		= ril_sms_probe,
	.remove		= ril_sms_remove,
	.sca_query	= ril_csca_query,
	.sca_set	= ril_csca_set,
	.submit		= ril_cmgs,
	.bearer_query	= NULL,          /* FIXME: needs investigation. */
	.bearer_set	= NULL,
};

void ril_sms_init(void)
{
	DBG("");
	if (ofono_sms_driver_register(&driver))
		DBG("ofono_sms_driver_register failed!");
}

void ril_sms_exit(void)
{
	DBG("");
	ofono_sms_driver_unregister(&driver);
}
