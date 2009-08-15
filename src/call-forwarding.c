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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <glib.h>
#include <gdbus.h>

#include "ofono.h"

#include "driver.h"
#include "common.h"
#include "ussd.h"

#define CALL_FORWARDING_INTERFACE "org.ofono.CallForwarding"

#define CALL_FORWARDING_FLAG_CACHED 0x1

/* According to 27.007 Spec */
#define DEFAULT_NO_REPLY_TIMEOUT 20

struct call_forwarding_data {
	struct ofono_call_forwarding_ops *ops;
	GSList *cf_conditions[4];
	int flags;
	DBusMessage *pending;
	int query_next;
	int query_end;
	struct cf_ss_request *ss_req;
};

static void get_query_next_cf_cond(struct ofono_modem *modem);
static void set_query_next_cf_cond(struct ofono_modem *modem);
static void ss_set_query_next_cf_cond(struct ofono_modem *modem);
static void cf_unregister_ss_controls(struct ofono_modem *modem);

struct cf_ss_request {
	int ss_type;
	int cf_type;
	int cls;
	GSList *cf_list[4];
};

static gint cf_condition_compare(gconstpointer a, gconstpointer b)
{
	const struct ofono_cf_condition *ca = a;
	const struct ofono_cf_condition *cb = b;

	if (ca->cls < cb->cls)
		return -1;

	if (ca->cls > cb->cls)
		return 1;

	return 0;
}

static gint cf_condition_find_with_cls(gconstpointer a, gconstpointer b)
{
	const struct ofono_cf_condition *c = a;
	int cls = GPOINTER_TO_INT(b);

	if (c->cls < cls)
		return -1;

	if (c->cls > cls)
		return 1;

	return 0;
}

static int cf_find_timeout(GSList *cf_list, int cls)
{
	GSList *l;
	struct ofono_cf_condition *c;

	l = g_slist_find_custom(cf_list, GINT_TO_POINTER(cls),
		cf_condition_find_with_cls);

	if (!l)
		return DEFAULT_NO_REPLY_TIMEOUT;

	c = l->data;

	return c->time;
}

static void cf_cond_list_print(GSList *list)
{
	GSList *l;
	struct ofono_cf_condition *cond;

	for (l = list; l; l = l->next) {
		cond = l->data;

		ofono_debug("CF Condition status: %d, class: %d, number: %s,"
			" number_type: %d, time: %d",
			cond->status, cond->cls, cond->phone_number.number,
			cond->phone_number.type, cond->time);
	}
}

static GSList *cf_cond_list_create(int total,
					const struct ofono_cf_condition *list)
{
	GSList *l = NULL;
	int i;
	int j;
	struct ofono_cf_condition *cond;

	/* Specification is not really clear how the results are reported,
	 * so assume both multiple list items & compound values of class
	 * are possible
	 */
	for (i = 0; i < total; i++) {
		for (j = 1; j <= BEARER_CLASS_PAD; j = j << 1) {
			if (!(list[i].cls & j))
				continue;

			if (list[i].status == 0)
				continue;

			cond = g_try_new0(struct ofono_cf_condition, 1);
			if (!cond)
				continue;

			memcpy(cond, &list[i], sizeof(struct ofono_cf_condition));
			cond->cls = j;

			l = g_slist_insert_sorted(l, cond,
							cf_condition_compare);
		}
	}

	return l;
}

static inline void cf_list_clear(GSList *cf_list)
{
	GSList *l;

	for (l = cf_list; l; l = l->next)
		g_free(l->data);

	g_slist_free(cf_list);
}

static inline void cf_clear_all(struct call_forwarding_data *cf)
{
	int i;

	for (i = 0; i < 4; i++) {
		cf_list_clear(cf->cf_conditions[i]);
		cf->cf_conditions[i] = NULL;
	}
}

static struct call_forwarding_data *call_forwarding_create()
{
	struct call_forwarding_data *r;

	r = g_try_new0(struct call_forwarding_data, 1);

	if (!r)
		return r;

	return r;
}

static void call_forwarding_destroy(gpointer data)
{
	struct ofono_modem *modem = data;
	struct call_forwarding_data *cf = modem->call_forwarding;

	cf_clear_all(cf);

	cf_unregister_ss_controls(modem);

	g_free(cf);
}

static const char *cf_type_lut[] = {
	"Unconditional",
	"Busy",
	"NoReply",
	"NotReachable",
	"All",
	"AllConditional"
};

static void set_new_cond_list(struct ofono_modem *modem, int type, GSList *list)
{
	struct call_forwarding_data *cf = modem->call_forwarding;
	GSList *old = cf->cf_conditions[type];
	DBusConnection *conn = ofono_dbus_get_connection();
	GSList *l;
	GSList *o;
	struct ofono_cf_condition *lc;
	struct ofono_cf_condition *oc;
	const char *number;
	dbus_uint16_t timeout;
	char attr[64];
	char tattr[64];

	for (l = list; l; l = l->next) {
		lc = l->data;

		/* New condition lists might have attributes we don't care about
		 * triggered by e.g. ss control magic strings just skip them
		 * here.  For now we only support Voice, although Fax & all Data
		 * basic services are applicable as well.
		 */
		if (lc->cls > BEARER_CLASS_VOICE)
			continue;

		timeout = lc->time;
		number = phone_number_to_string(&lc->phone_number);

		sprintf(attr, "%s%s", bearer_class_to_string(lc->cls),
					cf_type_lut[type]);

		if (type == CALL_FORWARDING_TYPE_NO_REPLY)
			sprintf(tattr, "%sTimeout", attr);

		o = g_slist_find_custom(old, GINT_TO_POINTER(lc->cls),
					cf_condition_find_with_cls);

		if (o) { /* On the old list, must be active */
			oc = o->data;

			if (oc->phone_number.type != lc->phone_number.type ||
				strcmp(oc->phone_number.number,
					lc->phone_number.number))
				ofono_dbus_signal_property_changed(conn,
						modem->path,
						CALL_FORWARDING_INTERFACE,
						attr, DBUS_TYPE_STRING,
						&number);

			if (type == CALL_FORWARDING_TYPE_NO_REPLY &&
				oc->time != lc->time)
				ofono_dbus_signal_property_changed(conn,
						modem->path,
						CALL_FORWARDING_INTERFACE,
						tattr, DBUS_TYPE_UINT16,
						&timeout);

			/* Remove from the old list */
			g_free(o->data);
			old = g_slist_remove(old, o->data);
		} else {
			number = phone_number_to_string(&lc->phone_number);

			ofono_dbus_signal_property_changed(conn, modem->path,
						CALL_FORWARDING_INTERFACE,
						attr, DBUS_TYPE_STRING,
						&number);

			if (type == CALL_FORWARDING_TYPE_NO_REPLY &&
				lc->time != DEFAULT_NO_REPLY_TIMEOUT)
				ofono_dbus_signal_property_changed(conn,
						modem->path,
						CALL_FORWARDING_INTERFACE,
						tattr, DBUS_TYPE_UINT16,
						&timeout);
		}
	}

	timeout = DEFAULT_NO_REPLY_TIMEOUT;
	number = "";

	for (o = old; o; o = o->next) {
		oc = o->data;

		sprintf(attr, "%s%s", bearer_class_to_string(oc->cls),
					cf_type_lut[type]);

		if (type == CALL_FORWARDING_TYPE_NO_REPLY)
			sprintf(tattr, "%sTimeout", attr);

		ofono_dbus_signal_property_changed(conn, modem->path,
					CALL_FORWARDING_INTERFACE, attr,
					DBUS_TYPE_STRING, &number);

		if (type == CALL_FORWARDING_TYPE_NO_REPLY &&
			oc->time != DEFAULT_NO_REPLY_TIMEOUT)
			ofono_dbus_signal_property_changed(conn, modem->path,
						CALL_FORWARDING_INTERFACE,
						tattr, DBUS_TYPE_UINT16,
						&timeout);
	}

	cf_list_clear(old);
	cf->cf_conditions[type] = list;
}

static inline void property_append_cf_condition(DBusMessageIter *dict, int cls,
						const char *postfix,
						const char *value,
						dbus_uint16_t timeout)
{
	char attr[64];
	char tattr[64];
	int addt = !strcmp(postfix, "NoReply");

	sprintf(attr, "%s%s", bearer_class_to_string(cls), postfix);

	if (addt)
		sprintf(tattr, "%s%sTimeout", bearer_class_to_string(cls),
			postfix);

	ofono_dbus_dict_append(dict, attr, DBUS_TYPE_STRING, &value);

	if (addt)
		ofono_dbus_dict_append(dict, tattr, DBUS_TYPE_UINT16, &timeout);
}

static void property_append_cf_conditions(DBusMessageIter *dict,
						GSList *cf_list, int mask,
						const char *postfix)
{
	GSList *l;
	int i;
	struct ofono_cf_condition *cf;
	const char *number;

	for (i = 1, l = cf_list; i <= BEARER_CLASS_PAD; i = i << 1) {
		if (!(mask & i))
			continue;

		while (l && (cf = l->data) && (cf->cls < i))
				l = l->next;

		if (!l || cf->cls != i) {
			property_append_cf_condition(dict, i, postfix, "",
						DEFAULT_NO_REPLY_TIMEOUT);
			continue;
		}

		number = phone_number_to_string(&cf->phone_number);

		property_append_cf_condition(dict, i, postfix, number,
						cf->time);
	}
}

static DBusMessage *cf_get_properties_reply(DBusMessage *msg,
						struct call_forwarding_data *cf)
{
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;
	int i;

	reply = dbus_message_new_method_return(msg);

	if (!reply)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
						&dict);

	for (i = 0; i < 4; i++)
		property_append_cf_conditions(&dict, cf->cf_conditions[i],
						BEARER_CLASS_VOICE,
						cf_type_lut[i]);

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static void get_query_cf_callback(const struct ofono_error *error, int total,
					const struct ofono_cf_condition *list,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_forwarding_data *cf = modem->call_forwarding;

	if (error->type == OFONO_ERROR_TYPE_NO_ERROR) {
		GSList *l;
		l = cf_cond_list_create(total, list);
		set_new_cond_list(modem, cf->query_next, l);

		ofono_debug("%s conditions:", cf_type_lut[cf->query_next]);
		cf_cond_list_print(l);

		if (cf->query_next == CALL_FORWARDING_TYPE_NOT_REACHABLE)
			cf->flags |= CALL_FORWARDING_FLAG_CACHED;
	}

	if (cf->query_next == CALL_FORWARDING_TYPE_NOT_REACHABLE) {
		DBusMessage *reply = cf_get_properties_reply(cf->pending, cf);
		__ofono_dbus_pending_reply(&cf->pending, reply);
		return;
	}

	cf->query_next++;
	get_query_next_cf_cond(modem);
}

static void get_query_next_cf_cond(struct ofono_modem *modem)
{
	struct call_forwarding_data *cf = modem->call_forwarding;

	cf->ops->query(modem, cf->query_next, BEARER_CLASS_DEFAULT,
			get_query_cf_callback, modem);
}

static DBusMessage *cf_get_properties(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_forwarding_data *cf = modem->call_forwarding;

	if (cf->flags & CALL_FORWARDING_FLAG_CACHED)
		return cf_get_properties_reply(msg, cf);

	if (!cf->ops->query)
		return __ofono_error_not_implemented(msg);

	if (cf->pending)
		return __ofono_error_busy(msg);

	cf->pending = dbus_message_ref(msg);
	cf->query_next = 0;

	get_query_next_cf_cond(modem);

	return NULL;
}

static gboolean cf_condition_enabled_property(struct call_forwarding_data *cf,
			const char *property, int *out_type, int *out_cls)
{
	int i;
	int j;
	int len;
	const char *prefix;

	for (i = 1; i <= BEARER_CLASS_VOICE; i = i << 1) {
		prefix = bearer_class_to_string(i);

		len = strlen(prefix);

		if (strncmp(property, prefix, len))
			continue;

		/* We check the 4 call forwarding types, e.g.
		 * unconditional, busy, no reply, not reachable
		 */
		for (j = 0; j < 4; j++)
			if (!strcmp(property+len, cf_type_lut[j])) {
				*out_type = j;
				*out_cls = i;
				return TRUE;
			}
	}

	return FALSE;
}

static gboolean cf_condition_timeout_property(const char *property,
						int *out_cls)
{
	int i;
	int len;
	const char *prefix;

	for (i = 1; i <= BEARER_CLASS_VOICE; i = i << 1) {
		prefix = bearer_class_to_string(i);

		len = strlen(prefix);

		if (strncmp(property, prefix, len))
			continue;

		if (!strcmp(property+len, "NoReplyTimeout")) {
			*out_cls = i;
			return TRUE;
		}
	}

	return FALSE;
}

static void set_query_cf_callback(const struct ofono_error *error, int total,
					const struct ofono_cf_condition *list,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_forwarding_data *cf = modem->call_forwarding;
	GSList *l;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_error("Setting succeeded, but query failed");
		cf->flags &= ~CALL_FORWARDING_FLAG_CACHED;
		reply = __ofono_error_failed(cf->pending);
		__ofono_dbus_pending_reply(&cf->pending, reply);
		return;
	}

	if (cf->query_next == cf->query_end) {
		reply = dbus_message_new_method_return(cf->pending);
		__ofono_dbus_pending_reply(&cf->pending, reply);
	} 

	l = cf_cond_list_create(total, list);
	set_new_cond_list(modem, cf->query_next, l);

	ofono_debug("%s conditions:", cf_type_lut[cf->query_next]);
	cf_cond_list_print(l);

	if (cf->query_next != cf->query_end) {
		cf->query_next++;
		set_query_next_cf_cond(modem);
	}
}

static void set_query_next_cf_cond(struct ofono_modem *modem)
{
	struct call_forwarding_data *cf = modem->call_forwarding;

	cf->ops->query(modem, cf->query_next, BEARER_CLASS_DEFAULT,
			set_query_cf_callback, modem);
}

static void set_property_callback(const struct ofono_error *error, void *data)
{
	struct ofono_modem *modem = data;
	struct call_forwarding_data *cf = modem->call_forwarding;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_debug("Error occurred during set/erasure");
		__ofono_dbus_pending_reply(&cf->pending,
					__ofono_error_failed(cf->pending));
		return;
	}

	/* Successfully set, query the entire set just in case */
	set_query_next_cf_cond(modem);
}

static DBusMessage *set_property_request(struct ofono_modem *modem,
						DBusMessage *msg,
						int type, int cls,
						struct ofono_phone_number *ph,
						int timeout)
{
	struct call_forwarding_data *cf = modem->call_forwarding;

	if (ph->number[0] != '\0' && cf->ops->registration == NULL)
		return __ofono_error_not_implemented(msg);

	if (ph->number[0] == '\0' && cf->ops->erasure == NULL)
		return __ofono_error_not_implemented(msg);

	cf->pending = dbus_message_ref(msg);
	cf->query_next = type;
	cf->query_end = type;

	ofono_debug("Farming off request, will be erasure: %d",
			ph->number[0] == '\0');

	if (ph->number[0] != '\0')
		cf->ops->registration(modem, type, cls, ph, timeout,
					set_property_callback, modem);
	else
		cf->ops->erasure(modem, type, cls, set_property_callback, modem);

	return NULL;
}

static DBusMessage *cf_set_property(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_forwarding_data *cf = modem->call_forwarding;
	DBusMessageIter iter;
	DBusMessageIter var;
	const char *property;
	int cls;
	int type;

	if (cf->pending)
		return __ofono_error_busy(msg);

	if (!dbus_message_iter_init(msg, &iter))
		return __ofono_error_invalid_args(msg);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_get_basic(&iter, &property);
	dbus_message_iter_next(&iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_recurse(&iter, &var);

	if (cf_condition_timeout_property(property, &cls)) {
		dbus_uint16_t timeout;
		GSList *l;
		struct ofono_cf_condition *c;

		type = CALL_FORWARDING_TYPE_NO_REPLY;

		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_UINT16)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &timeout);

		if (timeout < 1 || timeout > 30)
			return __ofono_error_invalid_format(msg);

		l = g_slist_find_custom(cf->cf_conditions[type],
				GINT_TO_POINTER(cls),
				cf_condition_find_with_cls);

		if (!l)
			return __ofono_error_failed(msg);

		c = l->data;

		return set_property_request(modem, msg, type, cls,
						&c->phone_number, timeout);
	} else if (cf_condition_enabled_property(cf, property, &type, &cls)) {
		struct ofono_phone_number ph;
		const char *number;
		int timeout;

		ph.number[0] = '\0';
		ph.type = 129;

		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &number);

		if (strlen(number) > 0 && !valid_phone_number_format(number))
			return __ofono_error_invalid_format(msg);

		if (number[0] != '\0')
			string_to_phone_number(number, &ph);

		timeout = cf_find_timeout(cf->cf_conditions[type], cls);

		return set_property_request(modem, msg, type, cls, &ph,
						timeout);
	}

	return __ofono_error_invalid_args(msg);
}

static void disable_conditional_callback(const struct ofono_error *error,
						void *data)
{
	struct ofono_modem *modem = data;
	struct call_forwarding_data *cf = modem->call_forwarding;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_debug("Error occurred during conditional erasure");

		__ofono_dbus_pending_reply(&cf->pending,
					__ofono_error_failed(cf->pending));
		return;
	}

	/* Query the three conditional cf types */
	cf->query_next = CALL_FORWARDING_TYPE_BUSY;
	cf->query_end = CALL_FORWARDING_TYPE_NOT_REACHABLE;
	set_query_next_cf_cond(modem);
}

static void disable_all_callback(const struct ofono_error *error, void *data)
{
	struct ofono_modem *modem = data;
	struct call_forwarding_data *cf = modem->call_forwarding;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_debug("Error occurred during erasure of all");

		__ofono_dbus_pending_reply(&cf->pending,
					__ofono_error_failed(cf->pending));
		return;
	}

	/* Query all cf types */
	cf->query_next = CALL_FORWARDING_TYPE_UNCONDITIONAL;
	cf->query_end = CALL_FORWARDING_TYPE_NOT_REACHABLE;
	set_query_next_cf_cond(modem);
}

static DBusMessage *cf_disable_all(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_forwarding_data *cf = modem->call_forwarding;
	const char *strtype;
	int type;

	if (cf->pending)
		return __ofono_error_busy(msg);

	if (!cf->ops->erasure)
		return __ofono_error_not_implemented(msg);

	if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &strtype,
					DBUS_TYPE_INVALID) == FALSE)
		return __ofono_error_invalid_args(msg);

	if (!strcmp(strtype, "all") || !strcmp(strtype, ""))
		type = CALL_FORWARDING_TYPE_ALL;
	else if (!strcmp(strtype, "conditional"))
		type = CALL_FORWARDING_TYPE_ALL_CONDITIONAL;
	else
		return __ofono_error_invalid_format(msg);

	cf->pending = dbus_message_ref(msg);

	if (type == CALL_FORWARDING_TYPE_ALL)
		cf->ops->erasure(modem, type, BEARER_CLASS_DEFAULT,
				disable_all_callback, modem);
	else
		cf->ops->erasure(modem, type, BEARER_CLASS_DEFAULT,
				disable_conditional_callback, modem);

	return NULL;
}

static GDBusMethodTable cf_methods[] = {
	{ "GetProperties",	"",	"a{sv}",	cf_get_properties,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "SetProperty",	"sv",	"",		cf_set_property,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ "DisableAll",		"s",	"",		cf_disable_all,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ }
};

static GDBusSignalTable cf_signals[] = {
	{ "PropertyChanged",	"sv" },
	{ }
};

static DBusMessage *cf_ss_control_reply(struct ofono_modem *modem,
					struct cf_ss_request *req)
{
	struct call_forwarding_data *cf = modem->call_forwarding;
	const char *context = "CallForwarding";
	const char *sig = "(ssa{sv})";
	const char *ss_type = ss_control_type_to_string(req->ss_type);
	const char *cf_type = cf_type_lut[req->cf_type];
	DBusMessageIter iter;
	DBusMessageIter variant;
	DBusMessageIter vstruct;
	DBusMessageIter dict;
	DBusMessage *reply;

	reply = dbus_message_new_method_return(cf->pending);

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &context);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, sig,
						&variant);

	dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL,
						&vstruct);

	dbus_message_iter_append_basic(&vstruct, DBUS_TYPE_STRING,
					&ss_type);

	dbus_message_iter_append_basic(&vstruct, DBUS_TYPE_STRING,
					&cf_type);

	dbus_message_iter_open_container(&vstruct, DBUS_TYPE_ARRAY,
				OFONO_PROPERTIES_ARRAY_SIGNATURE, &dict);

	if (req->cf_type == CALL_FORWARDING_TYPE_UNCONDITIONAL ||
		req->cf_type == CALL_FORWARDING_TYPE_ALL)
		property_append_cf_conditions(&dict,
			req->cf_list[CALL_FORWARDING_TYPE_UNCONDITIONAL],
			req->cls,
			cf_type_lut[CALL_FORWARDING_TYPE_UNCONDITIONAL]);

	if (req->cf_type == CALL_FORWARDING_TYPE_NO_REPLY ||
		req->cf_type == CALL_FORWARDING_TYPE_ALL ||
		req->cf_type == CALL_FORWARDING_TYPE_ALL_CONDITIONAL)
		property_append_cf_conditions(&dict,
			req->cf_list[CALL_FORWARDING_TYPE_NO_REPLY],
			req->cls, cf_type_lut[CALL_FORWARDING_TYPE_NO_REPLY]);

	if (req->cf_type == CALL_FORWARDING_TYPE_NOT_REACHABLE ||
		req->cf_type == CALL_FORWARDING_TYPE_ALL ||
		req->cf_type == CALL_FORWARDING_TYPE_ALL_CONDITIONAL)
		property_append_cf_conditions(&dict,
			req->cf_list[CALL_FORWARDING_TYPE_NOT_REACHABLE],
			req->cls,
			cf_type_lut[CALL_FORWARDING_TYPE_NOT_REACHABLE]);

	if (req->cf_type == CALL_FORWARDING_TYPE_BUSY ||
		req->cf_type == CALL_FORWARDING_TYPE_ALL ||
		req->cf_type == CALL_FORWARDING_TYPE_ALL_CONDITIONAL)
		property_append_cf_conditions(&dict,
			req->cf_list[CALL_FORWARDING_TYPE_BUSY],
			req->cls, cf_type_lut[CALL_FORWARDING_TYPE_BUSY]);

	dbus_message_iter_close_container(&vstruct, &dict);

	dbus_message_iter_close_container(&variant, &vstruct);

	dbus_message_iter_close_container(&iter, &variant);

	return reply;
}

static void ss_set_query_cf_callback(const struct ofono_error *error, int total,
					const struct ofono_cf_condition *list,
					void *data)
{
	struct ofono_modem *modem = data;
	struct call_forwarding_data *cf = modem->call_forwarding;
	GSList *l;
	DBusMessage *reply;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_error("Setting succeeded, but query failed");
		cf->flags &= ~CALL_FORWARDING_FLAG_CACHED;
		reply = __ofono_error_failed(cf->pending);
		__ofono_dbus_pending_reply(&cf->pending, reply);
		return;
	}

	l = cf_cond_list_create(total, list);
	ofono_debug("%s conditions:", cf_type_lut[cf->query_next]);
	cf_cond_list_print(l);

	cf->ss_req->cf_list[cf->query_next] = l;

	if (cf->query_next == cf->query_end) {
		reply = cf_ss_control_reply(modem, cf->ss_req);
		__ofono_dbus_pending_reply(&cf->pending, reply);
		g_free(cf->ss_req);
		cf->ss_req = NULL;
	}

	set_new_cond_list(modem, cf->query_next, l);

	if (cf->query_next != cf->query_end) {
		cf->query_next++;
		ss_set_query_next_cf_cond(modem);
	}
}

static void ss_set_query_next_cf_cond(struct ofono_modem *modem)
{
	struct call_forwarding_data *cf = modem->call_forwarding;

	cf->ops->query(modem, cf->query_next, BEARER_CLASS_DEFAULT,
			ss_set_query_cf_callback, modem);
}

static void cf_ss_control_callback(const struct ofono_error *error, void *data)
{
	struct ofono_modem *modem = data;
	struct call_forwarding_data *cf = modem->call_forwarding;

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR) {
		ofono_debug("Error occurred during cf ss control set/erasure");

		__ofono_dbus_pending_reply(&cf->pending,
					__ofono_error_failed(cf->pending));
		g_free(cf->ss_req);
		cf->ss_req = NULL;
		return;
	}

	ss_set_query_next_cf_cond(modem);
}

static gboolean cf_ss_control(struct ofono_modem *modem,
				enum ss_control_type type, const char *sc,
				const char *sia, const char *sib,
				const char *sic, const char *dn,
				DBusMessage *msg)
{
	struct call_forwarding_data *cf = modem->call_forwarding;
	DBusConnection *conn = ofono_dbus_get_connection();
	int cls = BEARER_CLASS_SS_DEFAULT;
	int timeout = DEFAULT_NO_REPLY_TIMEOUT;
	int cf_type;
	DBusMessage *reply;
	struct ofono_phone_number ph;
	void *operation = NULL;

	/* Before we do anything, make sure we're actually initialized */
	if (!cf)
		return FALSE;

	if (cf->pending) {
		reply = __ofono_error_busy(msg);
		g_dbus_send_message(conn, reply);

		return TRUE;
	}

	ofono_debug("Received call forwarding ss control request");

	ofono_debug("type: %d, sc: %s, sia: %s, sib: %s, sic: %s, dn: %s",
			type, sc, sia, sib, sic, dn);

	if (!strcmp(sc, "21"))
		cf_type = CALL_FORWARDING_TYPE_UNCONDITIONAL;
	else if (!strcmp(sc, "67"))
		cf_type = CALL_FORWARDING_TYPE_BUSY;
	else if (!strcmp(sc, "61"))
		cf_type = CALL_FORWARDING_TYPE_NO_REPLY;
	else if (!strcmp(sc, "62"))
		cf_type = CALL_FORWARDING_TYPE_NOT_REACHABLE;
	else if (!strcmp(sc, "002"))
		cf_type = CALL_FORWARDING_TYPE_ALL;
	else if (!strcmp(sc, "004"))
		cf_type = CALL_FORWARDING_TYPE_ALL_CONDITIONAL;
	else
		return FALSE;

	if (strlen(sia) &&
		(type == SS_CONTROL_TYPE_QUERY ||
		type == SS_CONTROL_TYPE_ERASURE ||
		type == SS_CONTROL_TYPE_DEACTIVATION))
		goto error;

	/* Activation / Registration is figured context specific according to
	 * 22.030 Section 6.5.2 "The UE shall determine from the context
	 * whether, an entry of a single *, activation or registration
	 * was intended."
	 */
	if (type == SS_CONTROL_TYPE_ACTIVATION && strlen(sia) > 0)
		type = SS_CONTROL_TYPE_REGISTRATION;

	if (type == SS_CONTROL_TYPE_REGISTRATION &&
		!valid_phone_number_format(sia))
		goto error;

	if (strlen(sib) > 0) {
		long service_code;
		char *end;

		service_code = strtoul(sib, &end, 10);

		if (end == sib || *end != '\0')
			goto error;

		cls = mmi_service_code_to_bearer_class(service_code);

		if (cls == 0)
			goto error;
	}

	if (strlen(sic) > 0) {
		char *end;

		if  (type != SS_CONTROL_TYPE_REGISTRATION)
			goto error;

		if (cf_type != CALL_FORWARDING_TYPE_ALL &&
			cf_type != CALL_FORWARDING_TYPE_ALL_CONDITIONAL &&
			cf_type != CALL_FORWARDING_TYPE_NO_REPLY)
			goto error;

		timeout = strtoul(sic, &end, 10);

		if (end == sic || *end != '\0')
			goto error;

		if (timeout < 1 || timeout > 30)
			goto error;
	}

	switch (type) {
	case SS_CONTROL_TYPE_REGISTRATION:
		operation = cf->ops->registration;
		break;
	case SS_CONTROL_TYPE_ACTIVATION:
		operation = cf->ops->activation;
		break;
	case SS_CONTROL_TYPE_DEACTIVATION:
		operation = cf->ops->deactivation;
		break;
	case SS_CONTROL_TYPE_ERASURE:
		operation = cf->ops->erasure;
		break;
	case SS_CONTROL_TYPE_QUERY:
		operation = cf->ops->query;
		break;
	}

	if (!operation) {
		reply = __ofono_error_not_implemented(msg);
		g_dbus_send_message(conn, reply);

		return TRUE;
	}

	cf->ss_req = g_try_new0(struct cf_ss_request, 1);

	if (!cf->ss_req) {
		reply = __ofono_error_failed(msg);
		g_dbus_send_message(conn, reply);

		return TRUE;
	}

	cf->ss_req->ss_type = type;
	cf->ss_req->cf_type = cf_type;
	cf->ss_req->cls = cls;

	cf->pending = dbus_message_ref(msg);

	switch (cf->ss_req->cf_type) {
	case CALL_FORWARDING_TYPE_ALL:
		cf->query_next = CALL_FORWARDING_TYPE_UNCONDITIONAL;
		cf->query_end = CALL_FORWARDING_TYPE_NOT_REACHABLE;
		break;
	case CALL_FORWARDING_TYPE_ALL_CONDITIONAL:
		cf->query_next = CALL_FORWARDING_TYPE_BUSY;
		cf->query_end = CALL_FORWARDING_TYPE_NOT_REACHABLE;
		break;
	default:
		cf->query_next = cf->ss_req->cf_type;
		cf->query_end = cf->ss_req->cf_type;
		break;
	}

	/* Some modems don't understand all classes very well, particularly
	 * the older models.  So if the bearer class is the default, we
	 * just use the more commonly understood value of 7 since BEARER_SMS
	 * is not applicable to CallForwarding conditions according to 22.004
	 * Annex A
	 */
	if (cls == BEARER_CLASS_SS_DEFAULT)
		cls = BEARER_CLASS_DEFAULT;

	switch (cf->ss_req->ss_type) {
	case SS_CONTROL_TYPE_REGISTRATION:
		string_to_phone_number(sia, &ph);
		cf->ops->registration(modem, cf_type, cls, &ph, timeout,
					cf_ss_control_callback, modem);
		break;
	case SS_CONTROL_TYPE_ACTIVATION:
		cf->ops->activation(modem, cf_type, cls, cf_ss_control_callback,
					modem);
		break;
	case SS_CONTROL_TYPE_DEACTIVATION:
		cf->ops->deactivation(modem, cf_type, cls,
					cf_ss_control_callback, modem);
		break;
	case SS_CONTROL_TYPE_ERASURE:
		cf->ops->erasure(modem, cf_type, cls, cf_ss_control_callback,
					modem);
		break;
	case SS_CONTROL_TYPE_QUERY:
		ss_set_query_next_cf_cond(modem);
		break;
	}

	return TRUE;

error:
	reply = __ofono_error_invalid_format(msg);
	g_dbus_send_message(conn, reply);
	return TRUE;
}

static void cf_register_ss_controls(struct ofono_modem *modem)
{
	ss_control_register(modem, "21", cf_ss_control);
	ss_control_register(modem, "67", cf_ss_control);
	ss_control_register(modem, "61", cf_ss_control);
	ss_control_register(modem, "62", cf_ss_control);

	ss_control_register(modem, "002", cf_ss_control);
	ss_control_register(modem, "004", cf_ss_control);
}

static void cf_unregister_ss_controls(struct ofono_modem *modem)
{
	ss_control_unregister(modem, "21", cf_ss_control);
	ss_control_unregister(modem, "67", cf_ss_control);
	ss_control_unregister(modem, "61", cf_ss_control);
	ss_control_unregister(modem, "62", cf_ss_control);

	ss_control_unregister(modem, "002", cf_ss_control);
	ss_control_unregister(modem, "004", cf_ss_control);
}

int ofono_call_forwarding_register(struct ofono_modem *modem,
				struct ofono_call_forwarding_ops *ops)
{
	DBusConnection *conn = ofono_dbus_get_connection();

	if (modem == NULL)
		return -1;

	if (ops == NULL)
		return -1;

	if (ops->query == NULL)
		return -1;

	modem->call_forwarding = call_forwarding_create();

	if (modem->call_forwarding == NULL)
		return -1;

	modem->call_forwarding->ops = ops;

	if (!g_dbus_register_interface(conn, modem->path,
					CALL_FORWARDING_INTERFACE,
					cf_methods, cf_signals, NULL,
					modem, call_forwarding_destroy)) {
		ofono_error("Could not register CallForwarding %s", modem->path);
		call_forwarding_destroy(modem);

		return -1;
	}

	ofono_debug("Registered call forwarding interface");

	cf_register_ss_controls(modem);

	ofono_modem_add_interface(modem, CALL_FORWARDING_INTERFACE);

	return 0;
}

void ofono_call_forwarding_unregister(struct ofono_modem *modem)
{
	struct call_forwarding_data *cf = modem->call_forwarding;
	DBusConnection *conn = ofono_dbus_get_connection();

	if (!cf)
		return;

	ofono_modem_remove_interface(modem, CALL_FORWARDING_INTERFACE);
	g_dbus_unregister_interface(conn, modem->path,
					CALL_FORWARDING_INTERFACE);

	modem->call_forwarding = NULL;
}
