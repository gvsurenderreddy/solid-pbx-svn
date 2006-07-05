/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Execute arbitrary system commands
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 32846 $")

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/options.h"

static char *tdesc = "Generic System() application";

static char *app = "System";

static char *app2 = "TrySystem";

static char *synopsis = "Execute a system command";

static char *synopsis2 = "Try executing a system command";

static char *chanvar = "SYSTEMSTATUS";

static char *descrip =
"  System(command): Executes a command  by  using  system(). If the command\n"
"fails, the console should report a fallthrough. \n"
"Result of execution is returned in the SYSTEMSTATUS channel variable:\n"
"   FAILURE	Could not execute the specified command\n"
"   SUCCESS	Specified command successfully executed\n"
"\n"
"Old behaviour:\n"
"If the command itself executes but is in error, and if there exists\n"
"a priority n + 101, where 'n' is the priority of the current instance,\n"
"then  the  channel  will  be  setup to continue at that priority level.\n"
"Note that this jump functionality has been deprecated and will only occur\n"
"if the global priority jumping option is enabled in extensions.conf.\n";

static char *descrip2 =
"  TrySystem(command): Executes a command  by  using  system().\n"
"on any situation.\n"
"Result of execution is returned in the SYSTEMSTATUS channel variable:\n"
"   FAILURE	Could not execute the specified command\n"
"   SUCCESS	Specified command successfully executed\n"
"   APPERROR	Specified command successfully executed, but returned error code\n"
"\n"
"Old behaviour:\nIf  the command itself executes but is in error, and if\n"
"there exists a priority n + 101, where 'n' is the priority of the current\n"
"instance, then  the  channel  will  be  setup  to continue at that\n"
"priority level.  Otherwise, System will terminate.\n";

LOCAL_USER_DECL;

static int system_exec_helper(struct ast_channel *chan, void *data, int failmode)
{
	int res=0;
	struct localuser *u;
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "System requires an argument(command)\n");
		pbx_builtin_setvar_helper(chan, chanvar, "FAILURE");
		return failmode;
	}

	LOCAL_USER_ADD(u);

	/* Do our thing here */
	res = ast_safe_system((char *)data);
	if ((res < 0) && (errno != ECHILD)) {
		ast_log(LOG_WARNING, "Unable to execute '%s'\n", (char *)data);
		pbx_builtin_setvar_helper(chan, chanvar, "FAILURE");
		res = failmode;
	} else if (res == 127) {
		ast_log(LOG_WARNING, "Unable to execute '%s'\n", (char *)data);
		pbx_builtin_setvar_helper(chan, chanvar, "FAILURE");
		res = failmode;
	} else {
		if (res < 0) 
			res = 0;
		if (ast_opt_priority_jumping && res)
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);

		if (res != 0)
			pbx_builtin_setvar_helper(chan, chanvar, "APPERROR");
		else
			pbx_builtin_setvar_helper(chan, chanvar, "SUCCESS");
		res = 0;
	} 

	LOCAL_USER_REMOVE(u);

	return res;
}

static int system_exec(struct ast_channel *chan, void *data)
{
	return system_exec_helper(chan, data, -1);
}

static int trysystem_exec(struct ast_channel *chan, void *data)
{
	return system_exec_helper(chan, data, 0);
}

static int unload_module(void *mod)
{
	int res;

	res = ast_unregister_application(app);
	res |= ast_unregister_application(app2);
	
	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

static int load_module(void *mod)
{
	int res;

	res = ast_register_application(app2, trysystem_exec, synopsis2, descrip2);
	res |= ast_register_application(app, system_exec, synopsis, descrip);

	return res;
}

static const char *description(void)
{
	return tdesc;
}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD1;