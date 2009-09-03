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
#include <errno.h>

#include <glib.h>
#include <gdbus.h>

#include "ofono.h"

#include "common.h"

static GSList *g_devinfo_drivers = NULL;
static GSList *g_driver_list = NULL;
static GSList *g_modem_list = NULL;

static int next_modem_id = 0;

enum ofono_property_type {
	OFONO_PROPERTY_TYPE_INVALID = 0,
	OFONO_PROPERTY_TYPE_STRING,
	OFONO_PROPERTY_TYPE_INTEGER,
};

struct ofono_modem {
	char		*path;
	GSList		*atoms;
	GSList		*atom_watches;
	int		next_atom_watch_id;
	GSList         	*interface_list;
	unsigned int	call_ids;
	DBusMessage	*pending;
	guint		interface_update;
	ofono_bool_t	powered;
	ofono_bool_t	powered_pending;
	ofono_bool_t	powered_persistent;
	guint		timeout;
	GHashTable	*properties;
	const struct ofono_modem_driver *driver;
	void		*driver_data;
	char		*driver_type;
};

struct ofono_devinfo {
	char *manufacturer;
	char *model;
	char *revision;
	char *serial;
	const struct ofono_devinfo_driver *driver;
	void *driver_data;
	struct ofono_atom *atom;
};

struct ofono_atom {
	enum ofono_atom_type type;
	void (*destruct)(struct ofono_atom *atom);
	void (*unregister)(struct ofono_atom *atom);
	void *data;
	struct ofono_modem *modem;
};

struct ofono_atom_watch {
	enum ofono_atom_type type;
	int id;
	ofono_atom_watch_func notify;
	ofono_destroy_func destroy;
	void *notify_data;
};

struct ofono_property {
	enum ofono_property_type type;
	void *value;
};

unsigned int __ofono_modem_alloc_callid(struct ofono_modem *modem)
{
	unsigned int i;

	for (i = 1; i < sizeof(modem->call_ids) * 8; i++) {
		if (modem->call_ids & (0x1 << i))
			continue;

		modem->call_ids |= (0x1 << i);
		return i;
	}

	return 0;
}

void __ofono_modem_release_callid(struct ofono_modem *modem, int id)
{
	modem->call_ids &= ~(0x1 << id);
}

void ofono_modem_set_data(struct ofono_modem *modem, void *data)
{
	if (modem == NULL)
		return;

	modem->driver_data = data;
}

void *ofono_modem_get_data(struct ofono_modem *modem)
{
	if (modem == NULL)
		return NULL;

	return modem->driver_data;
}

const char *ofono_modem_get_path(struct ofono_modem *modem)
{
	if (modem)
		return modem->path;

	return NULL;
}

struct ofono_atom *__ofono_modem_add_atom(struct ofono_modem *modem,
					enum ofono_atom_type type,
					void (*destruct)(struct ofono_atom *),
					void *data)
{
	struct ofono_atom *atom;

	if (modem == NULL)
		return NULL;

	atom = g_new0(struct ofono_atom, 1);

	atom->type = type;
	atom->destruct = destruct;
	atom->data = data;
	atom->modem = modem;

	modem->atoms = g_slist_prepend(modem->atoms, atom);

	return atom;
}

void *__ofono_atom_get_data(struct ofono_atom *atom)
{
	return atom->data;
}

const char *__ofono_atom_get_path(struct ofono_atom *atom)
{
	return atom->modem->path;
}

struct ofono_modem *__ofono_atom_get_modem(struct ofono_atom *atom)
{
	return atom->modem;
}

static void call_watches(struct ofono_atom *atom,
				enum ofono_atom_watch_condition cond)
{
	struct ofono_modem *modem = atom->modem;
	GSList *l;
	struct ofono_atom_watch *watch;

	for (l = modem->atom_watches; l; l = l->next) {
		watch = l->data;

		if (watch->type != atom->type)
			continue;

		watch->notify(atom, cond, watch->notify_data);
	}
}

void __ofono_atom_register(struct ofono_atom *atom,
			void (*unregister)(struct ofono_atom *))
{
	if (unregister == NULL)
		return;

	atom->unregister = unregister;

	call_watches(atom, OFONO_ATOM_WATCH_CONDITION_REGISTERED);
}

void __ofono_atom_unregister(struct ofono_atom *atom)
{
	if (atom->unregister == NULL)
		return;

	call_watches(atom, OFONO_ATOM_WATCH_CONDITION_UNREGISTERED);

	atom->unregister(atom);
}

gboolean __ofono_atom_get_registered(struct ofono_atom *atom)
{
	return atom->unregister ? TRUE : FALSE;
}

int __ofono_modem_add_atom_watch(struct ofono_modem *modem,
					enum ofono_atom_type type,
					ofono_atom_watch_func notify,
					void *data, ofono_destroy_func destroy)
{
	struct ofono_atom_watch *watch;

	if (notify == NULL)
		return 0;

	watch = g_new0(struct ofono_atom_watch, 1);

	watch->type = type;
	watch->id = ++modem->next_atom_watch_id;
	watch->notify = notify;
	watch->destroy = destroy;
	watch->notify_data = data;

	modem->atom_watches = g_slist_prepend(modem->atom_watches, watch);

	return watch->id;
}

gboolean __ofono_modem_remove_atom_watch(struct ofono_modem *modem, int id)
{
	struct ofono_atom_watch *watch;
	GSList *p;
	GSList *c;

	p = NULL;
	c = modem->atom_watches;

	while (c) {
		watch = c->data;

		if (watch->id != id) {
			p = c;
			c = c->next;
			continue;
		}

		if (p)
			p->next = c->next;
		else
			modem->atom_watches = c->next;

		if (watch->destroy)
			watch->destroy(watch->notify_data);

		g_free(watch);
		g_slist_free_1(c);

		return TRUE;
	}

	return FALSE;
}

static void remove_all_watches(struct ofono_modem *modem)
{
	struct ofono_atom_watch *watch;
	GSList *l;

	for (l = modem->atom_watches; l; l = l->next) {
		watch = l->data;

		if (watch->destroy)
			watch->destroy(watch->notify_data);

		g_free(watch);
	}

	g_slist_free(modem->atom_watches);
	modem->atom_watches = NULL;
}

struct ofono_atom *__ofono_modem_find_atom(struct ofono_modem *modem,
						enum ofono_atom_type type)
{
	GSList *l;
	struct ofono_atom *atom;

	if (modem == NULL)
		return NULL;

	for (l = modem->atoms; l; l = l->next) {
		atom = l->data;

		if (atom->type == type)
			return atom;
	}

	return NULL;
}

void __ofono_modem_foreach_atom(struct ofono_modem *modem,
				enum ofono_atom_type type,
				ofono_atom_func callback, void *data)
{
	GSList *l;
	struct ofono_atom *atom;

	if (modem == NULL)
		return;

	for (l = modem->atoms; l; l = l->next) {
		atom = l->data;

		if (atom->type != type)
			continue;

		callback(atom, data);
	}
}

void __ofono_atom_free(struct ofono_atom *atom)
{
	struct ofono_modem *modem = atom->modem;

	modem->atoms = g_slist_remove(modem->atoms, atom);

	__ofono_atom_unregister(atom);

	if (atom->destruct)
		atom->destruct(atom);

	g_free(atom);
}

static void remove_all_atoms(struct ofono_modem *modem)
{
	GSList *l;
	struct ofono_atom *atom;

	if (modem == NULL)
		return;

	for (l = modem->atoms; l; l = l->next) {
		atom = l->data;

		__ofono_atom_unregister(atom);

		if (atom->destruct)
			atom->destruct(atom);

		g_free(atom);
	}

	g_slist_free(modem->atoms);
	modem->atoms = NULL;
}

static DBusMessage *modem_get_properties(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	struct ofono_modem *modem = data;
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;
	char **interfaces;
	int i;
	GSList *l;
	struct ofono_atom *devinfo_atom;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					OFONO_PROPERTIES_ARRAY_SIGNATURE,
					&dict);

	ofono_dbus_dict_append(&dict, "Powered", DBUS_TYPE_BOOLEAN,
				&modem->powered);

	devinfo_atom = __ofono_modem_find_atom(modem, OFONO_ATOM_TYPE_DEVINFO);

	/* We cheat a little here and don't check the registered status */
	if (devinfo_atom) {
		struct ofono_devinfo *info;

		info = __ofono_atom_get_data(devinfo_atom);

		if (info->manufacturer)
			ofono_dbus_dict_append(&dict, "Manufacturer",
						DBUS_TYPE_STRING,
						&info->manufacturer);

		if (info->model)
			ofono_dbus_dict_append(&dict, "Model", DBUS_TYPE_STRING,
						&info->model);

		if (info->revision)
			ofono_dbus_dict_append(&dict, "Revision",
						DBUS_TYPE_STRING,
						&info->revision);

		if (info->serial)
			ofono_dbus_dict_append(&dict, "Serial",
						DBUS_TYPE_STRING,
						&info->serial);
	}

	interfaces = g_new0(char *, g_slist_length(modem->interface_list) + 1);

	for (i = 0, l = modem->interface_list; l; l = l->next, i++)
		interfaces[i] = l->data;

	ofono_dbus_dict_append_array(&dict, "Interfaces", DBUS_TYPE_STRING,
					&interfaces);

	g_free(interfaces);

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static int set_powered(struct ofono_modem *modem, ofono_bool_t powered)
{
	const struct ofono_modem_driver *driver = modem->driver;
	int err = -EINVAL;

	if (driver == NULL)
		return -EINVAL;

	if (modem->powered_pending == powered)
		return -EALREADY;

	modem->powered_pending = powered;

	if (powered == TRUE) {
		if (driver->enable)
			err = driver->enable(modem);
	} else {
		if (driver->disable)
			err = driver->disable(modem);
	}

	return err;
}

static gboolean set_powered_timeout(gpointer user)
{
	struct ofono_modem *modem = user;

	DBG("modem: %p", modem);

	modem->timeout = 0;

	if (modem->pending != NULL) {
		DBusMessage *reply;

		reply = __ofono_error_timed_out(modem->pending);
		__ofono_dbus_pending_reply(&modem->pending, reply);
	}

	return FALSE;
}

static DBusMessage *modem_set_property(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct ofono_modem *modem = data;
	DBusMessageIter iter, var;
	const char *name;

	if (dbus_message_iter_init(msg, &iter) == FALSE)
		return __ofono_error_invalid_args(msg);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_get_basic(&iter, &name);
	dbus_message_iter_next(&iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
		return __ofono_error_invalid_args(msg);

	dbus_message_iter_recurse(&iter, &var);

	if (g_str_equal(name, "Powered") == TRUE) {
		ofono_bool_t powered;
		int err;

		if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN)
			return __ofono_error_invalid_args(msg);

		dbus_message_iter_get_basic(&var, &powered);

		if (modem->pending != NULL)
			return __ofono_error_busy(msg);

		if (modem->powered == powered)
			return dbus_message_new_method_return(msg);

		err = set_powered(modem, powered);
		if (err < 0) {
			if (err != -EINPROGRESS)
				return __ofono_error_failed(msg);

			modem->pending = dbus_message_ref(msg);
			modem->timeout = g_timeout_add_seconds(20,
						set_powered_timeout, modem);
			return NULL;
		}

		ofono_debug("Foobar");

		modem->powered = powered;
		modem->powered_pending = powered;

		g_dbus_send_reply(conn, msg, DBUS_TYPE_INVALID);

		ofono_dbus_signal_property_changed(conn, modem->path,
						OFONO_MODEM_INTERFACE,
						"Powered", DBUS_TYPE_BOOLEAN,
						&powered);

		if (powered) {
			if (modem->driver->populate)
				modem->driver->populate(modem);

			__ofono_history_probe_drivers(modem);
		} else {
			remove_all_atoms(modem);
		}

		return NULL;
	}

	return __ofono_error_invalid_args(msg);
}

static GDBusMethodTable modem_methods[] = {
	{ "GetProperties",	"",	"a{sv}",	modem_get_properties },
	{ "SetProperty",	"sv",	"",		modem_set_property,
							G_DBUS_METHOD_FLAG_ASYNC },
	{ }
};

static GDBusSignalTable modem_signals[] = {
	{ "PropertyChanged",	"sv" },
	{ }
};

void ofono_modem_set_powered(struct ofono_modem *modem, ofono_bool_t powered)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	dbus_bool_t dbus_powered;

	if (modem->timeout > 0) {
		g_source_remove(modem->timeout);
		modem->timeout = 0;
	}

	if (modem->pending != NULL) {
		DBusMessage *reply;

		if (powered == modem->powered_pending)
			reply = dbus_message_new_method_return(modem->pending);
		else
			reply = __ofono_error_failed(modem->pending);

		__ofono_dbus_pending_reply(&modem->pending, reply);
	}

	modem->powered_pending = powered;

	if (modem->powered == powered)
		return;

	modem->powered = powered;

	if (modem->driver == NULL)
		return;

	dbus_powered = powered;
	ofono_dbus_signal_property_changed(conn, modem->path,
						OFONO_MODEM_INTERFACE,
						"Powered", DBUS_TYPE_BOOLEAN,
						&dbus_powered);

	if (powered) {
		if (modem->driver->populate)
			modem->driver->populate(modem);

		__ofono_history_probe_drivers(modem);
	} else {
		remove_all_atoms(modem);
	}
}

ofono_bool_t ofono_modem_get_powered(struct ofono_modem *modem)
{
	if (modem == NULL)
		return FALSE;

	return modem->powered;
}

static gboolean trigger_interface_update(void *data)
{
	struct ofono_modem *modem = data;
	DBusConnection *conn = ofono_dbus_get_connection();
	char **interfaces;
	GSList *l;
	int i;

	interfaces = g_new0(char *, g_slist_length(modem->interface_list) + 1);

	for (i = 0, l = modem->interface_list; l; l = l->next, i++)
		interfaces[i] = l->data;

	ofono_dbus_signal_array_property_changed(conn, modem->path,
						OFONO_MODEM_INTERFACE,
						"Interfaces", DBUS_TYPE_STRING,
						&interfaces);

	g_free(interfaces);

	modem->interface_update = 0;

	return FALSE;
}

void ofono_modem_add_interface(struct ofono_modem *modem,
				const char *interface)
{
	modem->interface_list =
		g_slist_prepend(modem->interface_list, g_strdup(interface));

	if (modem->interface_update != 0)
		return;

	modem->interface_update = g_idle_add(trigger_interface_update, modem);
}

void ofono_modem_remove_interface(struct ofono_modem *modem,
				const char *interface)
{
	GSList *found = g_slist_find_custom(modem->interface_list, interface,
						(GCompareFunc) strcmp);

	if (!found) {
		ofono_error("Interface %s not found on the interface_list",
				interface);
		return;
	}

	g_free(found->data);

	modem->interface_list = g_slist_remove(modem->interface_list,
						found->data);

	if (modem->interface_update != 0)
		return;

	modem->interface_update = g_idle_add(trigger_interface_update, modem);
}

static void query_serial_cb(const struct ofono_error *error,
				const char *serial, void *user)
{
	struct ofono_devinfo *info = user;
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(info->atom);

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR)
		return;

	info->serial = g_strdup(serial);

	ofono_dbus_signal_property_changed(conn, path,
						OFONO_MODEM_INTERFACE,
						"Serial", DBUS_TYPE_STRING,
						&info->serial);
}

static void query_serial(struct ofono_devinfo *info)
{
	if (!info->driver->query_serial)
		return;

	info->driver->query_serial(info, query_serial_cb, info);
}

static void query_revision_cb(const struct ofono_error *error,
				const char *revision, void *user)
{
	struct ofono_devinfo *info = user;
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(info->atom);

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR)
		goto out;

	info->revision = g_strdup(revision);

	ofono_dbus_signal_property_changed(conn, path,
						OFONO_MODEM_INTERFACE,
						"Revision", DBUS_TYPE_STRING,
						&info->revision);

out:
	query_serial(info);
}

static void query_revision(struct ofono_devinfo *info)
{
	if (!info->driver->query_revision) {
		query_serial(info);
		return;
	}

	info->driver->query_revision(info, query_revision_cb, info);
}

static void query_model_cb(const struct ofono_error *error,
				const char *model, void *user)
{
	struct ofono_devinfo *info = user;
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(info->atom);

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR)
		goto out;

	info->model = g_strdup(model);

	ofono_dbus_signal_property_changed(conn, path,
						OFONO_MODEM_INTERFACE,
						"Model", DBUS_TYPE_STRING,
						&info->model);

out:
	query_revision(info);
}

static void query_model(struct ofono_devinfo *info)
{
	if (!info->driver->query_model) {
		/* If model is not supported, don't bother querying revision */
		query_serial(info);
	}

	info->driver->query_model(info, query_model_cb, info);
}

static void query_manufacturer_cb(const struct ofono_error *error,
					const char *manufacturer, void *user)
{
	struct ofono_devinfo *info = user;
	DBusConnection *conn = ofono_dbus_get_connection();
	const char *path = __ofono_atom_get_path(info->atom);

	if (error->type != OFONO_ERROR_TYPE_NO_ERROR)
		goto out;

	info->manufacturer = g_strdup(manufacturer);

	ofono_dbus_signal_property_changed(conn, path,
						OFONO_MODEM_INTERFACE,
						"Serial", DBUS_TYPE_STRING,
						&info->manufacturer);

out:
	query_model(info);
}

static gboolean query_manufacturer(gpointer user)
{
	struct ofono_devinfo *info = user;

	if (!info->driver->query_manufacturer) {
		query_model(info);
		return FALSE;
	}

	info->driver->query_manufacturer(info, query_manufacturer_cb, info);

	return FALSE;
}

int ofono_devinfo_driver_register(const struct ofono_devinfo_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	if (d->probe == NULL)
		return -EINVAL;

	g_devinfo_drivers = g_slist_prepend(g_devinfo_drivers, (void *)d);

	return 0;
}

void ofono_devinfo_driver_unregister(const struct ofono_devinfo_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	g_devinfo_drivers = g_slist_remove(g_devinfo_drivers, (void *)d);
}

static void devinfo_remove(struct ofono_atom *atom)
{
	struct ofono_devinfo *info = __ofono_atom_get_data(atom);
	DBG("atom: %p", atom);

	if (info == NULL)
		return;

	if (info->driver == NULL)
		return;

	if (info->driver->remove)
		info->driver->remove(info);

	g_free(info->manufacturer);
	g_free(info->model);
	g_free(info->revision);
	g_free(info->serial);

	g_free(info);
}

struct ofono_devinfo *ofono_devinfo_create(struct ofono_modem *modem,
							unsigned int vendor,
							const char *driver,
							void *data)
{
	struct ofono_devinfo *info;
	GSList *l;

	info = g_new0(struct ofono_devinfo, 1);

	info->atom = __ofono_modem_add_atom(modem, OFONO_ATOM_TYPE_DEVINFO,
						devinfo_remove, info);

	for (l = g_devinfo_drivers; l; l = l->next) {
		const struct ofono_devinfo_driver *drv = l->data;

		if (g_strcmp0(drv->name, driver))
			continue;

		if (drv->probe(info, vendor, data) < 0)
			continue;

		info->driver = drv;
		break;
	}

	return info;
}

void ofono_devinfo_register(struct ofono_devinfo *info)
{
	query_manufacturer(info);
}

void ofono_devinfo_remove(struct ofono_devinfo *info)
{
	__ofono_atom_free(info->atom);
}

void ofono_devinfo_set_data(struct ofono_devinfo *info, void *data)
{
	info->driver_data = data;
}

void *ofono_devinfo_get_data(struct ofono_devinfo *info)
{
	return info->driver_data;
}

/* Clients only need to free *modems */
const char **__ofono_modem_get_list()
{
	GSList *l;
	int i;
	struct ofono_modem *modem;
	const char **modems;

	modems = g_new0(const char *, g_slist_length(g_modem_list) + 1);

	for (l = g_modem_list, i = 0; l; l = l->next, i++) {
		modem = l->data;

		if (modem->driver == NULL)
			continue;

		modems[i] = modem->path;
	}

	return modems;
}

static void unregister_property(gpointer data)
{
	struct ofono_property *property = data;

	DBG("property %p", property);

	g_free(property->value);
	g_free(property);
}

static int set_modem_property(struct ofono_modem *modem, const char *name,
				enum ofono_property_type type,
				const void *value)
{
	struct ofono_property *property;

	DBG("modem %p property %s", modem, name);

	if (type != OFONO_PROPERTY_TYPE_STRING &&
			type != OFONO_PROPERTY_TYPE_INTEGER)
		return -EINVAL;

	property = g_try_new0(struct ofono_property, 1);
	if (property == NULL)
		return -ENOMEM;

	property->type = type;

	switch (type) {
	case OFONO_PROPERTY_TYPE_STRING:
		property->value = g_strdup((const char *) value);
		break;
	case OFONO_PROPERTY_TYPE_INTEGER:
		property->value = g_memdup(value, sizeof(int));
		break;
	default:
		break;
	}

	g_hash_table_replace(modem->properties, g_strdup(name), property);

	return 0;
}

static gboolean get_modem_property(struct ofono_modem *modem, const char *name,
					enum ofono_property_type type,
					void *value)
{
	struct ofono_property *property;

	DBG("modem %p property %s", modem, name);

	property = g_hash_table_lookup(modem->properties, name);

	if (property == NULL)
		return FALSE;

	if (property->type != type)
		return FALSE;

	switch (property->type) {
	case OFONO_PROPERTY_TYPE_STRING:
		*((const char **) value) = property->value;
		return TRUE;
	case OFONO_PROPERTY_TYPE_INTEGER:
		memcpy(value, property->value, sizeof(int));
		return TRUE;
	default:
		return FALSE;
	}
}

int ofono_modem_set_string(struct ofono_modem *modem,
				const char *key, const char *value)
{
	return set_modem_property(modem, key,
					OFONO_PROPERTY_TYPE_STRING, value);
}

int ofono_modem_set_integer(struct ofono_modem *modem,
				const char *key, int value)
{
	return set_modem_property(modem, key,
					OFONO_PROPERTY_TYPE_INTEGER, &value);
}

const char *ofono_modem_get_string(struct ofono_modem *modem, const char *key)
{
	const char *value;

	if (get_modem_property(modem, key,
			OFONO_PROPERTY_TYPE_STRING, &value) == FALSE)
		return NULL;

	return value;
}

int ofono_modem_get_integer(struct ofono_modem *modem, const char *key)
{
	int value;

	if (get_modem_property(modem, key,
			OFONO_PROPERTY_TYPE_INTEGER, &value) == FALSE)
		return 0;

	return value;
}

struct ofono_modem *ofono_modem_create(const char *type)
{
	struct ofono_modem *modem;
	char path[128];

	DBG("%s", type);

	if (strlen(type) > 16)
		return NULL;

	snprintf(path, sizeof(path), "/%s%d", type, next_modem_id);

	if (__ofono_dbus_valid_object_path(path) == FALSE)
		return NULL;

	modem = g_try_new0(struct ofono_modem, 1);

	if (modem == NULL)
		return modem;

	modem->path = g_strdup(path);
	modem->driver_type = g_strdup(type);
	modem->properties = g_hash_table_new_full(g_str_hash, g_str_equal,
						g_free, unregister_property);

	g_modem_list = g_slist_prepend(g_modem_list, modem);

	next_modem_id += 1;

	return modem;
}

static void emit_modems()
{
	DBusConnection *conn = ofono_dbus_get_connection();
	const char **modems = __ofono_modem_get_list();

	if (modems == NULL)
		return;

	ofono_dbus_signal_array_property_changed(conn,
				OFONO_MANAGER_PATH,
				OFONO_MANAGER_INTERFACE, "Modems",
				DBUS_TYPE_OBJECT_PATH, &modems);

	g_free(modems);
}

int ofono_modem_register(struct ofono_modem *modem)
{
	DBusConnection *conn = ofono_dbus_get_connection();
	GSList *l;

	if (modem == NULL)
		return -EINVAL;

	if (modem->driver != NULL)
		return -EALREADY;

	for (l = g_driver_list; l; l = l->next) {
		const struct ofono_modem_driver *drv = l->data;

		if (g_strcmp0(drv->name, modem->driver_type))
			continue;

		if (drv->probe(modem) < 0)
			continue;

		modem->driver = drv;
		break;
	}

	if (modem->driver == NULL)
		return -ENODEV;

	if (!g_dbus_register_interface(conn, modem->path, OFONO_MODEM_INTERFACE,
				modem_methods, modem_signals, NULL,
				modem, NULL)) {
		ofono_error("Modem register failed on path %s", modem->path);

		if (modem->driver->remove)
			modem->driver->remove(modem);

		modem->driver = NULL;

		return -EIO;
	}

	g_free(modem->driver_type);
	modem->driver_type = NULL;

	emit_modems();

	/* TODO: Read powered property from store */
	if (modem->powered_persistent)
		set_powered(modem, TRUE);

	if (modem->powered == TRUE && modem->driver->populate) {
		modem->driver->populate(modem);
		__ofono_history_probe_drivers(modem);
	}

	return 0;
}

static void modem_unregister(struct ofono_modem *modem)
{
	DBusConnection *conn = ofono_dbus_get_connection();

	if (modem->driver == NULL)
		return;

	remove_all_atoms(modem);
	remove_all_watches(modem);

	g_slist_foreach(modem->interface_list, (GFunc)g_free, NULL);
	g_slist_free(modem->interface_list);
	modem->interface_list = NULL;

	if (modem->timeout) {
		g_source_remove(modem->timeout);
		modem->timeout = 0;
	}

	if (modem->pending) {
		dbus_message_unref(modem->pending);
		modem->pending = NULL;
	}

	if (modem->interface_update) {
		g_source_remove(modem->interface_update);
		modem->interface_update = 0;
	}

	g_dbus_unregister_interface(conn, modem->path, OFONO_MODEM_INTERFACE);

	if (modem->powered == TRUE)
		set_powered(modem, FALSE);

	if (modem->driver->remove)
		modem->driver->remove(modem);

	g_hash_table_destroy(modem->properties);
	modem->properties = NULL;

	modem->driver = NULL;

	emit_modems();
}

void ofono_modem_remove(struct ofono_modem *modem)
{
	DBG("%p", modem);

	if (modem == NULL)
		return;

	if (modem->driver)
		modem_unregister(modem);

	g_modem_list = g_slist_remove(g_modem_list, modem);

	if (modem->driver_type)
		g_free(modem->driver_type);

	g_free(modem->path);
	g_free(modem);
}

int ofono_modem_driver_register(const struct ofono_modem_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	if (d->probe == NULL)
		return -EINVAL;

	g_driver_list = g_slist_prepend(g_driver_list, (void *)d);

	return 0;
}

void ofono_modem_driver_unregister(const struct ofono_modem_driver *d)
{
	DBG("driver: %p, name: %s", d, d->name);

	g_driver_list = g_slist_remove(g_driver_list, (void *)d);
}
