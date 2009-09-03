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

#include <glib.h>

#define OFONO_API_SUBJECT_TO_CHANGE

int __ofono_manager_init();
void __ofono_manager_cleanup();

const char **__ofono_modem_get_list();

#include <ofono/log.h>

int __ofono_log_init(gboolean detach, gboolean debug);
void __ofono_log_cleanup(void);

void __ofono_toggle_debug(void);

#include <ofono/dbus.h>

int __ofono_dbus_init(DBusConnection *conn);
void __ofono_dbus_cleanup(void);

DBusMessage *__ofono_error_invalid_args(DBusMessage *msg);
DBusMessage *__ofono_error_invalid_format(DBusMessage *msg);
DBusMessage *__ofono_error_not_implemented(DBusMessage *msg);
DBusMessage *__ofono_error_failed(DBusMessage *msg);
DBusMessage *__ofono_error_busy(DBusMessage *msg);
DBusMessage *__ofono_error_not_found(DBusMessage *msg);
DBusMessage *__ofono_error_not_active(DBusMessage *msg);
DBusMessage *__ofono_error_not_supported(DBusMessage *msg);
DBusMessage *__ofono_error_timed_out(DBusMessage *msg);
DBusMessage *__ofono_error_sim_not_ready(DBusMessage *msg);

void __ofono_dbus_pending_reply(DBusMessage **msg, DBusMessage *reply);

gboolean __ofono_dbus_valid_object_path(const char *path);

#include <ofono/types.h>

#include <ofono/plugin.h>

int __ofono_plugin_init(const char *pattern, const char *exclude);
void __ofono_plugin_cleanup(void);

#include <ofono/modem.h>

unsigned int __ofono_modem_alloc_callid(struct ofono_modem *modem);
void __ofono_modem_release_callid(struct ofono_modem *modem, int id);

struct ofono_atom;

enum ofono_atom_type {
	OFONO_ATOM_TYPE_DEVINFO = 0,
	OFONO_ATOM_TYPE_CALL_BARRING = 1,
	OFONO_ATOM_TYPE_CALL_FORWARDING = 2,
	OFONO_ATOM_TYPE_CALL_METER = 3,
	OFONO_ATOM_TYPE_CALL_SETTINGS = 4,
	OFONO_ATOM_TYPE_NETREG = 5,
	OFONO_ATOM_TYPE_PHONEBOOK = 6,
	OFONO_ATOM_TYPE_SMS = 7,
	OFONO_ATOM_TYPE_SIM = 8,
	OFONO_ATOM_TYPE_USSD = 9,
	OFONO_ATOM_TYPE_VOICECALL = 10,
	OFONO_ATOM_TYPE_HISTORY = 11,
	OFONO_ATOM_TYPE_SSN = 12,
	OFONO_ATOM_TYPE_MESSAGE_WAITING = 13,
};

enum ofono_atom_watch_condition {
	OFONO_ATOM_WATCH_CONDITION_REGISTERED,
	OFONO_ATOM_WATCH_CONDITION_UNREGISTERED
};

typedef void (*ofono_atom_watch_func)(struct ofono_atom *atom,
					enum ofono_atom_watch_condition cond,
					void *data);

typedef void (*ofono_atom_func)(struct ofono_atom *atom, void *data);

struct ofono_atom *__ofono_modem_add_atom(struct ofono_modem *modem,
					enum ofono_atom_type type,
					void (*destruct)(struct ofono_atom *),
					void *data);

struct ofono_atom *__ofono_modem_find_atom(struct ofono_modem *modem,
						enum ofono_atom_type type);

void __ofono_modem_foreach_atom(struct ofono_modem *modem,
				enum ofono_atom_type type,
				ofono_atom_func callback, void *data);

void *__ofono_atom_get_data(struct ofono_atom *atom);
const char *__ofono_atom_get_path(struct ofono_atom *atom);
struct ofono_modem *__ofono_atom_get_modem(struct ofono_atom *atom);

void __ofono_atom_register(struct ofono_atom *atom,
				void (*unregister)(struct ofono_atom *));
void __ofono_atom_unregister(struct ofono_atom *atom);

gboolean __ofono_atom_get_registered(struct ofono_atom *atom);

int __ofono_modem_add_atom_watch(struct ofono_modem *modem,
					enum ofono_atom_type type,
					ofono_atom_watch_func notify,
					void *data, ofono_destroy_func destroy);
gboolean __ofono_modem_remove_atom_watch(struct ofono_modem *modem, int id);

void __ofono_atom_free(struct ofono_atom *atom);

#include <ofono/call-barring.h>
#include <ofono/call-forwarding.h>
#include <ofono/call-meter.h>
#include <ofono/call-settings.h>
#include <ofono/devinfo.h>
#include <ofono/phonebook.h>
#include <ofono/sms.h>
#include <ofono/sim.h>
#include <ofono/voicecall.h>

#include <ofono/ssn.h>

typedef void (*ofono_ssn_mo_notify_cb)(int index, void *user);
typedef void (*ofono_ssn_mt_notify_cb)(int index,
					const struct ofono_phone_number *ph,
					void *user);

unsigned int __ofono_ssn_mo_watch_add(struct ofono_ssn *ssn, int code1,
					ofono_ssn_mo_notify_cb cb, void *user,
					ofono_destroy_func destroy);
gboolean __ofono_ssn_mo_watch_remove(struct ofono_ssn *ssn, int id);

unsigned int __ofono_ssn_mt_watch_add(struct ofono_ssn *ssn, int code2,
					ofono_ssn_mt_notify_cb cb, void *user,
					ofono_destroy_func destroy);
gboolean __ofono_ssn_mt_watch_remove(struct ofono_ssn *ssn, int id);

#include <ofono/ussd.h>

typedef gboolean (*ofono_ussd_ssc_cb_t)(int type,
					const char *sc,
					const char *sia, const char *sib,
					const char *sic, const char *dn,
					DBusMessage *msg, void *data);

typedef gboolean (*ofono_ussd_passwd_cb_t)(const char *sc,
					const char *old, const char *new,
					DBusMessage *msg, void *data);

gboolean __ofono_ussd_ssc_register(struct ofono_ussd *ussd, const char *sc,
					ofono_ussd_ssc_cb_t cb, void *data,
					ofono_destroy_func destroy);
void __ofono_ussd_ssc_unregister(struct ofono_ussd *ussd, const char *sc);

gboolean __ofono_ussd_passwd_register(struct ofono_ussd *ussd, const char *sc,
					ofono_ussd_passwd_cb_t cb, void *data,
					ofono_destroy_func destroy);
void __ofono_ussd_passwd_unregister(struct ofono_ussd *ussd, const char *sc);

#include <ofono/netreg.h>

#include <ofono/history.h>

void __ofono_history_probe_drivers(struct ofono_modem *modem);

void __ofono_history_call_ended(struct ofono_modem *modem,
				const struct ofono_call *call,
				time_t start, time_t end);

void __ofono_history_call_missed(struct ofono_modem *modem,
				const struct ofono_call *call, time_t when);

#include <ofono/message-waiting.h>

struct sms;

void __ofono_message_waiting_mwi(struct ofono_message_waiting *mw,
				struct sms *sms, gboolean *out_discard);
