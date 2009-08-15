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

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <glib.h>

#include "driver.h"
#include "simutil.h"

const unsigned char valid_efopl[] = {
	0x42, 0xf6, 0x1d, 0x00, 0x00, 0xff, 0xfe, 0x01,
};

const unsigned char valid_efpnn[][28] = {
	{ 0x43, 0x0a, 0x00, 0x54, 0x75, 0x78, 0x20, 0x43, 0x6f, 0x6d,
	  0x6d, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, },
	{ 0x43, 0x05, 0x00, 0x4C, 0x6F, 0x6E, 0x67, 0x45, 0x06, 0x00,
	  0x53, 0x68, 0x6F, 0x72, 0x74, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, }
};

static void test_eons()
{
	const struct sim_eons_operator_info *op_info;
	struct sim_eons *eons_info;

	eons_info = sim_eons_new(2);

	g_assert(sim_eons_pnn_is_empty(eons_info));

	sim_eons_add_pnn_record(eons_info, 1,
			valid_efpnn[0], sizeof(valid_efpnn[0]));
	g_assert(!sim_eons_pnn_is_empty(eons_info));

	sim_eons_add_pnn_record(eons_info, 2,
			valid_efpnn[1], sizeof(valid_efpnn[1]));
	g_assert(!sim_eons_pnn_is_empty(eons_info));

	sim_eons_add_opl_record(eons_info, valid_efopl, sizeof(valid_efopl));
	sim_eons_optimize(eons_info);

	op_info = sim_eons_lookup(eons_info, "246", "82");
	g_assert(!op_info);
	op_info = sim_eons_lookup(eons_info, "246", "81");
	g_assert(op_info);

	g_assert(!strcmp(op_info->longname, "Tux Comm"));
	g_assert(!op_info->shortname);
	g_assert(!op_info->info);

	sim_eons_free(eons_info);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/testsimutil/EONS Handling", test_eons);

	return g_test_run();
}
