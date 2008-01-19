/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Joshua Colp
 *
 * Joshua Colp <jcolp@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Directed Call Pickup Support
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/features.h"

#define PICKUPMARK "PICKUPMARK"

static const char *app = "Pickup";
static const char *synopsis = "Directed Call Pickup";
static const char *descrip =
"  Pickup([extension[@context][&extension2@[context]...]]):  This application can\n"
"pickup any ringing channel that is calling the specified extension.  If no\n"
"context is specified, the current context will be used. If you use the special\n"
"string \"PICKUPMARK\" for the context parameter, for example 10@PICKUPMARK,\n"
"this application tries to find a channel which has defined a ${PICKUPMARK}\n"
"channel variable with the same value as \"extension\" (in this example, \"10\").\n"
"When no parameter is specified, the application will pickup a channel matching\n"
"the pickup group of the active channel.";

/* Perform actual pickup between two channels */
static int pickup_do(struct ast_channel *chan, struct ast_channel *target)
{
	int res = 0;

	ast_debug(1, "Call pickup on '%s' by '%s'\n", target->name, chan->name);

	if ((res = ast_answer(chan))) {
		ast_log(LOG_WARNING, "Unable to answer '%s'\n", chan->name);
		return -1;
	}

	if ((res = ast_queue_control(chan, AST_CONTROL_ANSWER))) {
		ast_log(LOG_WARNING, "Unable to queue answer on '%s'\n", chan->name);
		return -1;
	}

	if ((res = ast_channel_masquerade(target, chan))) {
		ast_log(LOG_WARNING, "Unable to masquerade '%s' into '%s'\n", chan->name, target->name);
		return -1;
	}

	return res;
}

/* Helper function that determines whether a channel is capable of being picked up */
static int can_pickup(struct ast_channel *chan)
{
	if (!chan->pbx && (chan->_state == AST_STATE_RINGING || chan->_state == AST_STATE_RING))
		return 1;
	else
		return 0;
}

/* Attempt to pick up specified extension with context */
static int pickup_by_exten(struct ast_channel *chan, const char *exten, const char *context)
{
	int res = -1;
	struct ast_channel *target = NULL;

	while ((target = ast_channel_walk_locked(target))) {
		if ((!strcasecmp(target->macroexten, exten) || !strcasecmp(target->exten, exten)) &&
		    !strcasecmp(target->dialcontext, context) &&
		    can_pickup(target)) {
			res = pickup_do(chan, target);
			ast_channel_unlock(target);
			break;
		}
		ast_channel_unlock(target);
	}

	return res;
}

/* Attempt to pick up specified mark */
static int pickup_by_mark(struct ast_channel *chan, const char *mark)
{
	int res = -1;
	const char *tmp = NULL;
	struct ast_channel *target = NULL;

	while ((target = ast_channel_walk_locked(target))) {
		if ((tmp = pbx_builtin_getvar_helper(target, PICKUPMARK)) &&
		    !strcasecmp(tmp, mark) &&
		    can_pickup(target)) {
			res = pickup_do(chan, target);
			ast_channel_unlock(target);
			break;
		}
		ast_channel_unlock(target);
	}

	return res;
}

/* Main application entry point */
static int pickup_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char *tmp = ast_strdupa(data);
	char *exten = NULL, *context = NULL;

	if (ast_strlen_zero(data)) {
		res = ast_pickup_call(chan);
		return res;
	}
	
	/* Parse extension (and context if there) */
	while (!ast_strlen_zero(tmp) && (exten = strsep(&tmp, "&"))) {
		if ((context = strchr(exten, '@')))
			*context++ = '\0';
		if (!ast_strlen_zero(context) && !strcasecmp(context, PICKUPMARK)) {
			if (!pickup_by_mark(chan, exten))
				break;
		} else {
			if (!pickup_by_exten(chan, exten, !ast_strlen_zero(context) ? context : chan->context))
				break;
		}
		ast_log(LOG_NOTICE, "No target channel found for %s.\n", exten);
	}

	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);
	
	return res;
}

static int load_module(void)
{
	return ast_register_application(app, pickup_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Directed Call Pickup Application");
