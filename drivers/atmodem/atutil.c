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

#include <glib.h>
#include <gatchat.h>
#include <string.h>
#include <stdlib.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/log.h>
#include <ofono/types.h>

#include "atutil.h"
#include "vendor.h"

void decode_at_error(struct ofono_error *error, const char *final)
{
	if (!strcmp(final, "OK")) {
		error->type = OFONO_ERROR_TYPE_NO_ERROR;
		error->error = 0;
	} else if (g_str_has_prefix(final, "+CMS ERROR:")) {
		error->type = OFONO_ERROR_TYPE_CMS;
		error->error = strtol(&final[11], NULL, 0);
	} else if (g_str_has_prefix(final, "+CME ERROR:")) {
		error->type = OFONO_ERROR_TYPE_CME;
		error->error = strtol(&final[11], NULL, 0);
	} else {
		error->type = OFONO_ERROR_TYPE_FAILURE;
		error->error = 0;
	}
}

gint at_util_call_compare_by_status(gconstpointer a, gconstpointer b)
{
	const struct ofono_call *call = a;
	int status = GPOINTER_TO_INT(b);

	if (status != call->status)
		return 1;

	return 0;
}

gint at_util_call_compare_by_phone_number(gconstpointer a, gconstpointer b)
{
	const struct ofono_call *call = a;
	const struct ofono_phone_number *pb = b;

	return memcmp(&call->phone_number, pb,
				sizeof(struct ofono_phone_number));
}

gint at_util_call_compare_by_id(gconstpointer a, gconstpointer b)
{
	const struct ofono_call *call = a;
	unsigned int id = GPOINTER_TO_UINT(b);

	if (id < call->id)
		return -1;

	if (id > call->id)
		return 1;

	return 0;
}

gint at_util_call_compare(gconstpointer a, gconstpointer b)
{
	const struct ofono_call *ca = a;
	const struct ofono_call *cb = b;

	if (ca->id < cb->id)
		return -1;

	if (ca->id > cb->id)
		return 1;

	return 0;
}

GSList *at_util_parse_clcc(GAtResult *result)
{
	GAtResultIter iter;
	GSList *l = NULL;
	int id, dir, status, type;
	ofono_bool_t mpty;
	struct ofono_call *call;

	g_at_result_iter_init(&iter, result);

	while (g_at_result_iter_next(&iter, "+CLCC:")) {
		const char *str = "";
		int number_type = 129;

		if (!g_at_result_iter_next_number(&iter, &id))
			continue;

		if (!g_at_result_iter_next_number(&iter, &dir))
			continue;

		if (!g_at_result_iter_next_number(&iter, &status))
			continue;

		if (!g_at_result_iter_next_number(&iter, &type))
			continue;

		if (!g_at_result_iter_next_number(&iter, &mpty))
			continue;

		if (g_at_result_iter_next_string(&iter, &str))
			g_at_result_iter_next_number(&iter, &number_type);

		call = g_try_new0(struct ofono_call, 1);

		if (!call)
			break;

		call->id = id;
		call->direction = dir;
		call->status = status;
		call->type = type;
		call->mpty = mpty;
		strncpy(call->phone_number.number, str,
				OFONO_MAX_PHONE_NUMBER_LENGTH);
		call->phone_number.type = number_type;

		if (strlen(call->phone_number.number) > 0)
			call->clip_validity = 0;
		else
			call->clip_validity = 2;

		l = g_slist_insert_sorted(l, call, at_util_call_compare);
	}

	return l;
}

gboolean at_util_parse_reg_unsolicited(GAtResult *result, const char *prefix,
					int *status,
					int *lac, int *ci, int *tech,
					unsigned int vendor)
{
	GAtResultIter iter;
	int s;
	int l = -1, c = -1, t = -1;
	const char *str;

	g_at_result_iter_init(&iter, result);

	if (g_at_result_iter_next(&iter, prefix) == FALSE)
		return FALSE;

	if (g_at_result_iter_next_number(&iter, &s) == FALSE)
		return FALSE;

	/* Some firmware will report bogus lac/ci when unregistered */
	if (s != 1 && s != 5)
		goto out;

	switch (vendor) {
	case OFONO_VENDOR_HUAWEI:
	case OFONO_VENDOR_NOVATEL:
		if (g_at_result_iter_next_unquoted_string(&iter, &str) == TRUE)
			l = strtol(str, NULL, 16);
		else
			goto out;

		if (g_at_result_iter_next_unquoted_string(&iter, &str) == TRUE)
			c = strtol(str, NULL, 16);
		else
			goto out;

		break;
	default:
		if (g_at_result_iter_next_string(&iter, &str) == TRUE)
			l = strtol(str, NULL, 16);
		else
			goto out;

		if (g_at_result_iter_next_string(&iter, &str) == TRUE)
			c = strtol(str, NULL, 16);
		else
			goto out;
	}

	g_at_result_iter_next_number(&iter, &t);

out:
	if (status)
		*status = s;

	if (lac)
		*lac = l;

	if (ci)
		*ci = c;

	if (tech)
		*tech = t;

	return TRUE;
}

gboolean at_util_parse_reg(GAtResult *result, const char *prefix,
				int *mode, int *status,
				int *lac, int *ci, int *tech,
				unsigned int vendor)
{
	GAtResultIter iter;
	int m, s;
	int l = -1, c = -1, t = -1;
	const char *str;

	g_at_result_iter_init(&iter, result);

	while (g_at_result_iter_next(&iter, prefix)) {
		gboolean r;

		g_at_result_iter_next_number(&iter, &m);

		/* Sometimes we get an unsolicited CREG/CGREG here, skip it */
		if (g_at_result_iter_next_number(&iter, &s) == FALSE)
			continue;

		/* Some firmware will report bogus lac/ci when unregistered */
		if (s != 1 && s != 5)
			goto out;

		switch (vendor) {
		case OFONO_VENDOR_HUAWEI:
		case OFONO_VENDOR_NOVATEL:
			r = g_at_result_iter_next_unquoted_string(&iter, &str);

			if (r == TRUE)
				l = strtol(str, NULL, 16);
			else
				goto out;

			r = g_at_result_iter_next_unquoted_string(&iter, &str);

			if (r == TRUE)
				c = strtol(str, NULL, 16);
			else
				goto out;

			break;
		default:
			if (g_at_result_iter_next_string(&iter, &str) == TRUE)
				l = strtol(str, NULL, 16);
			else
				goto out;

			if (g_at_result_iter_next_string(&iter, &str) == TRUE)
				c = strtol(str, NULL, 16);
			else
				goto out;
		}

		g_at_result_iter_next_number(&iter, &t);

out:
		if (mode)
			*mode = m;

		if (status)
			*status = s;

		if (lac)
			*lac = l;

		if (ci)
			*ci = c;

		if (tech)
			*tech = t;

		return TRUE;
	}

	return FALSE;
}

gboolean at_util_parse_sms_index_delivery(GAtResult *result, const char *prefix,
						enum at_util_sms_store *out_st,
						int *out_index)
{
	GAtResultIter iter;
	const char *strstore;
	enum at_util_sms_store st;
	int index;

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, prefix))
		return FALSE;

	if (!g_at_result_iter_next_string(&iter, &strstore))
		return FALSE;

	if (g_str_equal(strstore, "ME"))
		st = AT_UTIL_SMS_STORE_ME;
	else if (g_str_equal(strstore, "SM"))
		st = AT_UTIL_SMS_STORE_SM;
	else if (g_str_equal(strstore, "SR"))
		st = AT_UTIL_SMS_STORE_SR;
	else if (g_str_equal(strstore, "BM"))
		st = AT_UTIL_SMS_STORE_BM;
	else
		return FALSE;

	if (!g_at_result_iter_next_number(&iter, &index))
		return FALSE;

	if (out_index)
		*out_index = index;

	if (out_st)
		*out_st = st;

	return TRUE;
}

static gboolean at_util_charset_string_to_charset(const char *str,
					enum at_util_charset *charset)
{
	if (!g_strcmp0(str, "GSM"))
		*charset = AT_UTIL_CHARSET_GSM;
	else if (!g_strcmp0(str, "HEX"))
		*charset = AT_UTIL_CHARSET_HEX;
	else if (!g_strcmp0(str, "IRA"))
		*charset = AT_UTIL_CHARSET_IRA;
	else if (!g_strcmp0(str, "PCCP437"))
		*charset = AT_UTIL_CHARSET_PCCP437;
	else if (!g_strcmp0(str, "PCDN"))
		*charset = AT_UTIL_CHARSET_PCDN;
	else if (!g_strcmp0(str, "UCS2"))
		*charset = AT_UTIL_CHARSET_UCS2;
	else if (!g_strcmp0(str, "UTF-8"))
		*charset = AT_UTIL_CHARSET_UTF8;
	else if (!g_strcmp0(str, "8859-1"))
		*charset = AT_UTIL_CHARSET_8859_1;
	else if (!g_strcmp0(str, "8859-2"))
		*charset = AT_UTIL_CHARSET_8859_2;
	else if (!g_strcmp0(str, "8859-3"))
		*charset = AT_UTIL_CHARSET_8859_3;
	else if (!g_strcmp0(str, "8859-4"))
		*charset = AT_UTIL_CHARSET_8859_4;
	else if (!g_strcmp0(str, "8859-5"))
		*charset = AT_UTIL_CHARSET_8859_5;
	else if (!g_strcmp0(str, "8859-6"))
		*charset = AT_UTIL_CHARSET_8859_6;
	else if (!g_strcmp0(str, "8859-C"))
		*charset = AT_UTIL_CHARSET_8859_C;
	else if (!g_strcmp0(str, "8859-A"))
		*charset = AT_UTIL_CHARSET_8859_A;
	else if (!g_strcmp0(str, "8859-G"))
		*charset = AT_UTIL_CHARSET_8859_G;
	else if (!g_strcmp0(str, "8859-H"))
		*charset = AT_UTIL_CHARSET_8859_H;
	else
		return FALSE;

	return TRUE;
}

gboolean at_util_parse_cscs_supported(GAtResult *result, int *supported)
{
	GAtResultIter iter;
	const char *str;
	enum at_util_charset charset;
	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CSCS:"))
		return FALSE;

	/* Some modems don't report CSCS in a proper list */
	g_at_result_iter_open_list(&iter);

	while (g_at_result_iter_next_string(&iter, &str)) {
		if (at_util_charset_string_to_charset(str, &charset))
			*supported |= charset;
	}

	g_at_result_iter_close_list(&iter);

	return TRUE;
}

gboolean at_util_parse_cscs_query(GAtResult *result,
				enum at_util_charset *charset)
{
	GAtResultIter iter;
	const char *str;

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CSCS:"))
		return FALSE;

	if (g_at_result_iter_next_string(&iter, &str))
		return at_util_charset_string_to_charset(str, charset);

	return FALSE;
}
