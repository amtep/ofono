/*
 *
 *  AT chat library with GLib integration
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

#include <glib.h>

#include "gatsyntax.h"

enum GSMV1_STATE_ {
	GSMV1_STATE_IDLE = 0,
	GSMV1_STATE_INITIAL_CR,
	GSMV1_STATE_INITIAL_LF,
	GSMV1_STATE_RESPONSE,
	GSMV1_STATE_TERMINATOR_CR,
	GSMV1_STATE_GUESS_MULTILINE_RESPONSE,
	GSMV1_STATE_MULTILINE_RESPONSE,
	GSMV1_STATE_MULTILINE_TERMINATOR_CR,
	GSMV1_STATE_PDU_CHECK_EXTRA_CR,
	GSMV1_STATE_PDU_CHECK_EXTRA_LF,
	GSMV1_STATE_PDU,
	GSMV1_STATE_PDU_CR,
	GSMV1_STATE_PROMPT,
	GSMV1_STATE_GARBAGE,
	GSMV1_STATE_GARBAGE_CHECK_LF,
};

static void gsmv1_hint(GAtSyntax *syntax, GAtSyntaxExpectHint hint)
{
	switch (hint) {
	case G_AT_SYNTAX_EXPECT_PDU:
		syntax->state = GSMV1_STATE_PDU_CHECK_EXTRA_CR;
		break;
	case G_AT_SYNTAX_EXPECT_MULTILINE:
		syntax->state = GSMV1_STATE_GUESS_MULTILINE_RESPONSE;
		break;
	default:
		break;
	};
}

static GAtSyntaxResult gsmv1_feed(GAtSyntax *syntax,
					const char *bytes, gsize *len)
{
	gsize i = 0;
	GAtSyntaxResult res = G_AT_SYNTAX_RESULT_UNSURE;

	while (i < *len) {
		char byte = bytes[i];

		switch (syntax->state) {
		case GSMV1_STATE_IDLE:
			if (byte == '\r')
				syntax->state = GSMV1_STATE_INITIAL_CR;
			else
				syntax->state = GSMV1_STATE_GARBAGE;
			break;

		case GSMV1_STATE_INITIAL_CR:
			if (byte == '\n')
				syntax->state = GSMV1_STATE_INITIAL_LF;
			else
				syntax->state = GSMV1_STATE_GARBAGE;
			break;

		case GSMV1_STATE_INITIAL_LF:
			if (byte == '\r')
				syntax->state = GSMV1_STATE_TERMINATOR_CR;
			else if (byte == '>')
				syntax->state = GSMV1_STATE_PROMPT;
			else
				syntax->state = GSMV1_STATE_RESPONSE;
			break;

		case GSMV1_STATE_RESPONSE:
			if (byte == '\r')
				syntax->state = GSMV1_STATE_TERMINATOR_CR;
			break;

		case GSMV1_STATE_TERMINATOR_CR:
			syntax->state = GSMV1_STATE_IDLE;

			if (byte == '\n') {
				i += 1;
				res = G_AT_SYNTAX_RESULT_LINE;
			} else
				res = G_AT_SYNTAX_RESULT_UNRECOGNIZED;

			goto out;

		case GSMV1_STATE_GUESS_MULTILINE_RESPONSE:
			if (byte == '\r')
				syntax->state = GSMV1_STATE_INITIAL_CR;
			else
				syntax->state = GSMV1_STATE_MULTILINE_RESPONSE;
			break;

		case GSMV1_STATE_MULTILINE_RESPONSE:
			if (byte == '\r')
				syntax->state = GSMV1_STATE_MULTILINE_TERMINATOR_CR;
			break;

		case GSMV1_STATE_MULTILINE_TERMINATOR_CR:
			syntax->state = GSMV1_STATE_IDLE;

			if (byte == '\n') {
				i += 1;
				res = G_AT_SYNTAX_RESULT_MULTILINE;
			} else
				res = G_AT_SYNTAX_RESULT_UNRECOGNIZED;

			goto out;

		/* Some 27.007 compliant modems still get this wrong.  They
		 * insert an extra CRLF between the command and he PDU,
		 * in effect making them two separate lines.  We try to
		 * handle this case gracefully
		 */
		case GSMV1_STATE_PDU_CHECK_EXTRA_CR:
			if (byte == '\r')
				syntax->state = GSMV1_STATE_PDU_CHECK_EXTRA_LF;
			else
				syntax->state = GSMV1_STATE_PDU;
			break;

		case GSMV1_STATE_PDU_CHECK_EXTRA_LF:
			res = G_AT_SYNTAX_RESULT_UNRECOGNIZED;
			syntax->state = GSMV1_STATE_PDU;

			if (byte == '\n')
				i += 1;

			goto out;

		case GSMV1_STATE_PDU:
			if (byte == '\r')
				syntax->state = GSMV1_STATE_PDU_CR;
			break;

		case GSMV1_STATE_PDU_CR:
			syntax->state = GSMV1_STATE_IDLE;

			if (byte == '\n') {
				i += 1;
				res = G_AT_SYNTAX_RESULT_PDU;
			} else
				res = G_AT_SYNTAX_RESULT_UNRECOGNIZED;

			goto out;

		case GSMV1_STATE_PROMPT:
			if (byte == ' ') {
				syntax->state = GSMV1_STATE_IDLE;
				i += 1;
				res = G_AT_SYNTAX_RESULT_PROMPT;
				goto out;
			}

			syntax->state = GSMV1_STATE_RESPONSE;
			return G_AT_SYNTAX_RESULT_UNSURE;

		case GSMV1_STATE_GARBAGE:
			if (byte == '\r')
				syntax->state = GSMV1_STATE_GARBAGE_CHECK_LF;
			break;

		case GSMV1_STATE_GARBAGE_CHECK_LF:
			syntax->state = GSMV1_STATE_IDLE;
			res = G_AT_SYNTAX_RESULT_UNRECOGNIZED;

			if (byte == '\n')
				i += 1;

			goto out;

		default:
			break;
		};

		i += 1;
	}

out:
	*len = i;
	return res;
}

GAtSyntax *g_at_syntax_new_full(GAtSyntaxFeedFunc feed,
					GAtSyntaxSetHintFunc hint,
					int initial_state)
{
	GAtSyntax *syntax;

	syntax = g_new0(GAtSyntax, 1);

	syntax->feed = feed;
	syntax->set_hint = hint;
	syntax->state = initial_state;
	syntax->ref_count = 1;

	return syntax;
}


GAtSyntax *g_at_syntax_new_gsmv1()
{
	return g_at_syntax_new_full(gsmv1_feed, gsmv1_hint, GSMV1_STATE_IDLE);
}

GAtSyntax *g_at_syntax_ref(GAtSyntax *syntax)
{
	if (syntax == NULL)
		return NULL;

	g_atomic_int_inc(&syntax->ref_count);

	return syntax;
}

void g_at_syntax_unref(GAtSyntax *syntax)
{
	gboolean is_zero;

	if (syntax == NULL)
		return;

	is_zero = g_atomic_int_dec_and_test(&syntax->ref_count);

	if (is_zero)
		g_free(syntax);
}
