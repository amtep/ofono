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

#include <errno.h>

#define LIBUDEV_I_KNOW_THE_API_IS_SUBJECT_TO_CHANGE
#include <libudev.h>

#include <glib.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/modem.h>
#include <ofono/log.h>

#ifdef NEED_UDEV_MONITOR_FILTER
static int udev_monitor_filter_add_match_subsystem_devtype(struct udev_monitor *udev_monitor,
				const char *subsystem, const char *devtype)
{
	return -EINVAL;
}
static int udev_monitor_filter_update(struct udev_monitor *udev_monitor)
{
	return -EINVAL;
}
static int udev_monitor_filter_remove(struct udev_monitor *udev_monitor)
{
	return -EINVAL;
}
#endif

static GSList *modem_list = NULL;

static struct ofono_modem *find_modem(const char *devpath)
{
	GSList *list;

	for (list = modem_list; list; list = list->next) {
		struct ofono_modem *modem = list->data;
		const char *path = ofono_modem_get_string(modem, "Path");

		if (g_strcmp0(devpath, path) == 0)
			return modem;
	}

	return NULL;
}

static const char *get_driver(struct udev_device *udev_device)
{
	struct udev_list_entry *entry;
	const char *driver = NULL;

	entry = udev_device_get_properties_list_entry(udev_device);
	while (entry) {
		const char *name = udev_list_entry_get_name(entry);

		if (g_strcmp0(name, "OFONO_DRIVER") == 0)
			driver = udev_list_entry_get_value(entry);

		entry = udev_list_entry_get_next(entry);
	}

	return driver;
}

#define MODEM_DEVICE		"ModemDevice"
#define DATA_DEVICE		"DataDevice"
#define GPS_DEVICE		"GPSDevice"
#define NETWORK_INTERFACE	"NetworkInterface"

static void add_mbm(struct ofono_modem *modem,
					struct udev_device *udev_device)
{
	const char *description, *devnode;
	const char *device, *network;
	int registered;

	description = udev_device_get_sysattr_value(udev_device,
							"device/interface");
	if (description == NULL)
		return;

	registered = ofono_modem_get_integer(modem, "Registered");
	if (registered != 0)
		return;

	if (g_str_has_suffix(description, "Minicard Modem") == TRUE) {
		devnode = udev_device_get_devnode(udev_device);
		ofono_modem_set_string(modem, MODEM_DEVICE, devnode);
	} else if (g_str_has_suffix(description,
					"Minicard Data Modem") == TRUE) {
		devnode = udev_device_get_devnode(udev_device);
		ofono_modem_set_string(modem, DATA_DEVICE, devnode);
	} else if (g_str_has_suffix(description,
					"Minicard GPS Port") == TRUE) {
		devnode = udev_device_get_devnode(udev_device);
		ofono_modem_set_string(modem, GPS_DEVICE, devnode);
	} else if (g_str_has_suffix(description,
					"Minicard Network Adapter") == TRUE) {
		devnode = udev_device_get_property_value(udev_device,
								"INTERFACE");
		ofono_modem_set_string(modem, NETWORK_INTERFACE, devnode);
	} else
		return;

	device  = ofono_modem_get_string(modem, MODEM_DEVICE);
	network = ofono_modem_get_string(modem, NETWORK_INTERFACE);

	if (device != NULL && network != NULL) {
		ofono_modem_set_integer(modem, "Registered", 1);
		ofono_modem_register(modem);
	}
}

static void add_hso(struct ofono_modem *modem,
					struct udev_device *udev_device)
{
	const char *subsystem, *type, *devnode;
	const char *device, *network;
	int registered;

	subsystem = udev_device_get_subsystem(udev_device);
	if (subsystem == NULL)
		return;

	registered = ofono_modem_get_integer(modem, "Registered");
	if (registered != 0)
		return;

	type = udev_device_get_sysattr_value(udev_device, "hsotype");

	if (type != NULL && g_str_has_suffix(type, "Control") == TRUE) {
		devnode = udev_device_get_devnode(udev_device);
		ofono_modem_set_string(modem, MODEM_DEVICE, devnode);
	} else if (g_str_equal(subsystem, "net") == TRUE) {
		devnode = udev_device_get_property_value(udev_device,
								"INTERFACE");
		ofono_modem_set_string(modem, NETWORK_INTERFACE, devnode);
	} else
		return;

	device  = ofono_modem_get_string(modem, MODEM_DEVICE);
	network = ofono_modem_get_string(modem, NETWORK_INTERFACE);

	if (device != NULL && network != NULL) {
		ofono_modem_set_integer(modem, "Registered", 1);
		ofono_modem_register(modem);
	}
}

static void add_huawei(struct ofono_modem *modem,
					struct udev_device *udev_device)
{
	const char *devnode;
	int registered;

	registered = ofono_modem_get_integer(modem, "Registered");
	if (registered != 0)
		return;

	devnode = udev_device_get_devnode(udev_device);
	ofono_modem_set_string(modem, "Device", devnode);

	ofono_modem_set_integer(modem, "Registered", 1);
	ofono_modem_register(modem);
}

static void add_novatel(struct ofono_modem *modem,
					struct udev_device *udev_device)
{
	const char *devnode;
	int registered;

	registered = ofono_modem_get_integer(modem, "Registered");
	if (registered != 0)
		return;

	devnode = udev_device_get_devnode(udev_device);
	ofono_modem_set_string(modem, "Device", devnode);

	ofono_modem_set_integer(modem, "Registered", 1);
	ofono_modem_register(modem);
}

static void add_modem(struct udev_device *udev_device)
{
	struct ofono_modem *modem;
	struct udev_device *parent;
	const char *devpath, *driver = NULL;

	parent = udev_device_get_parent(udev_device);
	if (parent == NULL)
		return;

	driver = get_driver(parent);
	if (driver == NULL) {
		parent = udev_device_get_parent(parent);
		driver = get_driver(parent);
		if (driver == NULL) {
			parent = udev_device_get_parent(parent);
			driver = get_driver(parent);
			if (driver == NULL)
				return;
		}
	}

	devpath = udev_device_get_devpath(parent);
	if (devpath == NULL)
		return;

	modem = find_modem(devpath);
	if (modem == NULL) {
		modem = ofono_modem_create(driver);
		if (modem == NULL)
			return;

		ofono_modem_set_string(modem, "Path", devpath);
		ofono_modem_set_integer(modem, "Registered", 0);

		modem_list = g_slist_prepend(modem_list, modem);
	}

	if (g_strcmp0(driver, "mbm") == 0)
		add_mbm(modem, udev_device);
	else if (g_strcmp0(driver, "hso") == 0)
		add_hso(modem, udev_device);
	else if (g_strcmp0(driver, "huawei") == 0)
		add_huawei(modem, udev_device);
	else if (g_strcmp0(driver, "novatel") == 0)
		add_novatel(modem, udev_device);
}

static void remove_modem(struct udev_device *udev_device)
{
	struct ofono_modem *modem;
	struct udev_device *parent;
	const char *devpath, *driver = NULL;

	parent = udev_device_get_parent(udev_device);
	if (parent == NULL)
		return;

	driver = get_driver(parent);
	if (driver == NULL) {
		parent = udev_device_get_parent(parent);
		driver = get_driver(parent);
		if (driver == NULL) {
			parent = udev_device_get_parent(parent);
			driver = get_driver(parent);
			if (driver == NULL)
				return;
		}
	}

	devpath = udev_device_get_devpath(parent);
	if (devpath == NULL)
		return;

	modem = find_modem(devpath);
	if (modem == NULL)
		return;

	modem_list = g_slist_remove(modem_list, modem);

	ofono_modem_remove(modem);
}

static void enumerate_devices(struct udev *context)
{
	struct udev_enumerate *enumerate;
	struct udev_list_entry *entry;

	enumerate = udev_enumerate_new(context);
	if (enumerate == NULL)
		return;

	udev_enumerate_add_match_subsystem(enumerate, "tty");
	udev_enumerate_add_match_subsystem(enumerate, "net");

	udev_enumerate_scan_devices(enumerate);

	entry = udev_enumerate_get_list_entry(enumerate);
	while (entry) {
		const char *syspath = udev_list_entry_get_name(entry);
		struct udev_device *device;

		device = udev_device_new_from_syspath(context, syspath);
		if (device != NULL) {
			const char *subsystem;

			subsystem = udev_device_get_subsystem(device);

			if (g_strcmp0(subsystem, "tty") == 0 ||
					g_strcmp0(subsystem, "net") == 0)
				add_modem(device);

			udev_device_unref(device);
		}

		entry = udev_list_entry_get_next(entry);
	}

	udev_enumerate_unref(enumerate);
}

static gboolean udev_event(GIOChannel *channel,
				GIOCondition condition, gpointer user_data)
{
	struct udev_monitor *monitor = user_data;
	struct udev_device *device;
	const char *subsystem, *action;

	device = udev_monitor_receive_device(monitor);
	if (device == NULL)
		return TRUE;

	subsystem = udev_device_get_subsystem(device);
	if (subsystem == NULL)
		goto done;

	action = udev_device_get_action(device);
	if (action == NULL)
		goto done;

	if (g_str_equal(action, "add") == TRUE) {
		if (g_strcmp0(subsystem, "tty") == 0 ||
					g_strcmp0(subsystem, "net") == 0)
			add_modem(device);
	} else if (g_str_equal(action, "remove") == TRUE) {
		if (g_strcmp0(subsystem, "tty") == 0 ||
					g_strcmp0(subsystem, "net") == 0)
			remove_modem(device);
	}

done:
	udev_device_unref(device);

	return TRUE;
}

static struct udev *udev_ctx;
static struct udev_monitor *udev_mon;
static guint udev_watch = 0;

static void udev_start(void)
{
	GIOChannel *channel;
	int fd;

	if (udev_monitor_enable_receiving(udev_mon) < 0) {
		ofono_error("Failed to enable udev monitor");
		return;
	}

	enumerate_devices(udev_ctx);

	fd = udev_monitor_get_fd(udev_mon);

	channel = g_io_channel_unix_new(fd);
	if (channel == NULL)
		return;

	udev_watch = g_io_add_watch(channel, G_IO_IN, udev_event, udev_mon);

	g_io_channel_unref(channel);
}

static int udev_init(void)
{
	udev_ctx = udev_new();
	if (udev_ctx == NULL) {
		ofono_error("Failed to create udev context");
		return -EIO;
	}

	udev_mon = udev_monitor_new_from_netlink(udev_ctx, "udev");
	if (udev_mon == NULL) {
		ofono_error("Failed to create udev monitor");
		udev_unref(udev_ctx);
		udev_ctx = NULL;
		return -EIO;
	}

	udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "tty", NULL);
	udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "net", NULL);

	udev_monitor_filter_update(udev_mon);

	udev_start();

	return 0;
}

static void udev_exit(void)
{
	GSList *list;

	if (udev_watch > 0)
		g_source_remove(udev_watch);

	for (list = modem_list; list; list = list->next) {
		struct ofono_modem *modem = list->data;

		ofono_modem_remove(modem);
	}

	g_slist_free(modem_list);
	modem_list = NULL;

	if (udev_ctx == NULL)
		return;

	udev_monitor_filter_remove(udev_mon);

	udev_monitor_unref(udev_mon);
	udev_unref(udev_ctx);
}

OFONO_PLUGIN_DEFINE(udev, "udev hardware detection", VERSION,
			OFONO_PLUGIN_PRIORITY_DEFAULT, udev_init, udev_exit)
