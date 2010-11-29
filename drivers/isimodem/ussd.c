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
#include <sys/uio.h>
#include <errno.h>

#include <glib.h>

#include <gisi/client.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/ussd.h>

#include "smsutil.h"
#include "util.h"

#include "isimodem.h"
#include "isiutil.h"
#include "ss.h"
#include "debug.h"

struct ussd_data {
	GIsiClient *client;
	int mt_session;
};

static void ussd_notify_ack(struct ussd_data *ud)
{
	const unsigned char msg[] = {
		SS_GSM_USSD_SEND_REQ,
		SS_GSM_USSD_NOTIFY,
		0x00		/* subblock count */
	};

	g_isi_send(ud->client, msg, sizeof(msg), SS_TIMEOUT, NULL, NULL, NULL);
}

static void ussd_ind_cb(GIsiClient *client,
			const void *restrict data, size_t len,
			uint16_t object, void *opaque)
{
	const unsigned char *msg = data;
	struct ofono_ussd *ussd = opaque;
	struct ussd_data *ud = ofono_ussd_get_data(ussd);
	int dcs;
	int type;
	size_t ussdlen;
	int status;

	if (!msg || len < 4 || msg[0] != SS_GSM_USSD_RECEIVE_IND)
		return;

	dcs = msg[1];
	type = msg[2];
	ussdlen = msg[3];

	if (len < 4 + ussdlen)
		ussdlen = len - 4;

	switch (type) {
	case 0:
		/* Nothing - this is response to NOTIFY_ACK REQ */
		return;

	case SS_GSM_USSD_MT_REPLY:
		/* This never happens, but.. */
		status = OFONO_USSD_STATUS_LOCAL_CLIENT_RESPONDED;
		break;

	case SS_GSM_USSD_COMMAND:
		if (ud->mt_session)
			/* Ignore, we get SS_GSM_USSD_REQUEST, too */
			return;
		status = OFONO_USSD_STATUS_ACTION_REQUIRED;
		break;

	case SS_GSM_USSD_NOTIFY:
		status = OFONO_USSD_STATUS_NOTIFY;
		ussd_notify_ack(ud);
		break;

	case SS_GSM_USSD_END:
		status = OFONO_USSD_STATUS_TERMINATED;
		ud->mt_session = 0;
		break;

	case SS_GSM_USSD_REQUEST:
		ud->mt_session = 1;
		status = OFONO_USSD_STATUS_ACTION_REQUIRED;
		break;

	default:
		status = OFONO_USSD_STATUS_NOT_SUPPORTED;
	}

	DBG("type: %u %s, dcs: 0x%02x, len: %zu",
		type, ss_ussd_type_name(type), dcs, ussdlen);

	ofono_ussd_notify(ussd, status, dcs, msg + 4, ussdlen);
}

static gboolean ussd_send_resp_cb(GIsiClient *client,
					const void *restrict data, size_t len,
					uint16_t object, void *opaque)
{
	const unsigned char *msg = data;
	struct isi_cb_data *cbd = opaque;
	ofono_ussd_cb_t cb = cbd->cb;

	if (!msg) {
		DBG("ISI client error: %d", g_isi_client_error(client));
		goto error;
	}

	if (len < 3)
		return FALSE;

	if (msg[0] == SS_SERVICE_FAILED_RESP)
		goto error;

	if (msg[0] != SS_GSM_USSD_SEND_RESP)
		return FALSE;

	CALLBACK_WITH_SUCCESS(cb, cbd->data);

	return TRUE;

error:
	CALLBACK_WITH_FAILURE(cb, cbd->data);
	return TRUE;

}

static void isi_request(struct ofono_ussd *ussd, int dcs,
			const unsigned char *pdu, int len,
			ofono_ussd_cb_t cb, void *data)
{
	struct ussd_data *ud = ofono_ussd_get_data(ussd);
	struct isi_cb_data *cbd = isi_cb_data_new(ussd, cb, data);
	const uint8_t msg[] = {
		SS_GSM_USSD_SEND_REQ,
		ud->mt_session
		? SS_GSM_USSD_MT_REPLY
		: SS_GSM_USSD_COMMAND,
		0x01,		/* subblock count */
		SS_GSM_USSD_STRING,
		4 + len + 3,	/* subblock length */
		dcs,		/* DCS */
		len,		/* string length */
		/* USSD string goes here */
	};
	const struct iovec iov[2] = {
		{ (uint8_t *)msg, sizeof(msg) },
		{ (uint8_t *)pdu, len }
	};

	if (!cbd || !ud)
		goto error;

	if (g_isi_vsend(ud->client, iov, 2, SS_TIMEOUT,
			ussd_send_resp_cb, cbd, g_free))
		return;

error:
	CALLBACK_WITH_FAILURE(cb, data);
	g_free(cbd);
}

static void isi_cancel(struct ofono_ussd *ussd,
				ofono_ussd_cb_t cb, void *data)
{
	struct ussd_data *ud = ofono_ussd_get_data(ussd);
	struct isi_cb_data *cbd = isi_cb_data_new(ussd, cb, data);

	const unsigned char msg[] = {
		SS_GSM_USSD_SEND_REQ,
		SS_GSM_USSD_END,
		0x00		/* subblock count */
	};

	if (!cbd || !ud)
		goto error;

	if (g_isi_send(ud->client, msg, sizeof(msg), SS_TIMEOUT,
			ussd_send_resp_cb, cbd, g_free))
		return;

error:
	CALLBACK_WITH_FAILURE(cb, data);
	g_free(cbd);
}

static gboolean isi_ussd_register(gpointer user)
{
	struct ofono_ussd *ussd = user;
	struct ussd_data *ud = ofono_ussd_get_data(ussd);

	const char *debug = getenv("OFONO_ISI_DEBUG");

	if (debug && (strcmp(debug, "all") == 0 || strcmp(debug, "ussd") == 0))
		g_isi_client_set_debug(ud->client, ss_debug, NULL);

	g_isi_subscribe(ud->client, SS_GSM_USSD_RECEIVE_IND, ussd_ind_cb, ussd);
	ofono_ussd_register(ussd);

	return FALSE;
}

static void ussd_reachable_cb(GIsiClient *client,
				gboolean alive, uint16_t object,
				void *opaque)
{
	struct ofono_ussd *ussd = opaque;

	if (!alive) {
		DBG("Unable to bootstrap ussd driver");
		return;
	}

	DBG("%s (v%03d.%03d) reachable",
		pn_resource_name(g_isi_client_resource(client)),
		g_isi_version_major(client),
		g_isi_version_minor(client));

	g_idle_add(isi_ussd_register, ussd);
}

static int isi_ussd_probe(struct ofono_ussd *ussd, unsigned int vendor,
				void *user)
{
	GIsiModem *idx = user;
	struct ussd_data *ud = g_try_new0(struct ussd_data, 1);

	if (!ud)
		return -ENOMEM;

	ud->client = g_isi_client_create(idx, PN_SS);
	if (!ud->client)
		return -ENOMEM;

	ofono_ussd_set_data(ussd, ud);
	g_isi_verify(ud->client, ussd_reachable_cb, ussd);

	return 0;
}

static void isi_ussd_remove(struct ofono_ussd *ussd)
{
	struct ussd_data *data = ofono_ussd_get_data(ussd);

	if (!data)
		return;

	ofono_ussd_set_data(ussd, NULL);
	g_isi_client_destroy(data->client);
	g_free(data);
}

static struct ofono_ussd_driver driver = {
	.name			= "isimodem",
	.probe			= isi_ussd_probe,
	.remove			= isi_ussd_remove,
	.request		= isi_request,
	.cancel			= isi_cancel
};

void isi_ussd_init()
{
	ofono_ussd_driver_register(&driver);
}

void isi_ussd_exit()
{
	ofono_ussd_driver_unregister(&driver);
}
