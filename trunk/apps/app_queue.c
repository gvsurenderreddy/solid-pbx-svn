/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief True call queues with optional send URL on answer
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \arg Config in \ref Config_qu queues.conf
 * 
 * \par Development notes
 * \note 2004-11-25: Persistent Dynamic Members added by:
 *             NetNation Communications (www.netnation.com)
 *             Kevin Lindsay <kevinl@netnation.com>
 * 
 *             Each dynamic agent in each queue is now stored in the astdb.
 *             When asterisk is restarted, each agent will be automatically
 *             readded into their recorded queues. This feature can be
 *             configured with the 'persistent_members=<1|0>' setting in the
 *             '[general]' category in queues.conf. The default is on.
 * 
 * \note 2004-06-04: Priorities in queues added by inAccess Networks (work funded by Hellas On Line (HOL) www.hol.gr).
 *
 * \note These features added by David C. Troy <dave@toad.net>:
 *    - Per-queue holdtime calculation
 *    - Estimated holdtime announcement
 *    - Position announcement
 *    - Abandoned/completed call counters
 *    - Failout timer passed as optional app parameter
 *    - Optional monitoring of calls, started when call is answered
 *
 * Patch Version 1.07 2003-12-24 01
 *
 * Added servicelevel statistic by Michiel Betel <michiel@betel.nl>
 * Added Priority jumping code for adding and removing queue members by Jonathan Stanton <asterisk@doilooklikeicare.com>
 *
 * Fixed to work with CVS as of 2004-02-25 and released as 1.07a
 * by Matthew Enger <m.enger@xi.com.au>
 *
 * \ingroup applications
 */

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 22655 $")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/app.h"
#include "asterisk/linkedlists.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/say.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"
#include "asterisk/monitor.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/astdb.h"
#include "asterisk/devicestate.h"
#include "asterisk/stringfields.h"

#define QUEUE_STRATEGY_RINGALL		0
#define QUEUE_STRATEGY_ROUNDROBIN	1
#define QUEUE_STRATEGY_LEASTRECENT	2
#define QUEUE_STRATEGY_FEWESTCALLS	3
#define QUEUE_STRATEGY_RANDOM		4
#define QUEUE_STRATEGY_RRMEMORY		5

static struct strategy {
	int strategy;
	char *name;
} strategies[] = {
	{ QUEUE_STRATEGY_RINGALL, "ringall" },
	{ QUEUE_STRATEGY_ROUNDROBIN, "roundrobin" },
	{ QUEUE_STRATEGY_LEASTRECENT, "leastrecent" },
	{ QUEUE_STRATEGY_FEWESTCALLS, "fewestcalls" },
	{ QUEUE_STRATEGY_RANDOM, "random" },
	{ QUEUE_STRATEGY_RRMEMORY, "rrmemory" },
};

#define DEFAULT_RETRY		5
#define DEFAULT_TIMEOUT		15
#define RECHECK			1		/* Recheck every second to see we we're at the top yet */
#define MAX_PERIODIC_ANNOUNCEMENTS 10 /* The maximum periodic announcements we can have */

#define	RES_OKAY	0		/* Action completed */
#define	RES_EXISTS	(-1)		/* Entry already exists */
#define	RES_OUTOFMEMORY	(-2)		/* Out of memory */
#define	RES_NOSUCHQUEUE	(-3)		/* No such queue */

static char *tdesc = "True Call Queueing";

static char *app = "Queue";

static char *synopsis = "Queue a call for a call queue";

static char *descrip =
"  Queue(queuename[|options[|URL][|announceoverride][|timeout]]):\n"
"Queues an incoming call in a particular call queue as defined in queues.conf.\n"
"This application will return to the dialplan if the queue does not exist, or\n"
"any of the join options cause the caller to not enter the queue.\n"
"The option string may contain zero or more of the following characters:\n"
"      'd' -- data-quality (modem) call (minimum delay).\n"
"      'h' -- allow callee to hang up by hitting *.\n"
"      'H' -- allow caller to hang up by hitting *.\n"
"      'n' -- no retries on the timeout; will exit this application and \n"
"	      go to the next step.\n"
"      'r' -- ring instead of playing MOH\n"
"      't' -- allow the called user transfer the calling user\n"
"      'T' -- to allow the calling user to transfer the call.\n"
"      'w' -- allow the called user to write the conversation to disk via Monitor\n"
"      'W' -- allow the calling user to write the conversation to disk via Monitor\n"
"  In addition to transferring the call, a call may be parked and then picked\n"
"up by another user.\n"
"  The optional URL will be sent to the called party if the channel supports\n"
"it.\n"
"  The timeout will cause the queue to fail out after a specified number of\n"
"seconds, checked between each queues.conf 'timeout' and 'retry' cycle.\n"
"  This application sets the following channel variable upon completion:\n"
"      QUEUESTATUS    The status of the call as a text string, one of\n"
"             TIMEOUT | FULL | JOINEMPTY | LEAVEEMPTY | JOINUNAVAIL | LEAVEUNAVAIL\n";

static char *app_aqm = "AddQueueMember" ;
static char *app_aqm_synopsis = "Dynamically adds queue members" ;
static char *app_aqm_descrip =
"   AddQueueMember(queuename[|interface[|penalty[|options]]]):\n"
"Dynamically adds interface to an existing queue.\n"
"If the interface is already in the queue and there exists an n+101 priority\n"
"then it will then jump to this priority.  Otherwise it will return an error\n"
"The option string may contain zero or more of the following characters:\n"
"       'j' -- jump to +101 priority when appropriate.\n"
"  This application sets the following channel variable upon completion:\n"
"     AQMSTATUS    The status of the attempt to add a queue member as a \n"
"                     text string, one of\n"
"           ADDED | MEMBERALREADY | NOSUCHQUEUE \n"
"Example: AddQueueMember(techsupport|SIP/3000)\n"
"";

static char *app_rqm = "RemoveQueueMember" ;
static char *app_rqm_synopsis = "Dynamically removes queue members" ;
static char *app_rqm_descrip =
"   RemoveQueueMember(queuename[|interface[|options]]):\n"
"Dynamically removes interface to an existing queue\n"
"If the interface is NOT in the queue and there exists an n+101 priority\n"
"then it will then jump to this priority.  Otherwise it will return an error\n"
"The option string may contain zero or more of the following characters:\n"
"       'j' -- jump to +101 priority when appropriate.\n"
"  This application sets the following channel variable upon completion:\n"
"     RQMSTATUS      The status of the attempt to remove a queue member as a\n"
"                     text string, one of\n"
"           REMOVED | NOTINQUEUE | NOSUCHQUEUE \n"
"Example: RemoveQueueMember(techsupport|SIP/3000)\n"
"";

static char *app_pqm = "PauseQueueMember" ;
static char *app_pqm_synopsis = "Pauses a queue member" ;
static char *app_pqm_descrip =
"   PauseQueueMember([queuename]|interface[|options]):\n"
"Pauses (blocks calls for) a queue member.\n"
"The given interface will be paused in the given queue.  This prevents\n"
"any calls from being sent from the queue to the interface until it is\n"
"unpaused with UnpauseQueueMember or the manager interface.  If no\n"
"queuename is given, the interface is paused in every queue it is a\n"
"member of.  If the interface is not in the named queue, or if no queue\n"
"is given and the interface is not in any queue, it will jump to\n"
"priority n+101, if it exists and the appropriate options are set.\n"
"The application will fail if the interface is not found and no extension\n"
"to jump to exists.\n"
"The option string may contain zero or more of the following characters:\n"
"       'j' -- jump to +101 priority when appropriate.\n"
"  This application sets the following channel variable upon completion:\n"
"     PQMSTATUS      The status of the attempt to pause a queue member as a\n"
"                     text string, one of\n"
"           PAUSED | NOTFOUND\n"
"Example: PauseQueueMember(|SIP/3000)\n";

static char *app_upqm = "UnpauseQueueMember" ;
static char *app_upqm_synopsis = "Unpauses a queue member" ;
static char *app_upqm_descrip =
"   UnpauseQueueMember([queuename]|interface[|options]):\n"
"Unpauses (resumes calls to) a queue member.\n"
"This is the counterpart to PauseQueueMember and operates exactly the\n"
"same way, except it unpauses instead of pausing the given interface.\n"
"The option string may contain zero or more of the following characters:\n"
"       'j' -- jump to +101 priority when appropriate.\n"
"  This application sets the following channel variable upon completion:\n"
"     UPQMSTATUS       The status of the attempt to unpause a queue \n"
"                      member as a text string, one of\n"
"            UNPAUSED | NOTFOUND\n"
"Example: UnpauseQueueMember(|SIP/3000)\n";

/*! \brief Persistent Members astdb family */
static const char *pm_family = "/Queue/PersistentMembers";
/* The maximum length of each persistent member queue database entry */
#define PM_MAX_LEN 2048

/*! \brief queues.conf [general] option */
static int queue_persistent_members = 0;

/*! \brief queues.conf per-queue weight option */
static int use_weight = 0;

/*! \brief queues.conf [general] option */
static int autofill_default = 0;

enum queue_result {
	QUEUE_UNKNOWN = 0,
	QUEUE_TIMEOUT = 1,
	QUEUE_JOINEMPTY = 2,
	QUEUE_LEAVEEMPTY = 3,
	QUEUE_JOINUNAVAIL = 4,
	QUEUE_LEAVEUNAVAIL = 5,
	QUEUE_FULL = 6,
};

const struct { 
	enum queue_result id;
	char *text;
} queue_results[] = {
	{ QUEUE_UNKNOWN, "UNKNOWN" },
	{ QUEUE_TIMEOUT, "TIMEOUT" },
	{ QUEUE_JOINEMPTY,"JOINEMPTY" },
	{ QUEUE_LEAVEEMPTY, "LEAVEEMPTY" },
	{ QUEUE_JOINUNAVAIL, "JOINUNAVAIL" },
	{ QUEUE_LEAVEUNAVAIL, "LEAVEUNAVAIL" },
	{ QUEUE_FULL, "FULL" },
};

/*! \brief We define a custom "local user" structure because we
   use it not only for keeping track of what is in use but
   also for keeping track of who we're dialing. */

struct callattempt {
	struct callattempt *q_next;
	struct ast_channel *chan;
	char interface[256];
	int stillgoing;
	int metric;
	int oldstatus;
	time_t lastcall;
	struct member *member;
};

LOCAL_USER_DECL;

struct queue_ent {
	struct ast_call_queue *parent;	/*!< What queue is our parent */
	char moh[80];			/*!< Name of musiconhold to be used */
	char announce[80];		/*!< Announcement to play for member when call is answered */
	char context[AST_MAX_CONTEXT];	/*!< Context when user exits queue */
	char digits[AST_MAX_EXTENSION];	/*!< Digits entered while in queue */
	int pos;			/*!< Where we are in the queue */
	int prio;			/*!< Our priority */
	int last_pos_said;              /*!< Last position we told the user */
	time_t last_periodic_announce_time;	/*!< The last time we played a periodic announcement */
	int last_periodic_announce_sound;	/* The last periodic announcement we made */
	time_t last_pos;                /*!< Last time we told the user their position */
	int opos;			/*!< Where we started in the queue */
	int handled;			/*!< Whether our call was handled */
	int max_penalty;		/*!< Limit the members that can take this call to this penalty or lower */
	time_t start;			/*!< When we started holding */
	time_t expire;			/*!< When this entry should expire (time out of queue) */
	struct ast_channel *chan;	/*!< Our channel */
	struct queue_ent *next;		/*!< The next queue entry */
};

struct member {
	char interface[80];		/*!< Technology/Location */
	int penalty;			/*!< Are we a last resort? */
	int calls;			/*!< Number of calls serviced by this member */
	int dynamic;			/*!< Are we dynamically added? */
	int status;			/*!< Status of queue member */
	int paused;			/*!< Are we paused (not accepting calls)? */
	time_t lastcall;		/*!< When last successful call was hungup */
	int dead;			/*!< Used to detect members deleted in realtime */
	struct member *next;		/*!< Next member */
};

/* values used in multi-bit flags in ast_call_queue */
#define QUEUE_EMPTY_NORMAL 1
#define QUEUE_EMPTY_STRICT 2
#define ANNOUNCEHOLDTIME_ALWAYS 1
#define ANNOUNCEHOLDTIME_ONCE 2

struct ast_call_queue {
	ast_mutex_t lock;	
	char name[80];			/*!< Name */
	char moh[80];			/*!< Music On Hold class to be used */
	char announce[80];		/*!< Announcement to play when call is answered */
	char context[AST_MAX_CONTEXT];	/*!< Exit context */
	unsigned int monjoin:1;
	unsigned int dead:1;
	unsigned int joinempty:2;
	unsigned int eventwhencalled:1;
	unsigned int leavewhenempty:2;
	unsigned int ringinuse:1;
	unsigned int reportholdtime:1;
	unsigned int wrapped:1;
	unsigned int timeoutrestart:1;
	unsigned int announceholdtime:2;
	unsigned int strategy:3;
	unsigned int maskmemberstatus:1;
	unsigned int realtime:1;
	int announcefrequency;          /*!< How often to announce their position */
	int periodicannouncefrequency;	/*!< How often to play periodic announcement */
	int roundingseconds;            /*!< How many seconds do we round to? */
	int holdtime;                   /*!< Current avg holdtime, based on recursive boxcar filter */
	int callscompleted;             /*!< Number of queue calls completed */
	int callsabandoned;             /*!< Number of queue calls abandoned */
	int servicelevel;               /*!< seconds setting for servicelevel*/
	int callscompletedinsl;         /*!< Number of calls answered with servicelevel*/
	char monfmt[8];                 /*!< Format to use when recording calls */
	char sound_next[80];            /*!< Sound file: "Your call is now first in line" (def. queue-youarenext) */
	char sound_thereare[80];        /*!< Sound file: "There are currently" (def. queue-thereare) */
	char sound_calls[80];           /*!< Sound file: "calls waiting to speak to a representative." (def. queue-callswaiting)*/
	char sound_holdtime[80];        /*!< Sound file: "The current estimated total holdtime is" (def. queue-holdtime) */
	char sound_minutes[80];         /*!< Sound file: "minutes." (def. queue-minutes) */
	char sound_lessthan[80];        /*!< Sound file: "less-than" (def. queue-lessthan) */
	char sound_seconds[80];         /*!< Sound file: "seconds." (def. queue-seconds) */
	char sound_thanks[80];          /*!< Sound file: "Thank you for your patience." (def. queue-thankyou) */
	char sound_reporthold[80];	/*!< Sound file: "Hold time" (def. queue-reporthold) */
	char sound_periodicannounce[MAX_PERIODIC_ANNOUNCEMENTS][80];/* Sound files: Custom announce, no default */

	int count;			/*!< How many entries */
	int maxlen;			/*!< Max number of entries */
	int wrapuptime;			/*!< Wrapup Time */

	int retry;			/*!< Retry calling everyone after this amount of time */
	int timeout;			/*!< How long to wait for an answer */
	int weight;                     /*!< Respective weight */
	int autopause;			/*!< Auto pause queue members if they fail to answer */

	/* Queue strategy things */
	int rrpos;			/*!< Round Robin - position */
	int memberdelay;		/*!< Seconds to delay connecting member to caller */
	int autofill;			/*!< Ignore the head call status and ring an available agent */
	
	struct member *members;		/*!< Head of the list of members */
	struct queue_ent *head;		/*!< Head of the list of callers */
	AST_LIST_ENTRY(ast_call_queue) list;	/*!< Next call queue */
};

static AST_LIST_HEAD_STATIC(queues, ast_call_queue);

static int set_member_paused(char *queuename, char *interface, int paused);

static void set_queue_result(struct ast_channel *chan, enum queue_result res)
{
	int i;

	for (i = 0; i < sizeof(queue_results) / sizeof(queue_results[0]); i++) {
		if (queue_results[i].id == res) {
			pbx_builtin_setvar_helper(chan, "QUEUESTATUS", queue_results[i].text);
			return;
		}
	}
}

static char *int2strat(int strategy)
{
	int x;
	for (x=0;x<sizeof(strategies) / sizeof(strategies[0]);x++) {
		if (strategy == strategies[x].strategy)
			return strategies[x].name;
	}
	return "<unknown>";
}

static int strat2int(const char *strategy)
{
	int x;
	for (x=0;x<sizeof(strategies) / sizeof(strategies[0]);x++) {
		if (!strcasecmp(strategy, strategies[x].name))
			return strategies[x].strategy;
	}
	return -1;
}

/*! \brief Insert the 'new' entry after the 'prev' entry of queue 'q' */
static inline void insert_entry(struct ast_call_queue *q, struct queue_ent *prev, struct queue_ent *new, int *pos)
{
	struct queue_ent *cur;

	if (!q || !new)
		return;
	if (prev) {
		cur = prev->next;
		prev->next = new;
	} else {
		cur = q->head;
		q->head = new;
	}
	new->next = cur;
	new->parent = q;
	new->pos = ++(*pos);
	new->opos = *pos;
}

enum queue_member_status {
	QUEUE_NO_MEMBERS,
	QUEUE_NO_REACHABLE_MEMBERS,
	QUEUE_NORMAL
};

static enum queue_member_status get_member_status(const struct ast_call_queue *q, int max_penalty)
{
	struct member *member;
	enum queue_member_status result = QUEUE_NO_MEMBERS;

	for (member = q->members; member; member = member->next) {
		if (max_penalty && (member->penalty > max_penalty))
			continue;

		switch (member->status) {
		case AST_DEVICE_INVALID:
			/* nothing to do */
			break;
		case AST_DEVICE_UNAVAILABLE:
			result = QUEUE_NO_REACHABLE_MEMBERS;
			break;
		default:
			return QUEUE_NORMAL;
		}
	}
	
	return result;
}

struct statechange {
	int state;
	char dev[0];
};

static void *changethread(void *data)
{
	struct ast_call_queue *q;
	struct statechange *sc = data;
	struct member *cur;
	char *loc;
	char *technology;

	technology = ast_strdupa(sc->dev);
	loc = strchr(technology, '/');
	if (loc) {
		*loc = '\0';
		loc++;
	} else {
		free(sc);
		return NULL;
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Device '%s/%s' changed to state '%d' (%s)\n", technology, loc, sc->state, devstate2str(sc->state));
	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		ast_mutex_lock(&q->lock);
		cur = q->members;
		while(cur) {
			if (!strcasecmp(sc->dev, cur->interface)) {
				if (cur->status != sc->state) {
					cur->status = sc->state;
					if (!q->maskmemberstatus) {
						manager_event(EVENT_FLAG_AGENT, "QueueMemberStatus",
							"Queue: %s\r\n"
							"Location: %s\r\n"
							"Membership: %s\r\n"
							"Penalty: %d\r\n"
							"CallsTaken: %d\r\n"
							"LastCall: %d\r\n"
							"Status: %d\r\n"
							"Paused: %d\r\n",
						q->name, cur->interface, cur->dynamic ? "dynamic" : "static",
						cur->penalty, cur->calls, (int)cur->lastcall, cur->status, cur->paused);
					}
				}
			}
			cur = cur->next;
		}
		ast_mutex_unlock(&q->lock);
	}
	AST_LIST_UNLOCK(&queues);
	free(sc);
	return NULL;
}

static int statechange_queue(const char *dev, int state, void *ign)
{
	/* Avoid potential for deadlocks by spawning a new thread to handle
	   the event */
	struct statechange *sc;
	pthread_t t;
	pthread_attr_t attr;
	
	if ((sc = ast_calloc(1, sizeof(*sc) + strlen(dev) + 1))) {
		sc->state = state;
		strcpy(sc->dev, dev);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (ast_pthread_create(&t, &attr, changethread, sc)) {
			ast_log(LOG_WARNING, "Failed to create update thread!\n");
			free(sc);
		}
	}
	return 0;
}

static struct member *create_queue_member(char *interface, int penalty, int paused)
{
	struct member *cur;
	
	/* Add a new member */

	if ((cur = ast_calloc(1, sizeof(*cur)))) {
		cur->penalty = penalty;
		cur->paused = paused;
		ast_copy_string(cur->interface, interface, sizeof(cur->interface));
		if (!strchr(cur->interface, '/'))
			ast_log(LOG_WARNING, "No location at interface '%s'\n", interface);
		cur->status = ast_device_state(interface);
	}

	return cur;
}

static struct ast_call_queue *alloc_queue(const char *queuename)
{
	struct ast_call_queue *q;

	if ((q = ast_calloc(1, sizeof(*q)))) {
		ast_mutex_init(&q->lock);
		ast_copy_string(q->name, queuename, sizeof(q->name));
	}
	return q;
}

static void init_queue(struct ast_call_queue *q)
{
	int i;
	q->dead = 0;
	q->retry = DEFAULT_RETRY;
	q->timeout = -1;
	q->maxlen = 0;
	q->announcefrequency = 0;
	q->announceholdtime = 0;
	q->roundingseconds = 0; /* Default - don't announce seconds */
	q->servicelevel = 0;
	q->ringinuse = 1;
	q->autofill = autofill_default;
	q->moh[0] = '\0';
	q->announce[0] = '\0';
	q->context[0] = '\0';
	q->monfmt[0] = '\0';
	q->periodicannouncefrequency = 0;
	ast_copy_string(q->sound_next, "queue-youarenext", sizeof(q->sound_next));
	ast_copy_string(q->sound_thereare, "queue-thereare", sizeof(q->sound_thereare));
	ast_copy_string(q->sound_calls, "queue-callswaiting", sizeof(q->sound_calls));
	ast_copy_string(q->sound_holdtime, "queue-holdtime", sizeof(q->sound_holdtime));
	ast_copy_string(q->sound_minutes, "queue-minutes", sizeof(q->sound_minutes));
	ast_copy_string(q->sound_seconds, "queue-seconds", sizeof(q->sound_seconds));
	ast_copy_string(q->sound_thanks, "queue-thankyou", sizeof(q->sound_thanks));
	ast_copy_string(q->sound_lessthan, "queue-less-than", sizeof(q->sound_lessthan));
	ast_copy_string(q->sound_reporthold, "queue-reporthold", sizeof(q->sound_reporthold));
	ast_copy_string(q->sound_periodicannounce[0], "queue-periodic-announce", sizeof(q->sound_periodicannounce[0]));
	for (i=1;i<MAX_PERIODIC_ANNOUNCEMENTS;i++) {
		q->sound_periodicannounce[i][0]='\0';
	}
}

static void clear_queue(struct ast_call_queue *q)
{
	q->holdtime = 0;
	q->callscompleted = 0;
	q->callsabandoned = 0;
	q->callscompletedinsl = 0;
	q->wrapuptime = 0;
}

/*! \brief Configure a queue parameter.
\par
   For error reporting, line number is passed for .conf static configuration.
   For Realtime queues, linenum is -1.
   The failunknown flag is set for config files (and static realtime) to show
   errors for unknown parameters. It is cleared for dynamic realtime to allow
   extra fields in the tables. */
static void queue_set_param(struct ast_call_queue *q, const char *param, const char *val, int linenum, int failunknown)
{
	int i = 0;
	char *c, *lastc;
	char buff[80];
	if (!strcasecmp(param, "music") || !strcasecmp(param, "musiconhold")) {
		ast_copy_string(q->moh, val, sizeof(q->moh));
	} else if (!strcasecmp(param, "announce")) {
		ast_copy_string(q->announce, val, sizeof(q->announce));
	} else if (!strcasecmp(param, "context")) {
		ast_copy_string(q->context, val, sizeof(q->context));
	} else if (!strcasecmp(param, "timeout")) {
		q->timeout = atoi(val);
		if (q->timeout < 0)
			q->timeout = DEFAULT_TIMEOUT;
	} else if (!strcasecmp(param, "ringinuse")) {
		q->ringinuse = ast_true(val);
	} else if (!strcasecmp(param, "monitor-join")) {
		q->monjoin = ast_true(val);
	} else if (!strcasecmp(param, "monitor-format")) {
		ast_copy_string(q->monfmt, val, sizeof(q->monfmt));
	} else if (!strcasecmp(param, "queue-youarenext")) {
		ast_copy_string(q->sound_next, val, sizeof(q->sound_next));
	} else if (!strcasecmp(param, "queue-thereare")) {
		ast_copy_string(q->sound_thereare, val, sizeof(q->sound_thereare));
	} else if (!strcasecmp(param, "queue-callswaiting")) {
		ast_copy_string(q->sound_calls, val, sizeof(q->sound_calls));
	} else if (!strcasecmp(param, "queue-holdtime")) {
		ast_copy_string(q->sound_holdtime, val, sizeof(q->sound_holdtime));
	} else if (!strcasecmp(param, "queue-minutes")) {
		ast_copy_string(q->sound_minutes, val, sizeof(q->sound_minutes));
	} else if (!strcasecmp(param, "queue-seconds")) {
		ast_copy_string(q->sound_seconds, val, sizeof(q->sound_seconds));
	} else if (!strcasecmp(param, "queue-lessthan")) {
		ast_copy_string(q->sound_lessthan, val, sizeof(q->sound_lessthan));
	} else if (!strcasecmp(param, "queue-thankyou")) {
		ast_copy_string(q->sound_thanks, val, sizeof(q->sound_thanks));
	} else if (!strcasecmp(param, "queue-reporthold")) {
		ast_copy_string(q->sound_reporthold, val, sizeof(q->sound_reporthold));
	} else if (!strcasecmp(param, "announce-frequency")) {
		q->announcefrequency = atoi(val);
	} else if (!strcasecmp(param, "announce-round-seconds")) {
		q->roundingseconds = atoi(val);
		if (q->roundingseconds>60 || q->roundingseconds<0) {
			if (linenum >= 0) {
				ast_log(LOG_WARNING, "'%s' isn't a valid value for %s "
					"using 0 instead for queue '%s' at line %d of queues.conf\n",
					val, param, q->name, linenum);
			} else {
				ast_log(LOG_WARNING, "'%s' isn't a valid value for %s "
					"using 0 instead for queue '%s'\n", val, param, q->name);
			}
			q->roundingseconds=0;
		}
	} else if (!strcasecmp(param, "announce-holdtime")) {
		if (!strcasecmp(val, "once"))
			q->announceholdtime = ANNOUNCEHOLDTIME_ONCE;
		else if (ast_true(val))
			q->announceholdtime = ANNOUNCEHOLDTIME_ALWAYS;
		else
			q->announceholdtime = 0;
	} else if (!strcasecmp(param, "periodic-announce")) {
		if (strchr(val,'|')) {
			lastc = (char *)val;
			while ((c = strchr(lastc,'|'))) {
				if (i > MAX_PERIODIC_ANNOUNCEMENTS)
					break;
				strncpy(buff, lastc, abs(lastc - c));
				buff[abs(lastc - c)] = '\0';
				ast_copy_string(q->sound_periodicannounce[i], buff, sizeof(q->sound_periodicannounce[i]));
				lastc = (c + 1);
				i++;
			}
			if (strlen(lastc)) {
				ast_copy_string(q->sound_periodicannounce[i], lastc, sizeof(q->sound_periodicannounce[i]));
			}
		} else {
			ast_copy_string(q->sound_periodicannounce[i], val, sizeof(q->sound_periodicannounce[i]));
		}
	} else if (!strcasecmp(param, "periodic-announce-frequency")) {
		q->periodicannouncefrequency = atoi(val);
	} else if (!strcasecmp(param, "retry")) {
		q->retry = atoi(val);
		if (q->retry < 0)
			q->retry = DEFAULT_RETRY;
	} else if (!strcasecmp(param, "wrapuptime")) {
		q->wrapuptime = atoi(val);
	} else if (!strcasecmp(param, "autofill")) {
		q->autofill = ast_true(val);
	} else if (!strcasecmp(param, "autopause")) {
		q->autopause = ast_true(val);
	} else if (!strcasecmp(param, "maxlen")) {
		q->maxlen = atoi(val);
		if (q->maxlen < 0)
			q->maxlen = 0;
	} else if (!strcasecmp(param, "servicelevel")) {
		q->servicelevel= atoi(val);
	} else if (!strcasecmp(param, "strategy")) {
		q->strategy = strat2int(val);
		if (q->strategy < 0) {
			ast_log(LOG_WARNING, "'%s' isn't a valid strategy for queue '%s', using ringall instead\n",
				val, q->name);
			q->strategy = 0;
		}
	} else if (!strcasecmp(param, "joinempty")) {
		if (!strcasecmp(val, "strict"))
			q->joinempty = QUEUE_EMPTY_STRICT;
		else if (ast_true(val))
			q->joinempty = QUEUE_EMPTY_NORMAL;
		else
			q->joinempty = 0;
	} else if (!strcasecmp(param, "leavewhenempty")) {
		if (!strcasecmp(val, "strict"))
			q->leavewhenempty = QUEUE_EMPTY_STRICT;
		else if (ast_true(val))
			q->leavewhenempty = QUEUE_EMPTY_NORMAL;
		else
			q->leavewhenempty = 0;
	} else if (!strcasecmp(param, "eventmemberstatus")) {
		q->maskmemberstatus = !ast_true(val);
	} else if (!strcasecmp(param, "eventwhencalled")) {
		q->eventwhencalled = ast_true(val);
	} else if (!strcasecmp(param, "reportholdtime")) {
		q->reportholdtime = ast_true(val);
	} else if (!strcasecmp(param, "memberdelay")) {
		q->memberdelay = atoi(val);
	} else if (!strcasecmp(param, "weight")) {
		q->weight = atoi(val);
		if (q->weight)
			use_weight++;
		/* With Realtime queues, if the last queue using weights is deleted in realtime,
		   we will not see any effect on use_weight until next reload. */
	} else if (!strcasecmp(param, "timeoutrestart")) {
		q->timeoutrestart = ast_true(val);
	} else if(failunknown) {
		if (linenum >= 0) {
			ast_log(LOG_WARNING, "Unknown keyword in queue '%s': %s at line %d of queues.conf\n",
				q->name, param, linenum);
		} else {
			ast_log(LOG_WARNING, "Unknown keyword in queue '%s': %s\n", q->name, param);
		}
	}
}

static void rt_handle_member_record(struct ast_call_queue *q, char *interface, const char *penalty_str)
{
	struct member *m, *prev_m;
	int penalty = 0;

	if(penalty_str) {
		penalty = atoi(penalty_str);
		if(penalty < 0)
			penalty = 0;
	}

	/* Find the member, or the place to put a new one. */
	prev_m = NULL;
	m = q->members;
	while (m && strcmp(m->interface, interface)) {
		prev_m = m;
		m = m->next;
	}

	/* Create a new one if not found, else update penalty */
	if (!m) {
		m = create_queue_member(interface, penalty, 0);
		if (m) {
			m->dead = 0;
			if (prev_m) {
				prev_m->next = m;
			} else {
				q->members = m;
			}
		}
	} else {
		m->dead = 0;	/* Do not delete this one. */
		m->penalty = penalty;
	}
}

static void free_members(struct ast_call_queue *q, int all)
{
	/* Free non-dynamic members */
	struct member *curm, *next, *prev = NULL;

	for (curm = q->members; curm; curm = next) {
		next = curm->next;
		if (all || !curm->dynamic) {
			if (prev)
				prev->next = next;
			else
				q->members = next;
			free(curm);
		} else 
			prev = curm;
	}
}

static void destroy_queue(struct ast_call_queue *q)
{
	free_members(q, 1);
	ast_mutex_destroy(&q->lock);
	free(q);
}

/*!\brief Reload a single queue via realtime.
   \return Return the queue, or NULL if it doesn't exist.
   \note Should be called with the global qlock locked. */
static struct ast_call_queue *find_queue_by_name_rt(const char *queuename, struct ast_variable *queue_vars, struct ast_config *member_config)
{
	struct ast_variable *v;
	struct ast_call_queue *q;
	struct member *m, *prev_m, *next_m;
	char *interface;
	char *tmp, *tmp_name;
	char tmpbuf[64];	/* Must be longer than the longest queue param name. */

	/* Find the queue in the in-core list (we will create a new one if not found). */
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!strcasecmp(q->name, queuename)) {
			break;
		}
	}

	/* Static queues override realtime. */
	if (q) {
		ast_mutex_lock(&q->lock);
		if (!q->realtime) {
			if (q->dead) {
				ast_mutex_unlock(&q->lock);
				return NULL;
			} else {
				ast_mutex_unlock(&q->lock);
				return q;
			}
		}
	} else if (!member_config)
		/* Not found in the list, and it's not realtime ... */
		return NULL;

	/* Check if queue is defined in realtime. */
	if (!queue_vars) {
		/* Delete queue from in-core list if it has been deleted in realtime. */
		if (q) {
			/*! \note Hmm, can't seem to distinguish a DB failure from a not
			   found condition... So we might delete an in-core queue
			   in case of DB failure. */
			ast_log(LOG_DEBUG, "Queue %s not found in realtime.\n", queuename);

			q->dead = 1;
			/* Delete if unused (else will be deleted when last caller leaves). */
			if (!q->count) {
				/* Delete. */
				AST_LIST_REMOVE(&queues, q, list);
				ast_mutex_unlock(&q->lock);
				destroy_queue(q);
			} else
				ast_mutex_unlock(&q->lock);
		}
		return NULL;
	}

	/* Create a new queue if an in-core entry does not exist yet. */
	if (!q) {
		if (!(q = alloc_queue(queuename)))
			return NULL;
		ast_mutex_lock(&q->lock);
		clear_queue(q);
		q->realtime = 1;
		AST_LIST_INSERT_HEAD(&queues, q, list);
	}
	init_queue(q);		/* Ensure defaults for all parameters not set explicitly. */

	v = queue_vars;
	memset(tmpbuf, 0, sizeof(tmpbuf));
	while(v) {
		/* Convert to dashes `-' from underscores `_' as the latter are more SQL friendly. */
		if((tmp = strchr(v->name, '_')) != NULL) {
			ast_copy_string(tmpbuf, v->name, sizeof(tmpbuf));
			tmp_name = tmpbuf;
			tmp = tmp_name;
			while((tmp = strchr(tmp, '_')) != NULL)
				*tmp++ = '-';
		} else
			tmp_name = v->name;
		queue_set_param(q, tmp_name, v->value, -1, 0);
		v = v->next;
	}

	/* Temporarily set non-dynamic members dead so we can detect deleted ones. */
	m = q->members;
	while (m) {
		if (!m->dynamic)
			m->dead = 1;
		m = m->next;
	}

	interface = ast_category_browse(member_config, NULL);
	while (interface) {
		rt_handle_member_record(q, interface, ast_variable_retrieve(member_config, interface, "penalty"));
		interface = ast_category_browse(member_config, interface);
	}

	/* Delete all realtime members that have been deleted in DB. */
	m = q->members;
	prev_m = NULL;
	while (m) {
		next_m = m->next;
		if (m->dead) {
			if (prev_m) {
				prev_m->next = next_m;
			} else {
				q->members = next_m;
			}
			free(m);
		} else {
			prev_m = m;
		}
		m = next_m;
	}

	ast_mutex_unlock(&q->lock);

	return q;
}

static struct ast_call_queue *load_realtime_queue(char *queuename)
{
	struct ast_variable *queue_vars = NULL;
	struct ast_config *member_config = NULL;
	struct ast_call_queue *q;

	/* Find the queue in the in-core list first. */
	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!strcasecmp(q->name, queuename)) {
			break;
		}
	}
	AST_LIST_UNLOCK(&queues);

	if (!q || q->realtime) {
		/*! \note Load from realtime before taking the global qlock, to avoid blocking all
		   queue operations while waiting for the DB.

		   This will be two separate database transactions, so we might
		   see queue parameters as they were before another process
		   changed the queue and member list as it was after the change.
		   Thus we might see an empty member list when a queue is
		   deleted. In practise, this is unlikely to cause a problem. */

		queue_vars = ast_load_realtime("queues", "name", queuename, NULL);
		if (queue_vars) {
			member_config = ast_load_realtime_multientry("queue_members", "interface LIKE", "%", "queue_name", queuename, NULL);
			if (!member_config) {
				ast_log(LOG_ERROR, "no queue_members defined in your config (extconfig.conf).\n");
				return NULL;
			}
		}

		AST_LIST_LOCK(&queues);

		q = find_queue_by_name_rt(queuename, queue_vars, member_config);
		if (member_config)
			ast_config_destroy(member_config);
		if (queue_vars)
			ast_variables_destroy(queue_vars);

		AST_LIST_UNLOCK(&queues);
	}
	return q;
}

static int join_queue(char *queuename, struct queue_ent *qe, enum queue_result *reason)
{
	struct ast_call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int res = -1;
	int pos = 0;
	int inserted = 0;
	enum queue_member_status stat;

	q = load_realtime_queue(queuename);
	if (!q)
		return res;

	AST_LIST_LOCK(&queues);
	ast_mutex_lock(&q->lock);

	/* This is our one */
	stat = get_member_status(q, qe->max_penalty);
	if (!q->joinempty && (stat == QUEUE_NO_MEMBERS))
		*reason = QUEUE_JOINEMPTY;
	else if ((q->joinempty == QUEUE_EMPTY_STRICT) && (stat == QUEUE_NO_REACHABLE_MEMBERS))
		*reason = QUEUE_JOINUNAVAIL;
	else if (q->maxlen && (q->count >= q->maxlen))
		*reason = QUEUE_FULL;
	else {
		/* There's space for us, put us at the right position inside
		 * the queue. 
		 * Take into account the priority of the calling user */
		inserted = 0;
		prev = NULL;
		cur = q->head;
		while(cur) {
			/* We have higher priority than the current user, enter
			 * before him, after all the other users with priority
			 * higher or equal to our priority. */
			if ((!inserted) && (qe->prio > cur->prio)) {
				insert_entry(q, prev, qe, &pos);
				inserted = 1;
			}
			cur->pos = ++pos;
			prev = cur;
			cur = cur->next;
		}
		/* No luck, join at the end of the queue */
		if (!inserted)
			insert_entry(q, prev, qe, &pos);
		ast_copy_string(qe->moh, q->moh, sizeof(qe->moh));
		ast_copy_string(qe->announce, q->announce, sizeof(qe->announce));
		ast_copy_string(qe->context, q->context, sizeof(qe->context));
		q->count++;
		res = 0;
		/* XXX missing CalledIDnum ? */
		manager_event(EVENT_FLAG_CALL, "Join", 
			      "Channel: %s\r\nCallerID: %s\r\nCallerIDName: %s\r\nQueue: %s\r\nPosition: %d\r\nCount: %d\r\nUniqueid: %s\r\n",
			      qe->chan->name, 
			      S_OR(qe->chan->cid.cid_num, "unknown"), /* XXX somewhere else it is <unknown> */
			      S_OR(qe->chan->cid.cid_name, "unknown"),
			      q->name, qe->pos, q->count, qe->chan->uniqueid );
#if 0
ast_log(LOG_NOTICE, "Queue '%s' Join, Channel '%s', Position '%d'\n", q->name, qe->chan->name, qe->pos );
#endif
	}
	ast_mutex_unlock(&q->lock);
	AST_LIST_UNLOCK(&queues);
	return res;
}

static int play_file(struct ast_channel *chan, char *filename)
{
	int res;

	ast_stopstream(chan);
	res = ast_streamfile(chan, filename, chan->language);

	if (!res)
		res = ast_waitstream(chan, AST_DIGIT_ANY);
	else
		res = 0;

	ast_stopstream(chan);

	return res;
}

static int valid_exit(struct queue_ent *qe, char digit)
{
	int digitlen = strlen(qe->digits);

	/* Prevent possible buffer overflow */
	if (digitlen < sizeof(qe->digits) - 2) {
		qe->digits[digitlen] = digit;
		qe->digits[digitlen + 1] = '\0';
	} else {
		qe->digits[0] = '\0';
		return 0;
	}

 	/* If there's no context to goto, short-circuit */
	if (ast_strlen_zero(qe->context))
		return 0;

	/* If the extension is bad, then reset the digits to blank */
	if (!ast_canmatch_extension(qe->chan, qe->context, qe->digits, 1, qe->chan->cid.cid_num)) {
		qe->digits[0] = '\0';
		return 0;
	}

	/* We have an exact match */
	if (!ast_goto_if_exists(qe->chan, qe->context, qe->digits, 1)) {
		/* Return 1 on a successful goto */
		return 1;
	}
	return 0;
}

static int say_position(struct queue_ent *qe)
{
	int res = 0, avgholdmins, avgholdsecs;
	time_t now;

	/* Check to see if this is ludicrous -- if we just announced position, don't do it again*/
	time(&now);
	if ( (now - qe->last_pos) < 15 )
		return 0;

	/* If either our position has changed, or we are over the freq timer, say position */
	if ( (qe->last_pos_said == qe->pos) && ((now - qe->last_pos) < qe->parent->announcefrequency) )
		return 0;

	ast_moh_stop(qe->chan);
	/* Say we're next, if we are */
	if (qe->pos == 1) {
		res = play_file(qe->chan, qe->parent->sound_next);
		if (res && valid_exit(qe, res))
			goto playout;
		else
			goto posout;
	} else {
		res = play_file(qe->chan, qe->parent->sound_thereare);
		if (res && valid_exit(qe, res))
			goto playout;
		res = ast_say_number(qe->chan, qe->pos, AST_DIGIT_ANY, qe->chan->language, (char *) NULL); /* Needs gender */
		if (res && valid_exit(qe, res))
			goto playout;
		res = play_file(qe->chan, qe->parent->sound_calls);
		if (res && valid_exit(qe, res))
			goto playout;
	}
	/* Round hold time to nearest minute */
	avgholdmins = abs(( (qe->parent->holdtime + 30) - (now - qe->start) ) / 60);

	/* If they have specified a rounding then round the seconds as well */
	if(qe->parent->roundingseconds) {
		avgholdsecs = (abs(( (qe->parent->holdtime + 30) - (now - qe->start) )) - 60 * avgholdmins) / qe->parent->roundingseconds;
		avgholdsecs*= qe->parent->roundingseconds;
	} else {
		avgholdsecs=0;
	}

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Hold time for %s is %d minutes %d seconds\n", qe->parent->name, avgholdmins, avgholdsecs);

	/* If the hold time is >1 min, if it's enabled, and if it's not
	   supposed to be only once and we have already said it, say it */
	if ((avgholdmins+avgholdsecs) > 0 && (qe->parent->announceholdtime) &&
	    (!(qe->parent->announceholdtime == ANNOUNCEHOLDTIME_ONCE) && qe->last_pos)) {
		res = play_file(qe->chan, qe->parent->sound_holdtime);
		if (res && valid_exit(qe, res))
			goto playout;

		if (avgholdmins>0) {
			if (avgholdmins < 2) {
				res = play_file(qe->chan, qe->parent->sound_lessthan);
				if (res && valid_exit(qe, res))
					goto playout;

				res = ast_say_number(qe->chan, 2, AST_DIGIT_ANY, qe->chan->language, (char *)NULL);
				if (res && valid_exit(qe, res))
					goto playout;
			} else {
				res = ast_say_number(qe->chan, avgholdmins, AST_DIGIT_ANY, qe->chan->language, (char*) NULL);
				if (res && valid_exit(qe, res))
					goto playout;
			}
			
			res = play_file(qe->chan, qe->parent->sound_minutes);
			if (res && valid_exit(qe, res))
				goto playout;
		}
		if (avgholdsecs>0) {
			res = ast_say_number(qe->chan, avgholdsecs, AST_DIGIT_ANY, qe->chan->language, (char*) NULL);
			if (res && valid_exit(qe, res))
				goto playout;

			res = play_file(qe->chan, qe->parent->sound_seconds);
			if (res && valid_exit(qe, res))
				goto playout;
		}

	}

 posout:
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Told %s in %s their queue position (which was %d)\n",
			    qe->chan->name, qe->parent->name, qe->pos);
	res = play_file(qe->chan, qe->parent->sound_thanks);

 playout:
	/* Set our last_pos indicators */
 	qe->last_pos = now;
	qe->last_pos_said = qe->pos;
	ast_moh_start(qe->chan, qe->moh);

	return res;
}

static void recalc_holdtime(struct queue_ent *qe)
{
	int oldvalue, newvalue;

	/* Calculate holdtime using a recursive boxcar filter */
	/* Thanks to SRT for this contribution */
	/* 2^2 (4) is the filter coefficient; a higher exponent would give old entries more weight */

	newvalue = time(NULL) - qe->start;

	ast_mutex_lock(&qe->parent->lock);
	if (newvalue <= qe->parent->servicelevel)
		qe->parent->callscompletedinsl++;
	oldvalue = qe->parent->holdtime;
	qe->parent->holdtime = (((oldvalue << 2) - oldvalue) + newvalue) >> 2;
	ast_mutex_unlock(&qe->parent->lock);
}


static void leave_queue(struct queue_ent *qe)
{
	struct ast_call_queue *q;
	struct queue_ent *cur, *prev = NULL;
	int pos = 0;

	q = qe->parent;
	if (!q)
		return;
	ast_mutex_lock(&q->lock);

	prev = NULL;
	cur = q->head;
	while(cur) {
		if (cur == qe) {
			q->count--;

			/* Take us out of the queue */
			manager_event(EVENT_FLAG_CALL, "Leave",
				"Channel: %s\r\nQueue: %s\r\nCount: %d\r\n",
				qe->chan->name, q->name,  q->count);
#if 0
ast_log(LOG_NOTICE, "Queue '%s' Leave, Channel '%s'\n", q->name, qe->chan->name );
#endif
			/* Take us out of the queue */
			if (prev)
				prev->next = cur->next;
			else
				q->head = cur->next;
		} else {
			/* Renumber the people after us in the queue based on a new count */
			cur->pos = ++pos;
			prev = cur;
		}
		cur = cur->next;
	}
	ast_mutex_unlock(&q->lock);
	if (q->dead && !q->count) {	
		/* It's dead and nobody is in it, so kill it */
		AST_LIST_LOCK(&queues);
		AST_LIST_REMOVE(&queues, q, list);
		AST_LIST_UNLOCK(&queues);
		destroy_queue(q);
	}
}

/* Hang up a list of outgoing calls */
static void hangupcalls(struct callattempt *outgoing, struct ast_channel *exception)
{
	struct callattempt *oo;

	while(outgoing) {
		/* Hangup any existing lines we have open */
		if (outgoing->chan && (outgoing->chan != exception))
			ast_hangup(outgoing->chan);
		oo = outgoing;
		outgoing=outgoing->q_next;
		free(oo);
	}
}

static int update_status(struct ast_call_queue *q, struct member *member, int status)
{
	struct member *cur;

	/* Since a reload could have taken place, we have to traverse the list to
		be sure it's still valid */
	ast_mutex_lock(&q->lock);
	for (cur = q->members; cur; cur = cur->next) {
		if (member == cur) {
			cur->status = status;
			if (!q->maskmemberstatus) {
				manager_event(EVENT_FLAG_AGENT, "QueueMemberStatus",
					"Queue: %s\r\n"
					"Location: %s\r\n"
					"Membership: %s\r\n"
					"Penalty: %d\r\n"
					"CallsTaken: %d\r\n"
					"LastCall: %d\r\n"
					"Status: %d\r\n"
					"Paused: %d\r\n",
				q->name, cur->interface, cur->dynamic ? "dynamic" : "static",
				cur->penalty, cur->calls, (int)cur->lastcall, cur->status, cur->paused);
			}
			break;
		}
	}
	ast_mutex_unlock(&q->lock);
	return 0;
}

static int update_dial_status(struct ast_call_queue *q, struct member *member, int status)
{
	if (status == AST_CAUSE_BUSY)
		status = AST_DEVICE_BUSY;
	else if (status == AST_CAUSE_UNREGISTERED)
		status = AST_DEVICE_UNAVAILABLE;
	else if (status == AST_CAUSE_NOSUCHDRIVER)
		status = AST_DEVICE_INVALID;
	else
		status = AST_DEVICE_UNKNOWN;
	return update_status(q, member, status);
}

/* traverse all defined queues which have calls waiting and contain this member
   return 0 if no other queue has precedence (higher weight) or 1 if found  */
static int compare_weight(struct ast_call_queue *rq, struct member *member)
{
	struct ast_call_queue *q;
	struct member *mem;
	int found = 0;
	
	/* &qlock and &rq->lock already set by try_calling()
	 * to solve deadlock */
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (q == rq) /* don't check myself, could deadlock */
			continue; 
		ast_mutex_lock(&q->lock);
		if (q->count && q->members) {
			for (mem = q->members; mem; mem = mem->next) {
				if (!strcmp(mem->interface, member->interface)) {
					ast_log(LOG_DEBUG, "Found matching member %s in queue '%s'\n", mem->interface, q->name);
					if (q->weight > rq->weight) {
						ast_log(LOG_DEBUG, "Queue '%s' (weight %d, calls %d) is preferred over '%s' (weight %d, calls %d)\n", q->name, q->weight, q->count, rq->name, rq->weight, rq->count);
						found = 1;
						break;
					}
				}
			}
		}
		ast_mutex_unlock(&q->lock);
		if (found) 
			break;
	}
	return found;
}

/*! \brief common hangup actions */
static void do_hang(struct callattempt *o)
{
	o->stillgoing = 0;
	ast_hangup(o->chan);
	o->chan = NULL;
}

static int ring_entry(struct queue_ent *qe, struct callattempt *tmp, int *busies)
{
	int res;
	int status;
	char tech[256];
	char *location;

	/* on entry here, we know that tmp->chan == NULL */
	if (qe->parent->wrapuptime && (time(NULL) - tmp->lastcall < qe->parent->wrapuptime)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Wrapuptime not yet expired for %s\n", tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		(*busies)++;
		return 0;
	}

	if (!qe->parent->ringinuse && (tmp->member->status == AST_DEVICE_INUSE)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "%s in use, can't receive call\n", tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		return 0;
	}

	if (tmp->member->paused) {
		if (option_debug)
			ast_log(LOG_DEBUG, "%s paused, can't receive call\n", tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		return 0;
	}
	if (use_weight && compare_weight(qe->parent,tmp->member)) {
		ast_log(LOG_DEBUG, "Priority queue delaying call to %s:%s\n", qe->parent->name, tmp->interface);
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		(*busies)++;
		return 0;
	}

	ast_copy_string(tech, tmp->interface, sizeof(tech));
	if ((location = strchr(tech, '/')))
		*location++ = '\0';
	else
		location = "";

	/* Request the peer */
	tmp->chan = ast_request(tech, qe->chan->nativeformats, location, &status);
	if (!tmp->chan) {			/* If we can't, just go on to the next call */
#if 0
		ast_log(LOG_NOTICE, "Unable to create channel of type '%s' for Queue\n", cur->tech);
#endif			
		if (qe->chan->cdr)
			ast_cdr_busy(qe->chan->cdr);
		tmp->stillgoing = 0;
		update_dial_status(qe->parent, tmp->member, status);
		(*busies)++;
		return 0;
	} else if (status != tmp->oldstatus) 
		update_dial_status(qe->parent, tmp->member, status);
	
	tmp->chan->appl = "AppQueue";
	tmp->chan->data = "(Outgoing Line)";
	tmp->chan->whentohangup = 0;
	if (tmp->chan->cid.cid_num)
		free(tmp->chan->cid.cid_num);
	tmp->chan->cid.cid_num = ast_strdup(qe->chan->cid.cid_num);
	if (tmp->chan->cid.cid_name)
		free(tmp->chan->cid.cid_name);
	tmp->chan->cid.cid_name = ast_strdup(qe->chan->cid.cid_name);
	if (tmp->chan->cid.cid_ani)
		free(tmp->chan->cid.cid_ani);
	tmp->chan->cid.cid_ani = ast_strdup(qe->chan->cid.cid_ani);

	/* Inherit specially named variables from parent channel */
	ast_channel_inherit_variables(qe->chan, tmp->chan);

	/* Presense of ADSI CPE on outgoing channel follows ours */
	tmp->chan->adsicpe = qe->chan->adsicpe;

	/* Place the call, but don't wait on the answer */
	res = ast_call(tmp->chan, location, 0);
	if (res) {
		/* Again, keep going even if there's an error */
		if (option_debug)
			ast_log(LOG_DEBUG, "ast call on peer returned %d\n", res);
		else if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Couldn't call %s\n", tmp->interface);
		do_hang(tmp);
		(*busies)++;
		return 0;
	} else {
		if (qe->parent->eventwhencalled) {
			manager_event(EVENT_FLAG_AGENT, "AgentCalled",
						"AgentCalled: %s\r\n"
						"ChannelCalling: %s\r\n"
						"CallerID: %s\r\n"
						"CallerIDName: %s\r\n"
						"Context: %s\r\n"
						"Extension: %s\r\n"
						"Priority: %d\r\n",
						tmp->interface, qe->chan->name,
						S_OR(tmp->chan->cid.cid_num, "unknown"),
						S_OR(tmp->chan->cid.cid_name, "unknown"),
						qe->chan->context, qe->chan->exten, qe->chan->priority);
		}
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Called %s\n", tmp->interface);
	}
	return 1;
}

/*! \brief find the entry with the best metric, or NULL */
static struct callattempt *find_best(struct callattempt *outgoing)
{
	struct callattempt *best = NULL, *cur;

	for (cur = outgoing; cur; cur = cur->q_next) {
		if (cur->stillgoing &&					/* Not already done */
			!cur->chan &&					/* Isn't already going */
			(!best || cur->metric < best->metric)) {	/* We haven't found one yet, or it's better */
				best = cur;
		}
	}
	return best;
}

static int ring_one(struct queue_ent *qe, struct callattempt *outgoing, int *busies)
{
	int ret = 0;

	while (ret == 0) {
		struct callattempt *best = find_best(outgoing);
		if (!best) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Nobody left to try ringing in queue\n");
			break;
		}
		if (!qe->parent->strategy) {
			struct callattempt *cur;
			/* Ring everyone who shares this best metric (for ringall) */
			for (cur = outgoing; cur; cur = cur->q_next) {
				if (cur->stillgoing && !cur->chan && cur->metric <= best->metric) {
					if (option_debug)
						ast_log(LOG_DEBUG, "(Parallel) Trying '%s' with metric %d\n", cur->interface, cur->metric);
					ring_entry(qe, cur, busies);
				}
			}
		} else {
			/* Ring just the best channel */
			if (option_debug)
				ast_log(LOG_DEBUG, "Trying '%s' with metric %d\n", best->interface, best->metric);
			ring_entry(qe, best, busies);
		}
		if (best->chan) /* break out with result = 1 */
			ret = 1;
	}
	return ret;
}

static int store_next(struct queue_ent *qe, struct callattempt *outgoing)
{
	struct callattempt *best = find_best(outgoing);

	if (best) {
		/* Ring just the best channel */
		if (option_debug)
			ast_log(LOG_DEBUG, "Next is '%s' with metric %d\n", best->interface, best->metric);
		qe->parent->rrpos = best->metric % 1000;
	} else {
		/* Just increment rrpos */
		if (qe->parent->wrapped) {
			/* No more channels, start over */
			qe->parent->rrpos = 0;
		} else {
			/* Prioritize next entry */
			qe->parent->rrpos++;
		}
	}
	qe->parent->wrapped = 0;
	return 0;
}

static int background_file(struct queue_ent *qe, struct ast_channel *chan, char *filename)
{
	int res;

	ast_stopstream(chan);
	res = ast_streamfile(chan, filename, chan->language);

	if (!res) {
		/* Wait for a keypress */
		res = ast_waitstream(chan, AST_DIGIT_ANY);
		if (res <= 0 || !valid_exit(qe, res))
			res = 0;

		/* Stop playback */
		ast_stopstream(chan);
	} else {
		res = 0;
	}
	
	/*if (res) {
		ast_log(LOG_WARNING, "ast_streamfile failed on %s \n", chan->name);
		res = 0;
	}*/

	return res;
}

static int say_periodic_announcement(struct queue_ent *qe)
{
	int res = 0;
	time_t now;

	/* Get the current time */
	time(&now);

	/* Check to see if it is time to announce */
	if ((now - qe->last_periodic_announce_time) < qe->parent->periodicannouncefrequency)
		return 0;

	/* Stop the music on hold so we can play our own file */
	ast_moh_stop(qe->chan);

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Playing periodic announcement\n");

	/* Check to make sure we have a sound file. If not, reset to the first sound file */
	if (qe->last_periodic_announce_sound >= MAX_PERIODIC_ANNOUNCEMENTS || !strlen(qe->parent->sound_periodicannounce[qe->last_periodic_announce_sound])) {
		qe->last_periodic_announce_sound = 0;
	}
	
	/* play the announcement */
	res = background_file(qe, qe->chan, qe->parent->sound_periodicannounce[qe->last_periodic_announce_sound]);

	/* Resume Music on Hold */
	ast_moh_start(qe->chan, qe->moh);

	/* update last_periodic_announce_time */
	qe->last_periodic_announce_time = now;

	/* Update the current periodic announcement to the next announcement */
	qe->last_periodic_announce_sound++;
	
	return res;
}

static void record_abandoned(struct queue_ent *qe)
{
	ast_mutex_lock(&qe->parent->lock);
	manager_event(EVENT_FLAG_AGENT, "QueueCallerAbandon",
	              "Queue: %s\r\n"
	              "Uniqueid: %s\r\n"
	              "Position: %d\r\n"
	              "OriginalPosition: %d\r\n"
	              "HoldTime: %d\r\n",
	              qe->parent->name, qe->chan->uniqueid, qe->pos, qe->opos, (int)(time(NULL) - qe->start));

	qe->parent->callsabandoned++;
	ast_mutex_unlock(&qe->parent->lock);
}


#define AST_MAX_WATCHERS 256

static struct callattempt *wait_for_answer(struct queue_ent *qe, struct callattempt *outgoing, int *to, char *digit, int prebusies, int caller_disconnect)
{
	char *queue = qe->parent->name;
	struct callattempt *o;
	int status;
	int sentringing = 0;
	int numbusies = prebusies;
	int numnochan = 0;
	int stillgoing = 0;
	int orig = *to;
	struct ast_frame *f;
	struct callattempt *peer = NULL;
	struct ast_channel *winner;
	struct ast_channel *in = qe->chan;
	char on[256] = "";
	
	while(*to && !peer) {
		int numlines, retry, pos = 1;
		struct ast_channel *watchers[AST_MAX_WATCHERS];
		watchers[0] = in;

		for (retry = 0; retry < 2; retry++) {
			numlines = 0;
			for (o = outgoing; o; o = o->q_next) { /* Keep track of important channels */
				if (o->stillgoing) {	/* Keep track of important channels */
					stillgoing = 1;
					if (o->chan)
						watchers[pos++] = o->chan;
				}
				numlines++;
			}
			if (pos > 1 /* found */ || !stillgoing /* nobody listening */ ||
					 qe->parent->strategy /* ring would not be delivered */)
				break;
			/* On "ringall" strategy we only move to the next penalty level
			   when *all* ringing phones are done in the current penalty level */
			ring_one(qe, outgoing, &numbusies);
			/* and retry... */
		}
		if (pos == 1 /* not found */) {
			if (numlines == (numbusies + numnochan)) {
				ast_log(LOG_DEBUG, "Everyone is busy at this time\n");
			} else {
				ast_log(LOG_NOTICE, "No one is answering queue '%s' (%d/%d/%d)\n", queue, numlines, numbusies, numnochan);
			}
			*to = 0;
			return NULL;
		}
		winner = ast_waitfor_n(watchers, pos, to);
		for (o = outgoing; o; o = o->q_next) {
			if (o->stillgoing && (o->chan) &&  (o->chan->_state == AST_STATE_UP)) {
				if (!peer) {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
					peer = o;
				}
			} else if (o->chan && (o->chan == winner)) {
				ast_copy_string(on, o->member->interface, sizeof(on));
				if (!ast_strlen_zero(o->chan->call_forward)) {
					char tmpchan[256]="";
					char *stuff;
					char *tech;
					ast_copy_string(tmpchan, o->chan->call_forward, sizeof(tmpchan));
					if ((stuff = strchr(tmpchan, '/'))) {
						*stuff++ = '\0';
						tech = tmpchan;
					} else {
						snprintf(tmpchan, sizeof(tmpchan), "%s@%s", o->chan->call_forward, o->chan->context);
						stuff = tmpchan;
						tech = "Local";
					}
					/* Before processing channel, go ahead and check for forwarding */
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Now forwarding %s to '%s/%s' (thanks to %s)\n", in->name, tech, stuff, o->chan->name);
					/* Setup parameters */
					o->chan = ast_request(tech, in->nativeformats, stuff, &status);
					if (status != o->oldstatus) 
						update_dial_status(qe->parent, o->member, status);						
					if (!o->chan) {
						ast_log(LOG_NOTICE, "Unable to create local channel for call forward to '%s/%s'\n", tech, stuff);
						o->stillgoing = 0;
						numnochan++;
					} else {
						if (o->chan->cid.cid_num)
							free(o->chan->cid.cid_num);
						o->chan->cid.cid_num = ast_strdup(in->cid.cid_num);

						if (o->chan->cid.cid_name)
							free(o->chan->cid.cid_name);
						o->chan->cid.cid_name = ast_strdup(in->cid.cid_name);

						ast_string_field_set(o->chan, accountcode, in->accountcode);
						o->chan->cdrflags = in->cdrflags;

						if (in->cid.cid_ani) {
							if (o->chan->cid.cid_ani)
								free(o->chan->cid.cid_ani);
							o->chan->cid.cid_ani = ast_strdup(in->cid.cid_ani);
						}
						if (o->chan->cid.cid_rdnis) 
							free(o->chan->cid.cid_rdnis);
						o->chan->cid.cid_rdnis = ast_strdup(S_OR(in->macroexten, in->exten));
						if (ast_call(o->chan, tmpchan, 0)) {
							ast_log(LOG_NOTICE, "Failed to dial on local channel for call forward to '%s'\n", tmpchan);
							do_hang(o);
							numnochan++;
						}
					}
					/* Hangup the original channel now, in case we needed it */
					ast_hangup(winner);
					continue;
				}
				f = ast_read(winner);
				if (f) {
					if (f->frametype == AST_FRAME_CONTROL) {
						switch(f->subclass) {
						case AST_CONTROL_ANSWER:
							/* This is our guy if someone answered. */
							if (!peer) {
								if (option_verbose > 2)
									ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
								peer = o;
							}
							break;
						case AST_CONTROL_BUSY:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is busy\n", o->chan->name);
							if (in->cdr)
								ast_cdr_busy(in->cdr);
							do_hang(o);
							if (qe->parent->strategy) {
								if (qe->parent->timeoutrestart)
									*to = orig;
								ring_one(qe, outgoing, &numbusies);
							}
							numbusies++;
							break;
						case AST_CONTROL_CONGESTION:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is circuit-busy\n", o->chan->name);
							if (in->cdr)
								ast_cdr_busy(in->cdr);
							do_hang(o);
							if (qe->parent->strategy) {
								if (qe->parent->timeoutrestart)
									*to = orig;
								ring_one(qe, outgoing, &numbusies);
							}
							numbusies++;
							break;
						case AST_CONTROL_RINGING:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is ringing\n", o->chan->name);
							if (!sentringing) {
#if 0
								ast_indicate(in, AST_CONTROL_RINGING);
#endif								
								sentringing++;
							}
							break;
						case AST_CONTROL_OFFHOOK:
							/* Ignore going off hook */
							break;
						default:
							ast_log(LOG_DEBUG, "Dunno what to do with control type %d\n", f->subclass);
						}
					}
					ast_frfree(f);
				} else {
					do_hang(o);
					if (qe->parent->strategy) {
						if (qe->parent->timeoutrestart)
							*to = orig;
						ring_one(qe, outgoing, &numbusies);
					}
				}
			}
		}
		if (winner == in) {
			f = ast_read(in);
#if 0
			if (f && (f->frametype != AST_FRAME_VOICE))
					printf("Frame type: %d, %d\n", f->frametype, f->subclass);
			else if (!f || (f->frametype != AST_FRAME_VOICE))
				printf("Hangup received on %s\n", in->name);
#endif
			if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP))) {
				/* Got hung up */
				*to=-1;
				if (f)
					ast_frfree(f);
				return NULL;
			}
			if ((f->frametype == AST_FRAME_DTMF) && caller_disconnect && (f->subclass == '*')) {
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
				*to=0;
				ast_frfree(f);
				return NULL;
			}
			if ((f->frametype == AST_FRAME_DTMF) && (f->subclass != '*') && valid_exit(qe, f->subclass)) {
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_3 "User pressed digit: %c\n", f->subclass);
				*to=0;
				*digit=f->subclass;
				ast_frfree(f);
				return NULL;
			}
			ast_frfree(f);
		}
		if (!*to) {
			if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3 "Nobody picked up in %d ms\n", orig);
			if (qe->parent->autopause) {
				if (!set_member_paused(qe->parent->name, on, 1)) {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "Auto-Pausing Queue Member %s in queue %s since they failed to answer.\n", on, qe->parent->name);
				} else {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "Failed to pause Queue Member %s in queue %s!\n", on, qe->parent->name);
				}
			}
		}
	}

	return peer;
	
}

static int is_our_turn(struct queue_ent *qe)
{
	struct queue_ent *ch;
	struct member *cur;
	int avl = 0;
	int idx = 0;
	int res;

	if (!qe->parent->autofill) {

		/* Atomically read the parent head -- does not need a lock */
		ch = qe->parent->head;
		/* If we are now at the top of the head, break out */
		if ((ch == qe) || (qe->parent->autofill)) {
			if (option_debug)
				ast_log(LOG_DEBUG, "It's our turn (%s).\n", qe->chan->name);
			res = 1;
		} else {
			if (option_debug)
				ast_log(LOG_DEBUG, "It's not our turn (%s).\n", qe->chan->name);
			res = 0;
		}	

	} else {

		/* This needs a lock. How many members are available to be served? */
	
		ast_mutex_lock(&qe->parent->lock);
			
		ch = qe->parent->head;
		cur = qe->parent->members;
	
		while (cur) {
			if (cur->status == 1) 
				avl++;
			cur = cur->next;
		}

		if (option_debug)
			ast_log(LOG_DEBUG, "There are %d available members.\n", avl);
	
		if (qe->parent->strategy == 1) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Even though there are %d available members, the strategy is ringall so only the head call is allowed in!\n", avl);
			avl = 1;
		}
	
		while ((idx < avl) && (ch) && (ch != qe)) {
			idx++;
			ch = ch->next;			
		}
	
		/* If the queue entry is within avl [the number of available members] calls from the top ... */
		if (ch && idx < avl) {
			if (option_debug)
				ast_log(LOG_DEBUG, "It's our turn (%s).\n", qe->chan->name);
			res = 1;
		} else {
			if (option_debug)
				ast_log(LOG_DEBUG, "It's not our turn (%s).\n", qe->chan->name);
			res = 0;
		}
		
		ast_mutex_unlock(&qe->parent->lock);
	}

	return res;
}

static int wait_our_turn(struct queue_ent *qe, int ringing, enum queue_result *reason)
{
	int res = 0;

	/* This is the holding pen for callers 2 through maxlen */
	for (;;) {
		enum queue_member_status stat;

		if (is_our_turn(qe))
			break;

		/* If we have timed out, break out */
		if (qe->expire && (time(NULL) > qe->expire)) {
			*reason = QUEUE_TIMEOUT;
			ast_queue_log(qe->parent->name, qe->chan->uniqueid,"NONE", "EXITWITHTIMEOUT", "%d", qe->pos);
			break;
		}

		stat = get_member_status(qe->parent, qe->max_penalty);

		/* leave the queue if no agents, if enabled */
		if (qe->parent->leavewhenempty && (stat == QUEUE_NO_MEMBERS)) {
			*reason = QUEUE_LEAVEEMPTY;
			leave_queue(qe);
			break;
		}

		/* leave the queue if no reachable agents, if enabled */
		if ((qe->parent->leavewhenempty == QUEUE_EMPTY_STRICT) && (stat == QUEUE_NO_REACHABLE_MEMBERS)) {
			*reason = QUEUE_LEAVEUNAVAIL;
			leave_queue(qe);
			break;
		}

		/* Make a position announcement, if enabled */
		if (qe->parent->announcefrequency && !ringing)
			res = say_position(qe);
		if (res)
			break;

		/* Make a periodic announcement, if enabled */
		if (qe->parent->periodicannouncefrequency && !ringing)
			res = say_periodic_announcement(qe);

		/* Wait a second before checking again */
		if (!res) res = ast_waitfordigit(qe->chan, RECHECK * 1000);
		if (res)
			break;
	}
	return res;
}

static int update_queue(struct ast_call_queue *q, struct member *member)
{
	struct member *cur;

	/* Since a reload could have taken place, we have to traverse the list to
		be sure it's still valid */
	ast_mutex_lock(&q->lock);
	cur = q->members;
	while(cur) {
		if (member == cur) {
			time(&cur->lastcall);
			cur->calls++;
			break;
		}
		cur = cur->next;
	}
	q->callscompleted++;
	ast_mutex_unlock(&q->lock);
	return 0;
}

static int calc_metric(struct ast_call_queue *q, struct member *mem, int pos, struct queue_ent *qe, struct callattempt *tmp)
{
	if (mem->penalty > qe->max_penalty)
		return -1;

	switch (q->strategy) {
	case QUEUE_STRATEGY_RINGALL:
		/* Everyone equal, except for penalty */
		tmp->metric = mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_ROUNDROBIN:
		if (!pos) {
			if (!q->wrapped) {
				/* No more channels, start over */
				q->rrpos = 0;
			} else {
				/* Prioritize next entry */
				q->rrpos++;
			}
			q->wrapped = 0;
		}
		/* Fall through */
	case QUEUE_STRATEGY_RRMEMORY:
		if (pos < q->rrpos) {
			tmp->metric = 1000 + pos;
		} else {
			if (pos > q->rrpos)
				/* Indicate there is another priority */
				q->wrapped = 1;
			tmp->metric = pos;
		}
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_RANDOM:
		tmp->metric = ast_random() % 1000;
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_FEWESTCALLS:
		tmp->metric = mem->calls;
		tmp->metric += mem->penalty * 1000000;
		break;
	case QUEUE_STRATEGY_LEASTRECENT:
		if (!mem->lastcall)
			tmp->metric = 0;
		else
			tmp->metric = 1000000 - (time(NULL) - mem->lastcall);
		tmp->metric += mem->penalty * 1000000;
		break;
	default:
		ast_log(LOG_WARNING, "Can't calculate metric for unknown strategy %d\n", q->strategy);
		break;
	}
	return 0;
}

static int try_calling(struct queue_ent *qe, const char *options, char *announceoverride, const char *url, int *go_on)
{
	struct member *cur;
	struct callattempt *outgoing=NULL; /* the queue we are building */
	int to;
	char oldexten[AST_MAX_EXTENSION]="";
	char oldcontext[AST_MAX_CONTEXT]="";
	char queuename[256]="";
	struct ast_channel *peer;
	struct ast_channel *which;
	struct callattempt *lpeer;
	struct member *member;
	int res = 0, bridge = 0;
	int numbusies = 0;
	int x=0;
	char *announce = NULL;
	char digit = 0;
	time_t callstart;
	time_t now = time(NULL);
	struct ast_bridge_config bridge_config;
	char nondataquality = 1;

	memset(&bridge_config, 0, sizeof(bridge_config));
	time(&now);
		
	for (; options && *options; options++)
		switch (*options) {
		case 't':
			ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_REDIRECT);
			break;
		case 'T':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_REDIRECT);
			break;
		case 'w':
			ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_AUTOMON);
			break;
		case 'W':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_AUTOMON);
			break;
		case 'd':
			nondataquality = 0;
			break;
		case 'h':
			ast_set_flag(&(bridge_config.features_callee), AST_FEATURE_DISCONNECT);
			break;
		case 'H':
			ast_set_flag(&(bridge_config.features_caller), AST_FEATURE_DISCONNECT);
			break;
		case 'n':
			if ((now - qe->start >= qe->parent->timeout))
				*go_on = 1;
			break;
		}

	/* Hold the lock while we setup the outgoing calls */
	if (use_weight) 
		AST_LIST_LOCK(&queues);
	ast_mutex_lock(&qe->parent->lock);
	if (option_debug)
		ast_log(LOG_DEBUG, "%s is trying to call a queue member.\n", 
							qe->chan->name);
	ast_copy_string(queuename, qe->parent->name, sizeof(queuename));
	cur = qe->parent->members;
	if (!ast_strlen_zero(qe->announce))
		announce = qe->announce;
	if (!ast_strlen_zero(announceoverride))
		announce = announceoverride;

	for (;cur; cur = cur->next) {
		struct callattempt *tmp = ast_calloc(1, sizeof(*tmp));
		if (!tmp) {
			ast_mutex_unlock(&qe->parent->lock);
			if (use_weight) 
				AST_LIST_UNLOCK(&queues);
			goto out;
		}
		tmp->stillgoing = -1;
		if (option_debug) {
			if (url)
				ast_log(LOG_DEBUG, "Queue with URL=%s_\n", url);
			else 
				ast_log(LOG_DEBUG, "Simple queue (no URL)\n");
		}

		tmp->member = cur;		/* Never directly dereference!  Could change on reload */
		tmp->oldstatus = cur->status;
		tmp->lastcall = cur->lastcall;
		ast_copy_string(tmp->interface, cur->interface, sizeof(tmp->interface));
		/* Special case: If we ring everyone, go ahead and ring them, otherwise
		   just calculate their metric for the appropriate strategy */
		if (!calc_metric(qe->parent, cur, x++, qe, tmp)) {
			/* Put them in the list of outgoing thingies...  We're ready now. 
			   XXX If we're forcibly removed, these outgoing calls won't get
			   hung up XXX */
			tmp->q_next = outgoing;
			outgoing = tmp;		
			/* If this line is up, don't try anybody else */
			if (outgoing->chan && (outgoing->chan->_state == AST_STATE_UP))
				break;
		} else {
			free(tmp);
		}
	}
	to = (qe->parent->timeout) ? qe->parent->timeout * 1000 : -1;
	ring_one(qe, outgoing, &numbusies);
	ast_mutex_unlock(&qe->parent->lock);
	if (use_weight) 
		AST_LIST_UNLOCK(&queues);
	lpeer = wait_for_answer(qe, outgoing, &to, &digit, numbusies, ast_test_flag(&(bridge_config.features_caller), AST_FEATURE_DISCONNECT));
	ast_mutex_lock(&qe->parent->lock);
	if (qe->parent->strategy == QUEUE_STRATEGY_RRMEMORY) {
		store_next(qe, outgoing);
	}
	ast_mutex_unlock(&qe->parent->lock);
	peer = lpeer ? lpeer->chan : NULL;
	if (!peer) {
		if (to) {
			/* Must gotten hung up */
			res = -1;
		} else {
			res = digit;
		}
		if (option_debug)
			ast_log(LOG_DEBUG, "%s: Nobody answered.\n", qe->chan->name);
	} else { /* peer is valid */
		/* Ah ha!  Someone answered within the desired timeframe.  Of course after this
		   we will always return with -1 so that it is hung up properly after the 
		   conversation.  */
		qe->handled++;
		if (!strcmp(qe->chan->tech->type, "Zap"))
			ast_channel_setoption(qe->chan, AST_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality), 0);
		if (!strcmp(peer->tech->type, "Zap"))
			ast_channel_setoption(peer, AST_OPTION_TONE_VERIFY, &nondataquality, sizeof(nondataquality), 0);
		/* Update parameters for the queue */
		recalc_holdtime(qe);
		member = lpeer->member;
		hangupcalls(outgoing, peer);
		outgoing = NULL;
		if (announce || qe->parent->reportholdtime || qe->parent->memberdelay) {
			int res2;
			res2 = ast_autoservice_start(qe->chan);
			if (!res2) {
				if (qe->parent->memberdelay) {
					ast_log(LOG_NOTICE, "Delaying member connect for %d seconds\n", qe->parent->memberdelay);
					res2 |= ast_safe_sleep(peer, qe->parent->memberdelay * 1000);
				}
				if (!res2 && announce) {
					if (play_file(peer, announce))
						ast_log(LOG_WARNING, "Announcement file '%s' is unavailable, continuing anyway...\n", announce);
				}
				if (!res2 && qe->parent->reportholdtime) {
					if (!play_file(peer, qe->parent->sound_reporthold)) {
						int holdtime;

						time(&now);
						holdtime = abs((now - qe->start) / 60);
						if (holdtime < 2) {
							play_file(peer, qe->parent->sound_lessthan);
							ast_say_number(peer, 2, AST_DIGIT_ANY, peer->language, NULL);
						} else 
							ast_say_number(peer, holdtime, AST_DIGIT_ANY, peer->language, NULL);
						play_file(peer, qe->parent->sound_minutes);
					}
				}
			}
			res2 |= ast_autoservice_stop(qe->chan);
			if (peer->_softhangup) {
				/* Agent must have hung up */
				ast_log(LOG_WARNING, "Agent on %s hungup on the customer.  They're going to be pissed.\n", peer->name);
				ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "AGENTDUMP", "%s", "");
				record_abandoned(qe);
				if (qe->parent->eventwhencalled) {
					manager_event(EVENT_FLAG_AGENT, "AgentDump",
						      "Queue: %s\r\n"
						      "Uniqueid: %s\r\n"
						      "Channel: %s\r\n"
						      "Member: %s\r\n",
						      queuename, qe->chan->uniqueid, peer->name, member->interface);
				}
				ast_hangup(peer);
				goto out;
			} else if (res2) {
				/* Caller must have hung up just before being connected*/
				ast_log(LOG_NOTICE, "Caller was about to talk to agent on %s but the caller hungup.\n", peer->name);
				ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "ABANDON", "%d|%d|%ld", qe->pos, qe->opos, (long)time(NULL) - qe->start);
				record_abandoned(qe);
				ast_hangup(peer);
				return -1;
			}
		}
		/* Stop music on hold */
		ast_moh_stop(qe->chan);
		/* If appropriate, log that we have a destination channel */
		if (qe->chan->cdr)
			ast_cdr_setdestchan(qe->chan->cdr, peer->name);
		/* Make sure channels are compatible */
		res = ast_channel_make_compatible(qe->chan, peer);
		if (res < 0) {
			ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "SYSCOMPAT", "%s", "");
			ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", qe->chan->name, peer->name);
		record_abandoned(qe);
			ast_hangup(peer);
			return -1;
		}
		/* Begin Monitoring */
		if (qe->parent->monfmt && *qe->parent->monfmt) {
			const char *monitorfilename = pbx_builtin_getvar_helper(qe->chan, "MONITOR_FILENAME");
			if (pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC") || pbx_builtin_getvar_helper(qe->chan, "MONITOR_EXEC_ARGS"))
				which = qe->chan;
			else
				which = peer;
			if (monitorfilename)
				ast_monitor_start(which, qe->parent->monfmt, monitorfilename, 1 );
			else if (qe->chan->cdr) 
				ast_monitor_start(which, qe->parent->monfmt, qe->chan->cdr->uniqueid, 1 );
			else {
				/* Last ditch effort -- no CDR, make up something */
				char tmpid[256];
				snprintf(tmpid, sizeof(tmpid), "chan-%lx", ast_random());
				ast_monitor_start(which, qe->parent->monfmt, tmpid, 1 );
			}
			if (qe->parent->monjoin)
				ast_monitor_setjoinfiles(which, 1);
		}
		/* Drop out of the queue at this point, to prepare for next caller */
		leave_queue(qe);			
 		if (!ast_strlen_zero(url) && ast_channel_supports_html(peer)) {
			if (option_debug)
	 			ast_log(LOG_DEBUG, "app_queue: sendurl=%s.\n", url);
 			ast_channel_sendurl(peer, url);
 		}
		ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "CONNECT", "%ld|%s", (long)time(NULL) - qe->start, peer->uniqueid);
		if (qe->parent->eventwhencalled)
			manager_event(EVENT_FLAG_AGENT, "AgentConnect",
				      "Queue: %s\r\n"
				      "Uniqueid: %s\r\n"
				      "Channel: %s\r\n"
				      "Member: %s\r\n"
				      "Holdtime: %ld\r\n"
				      "BridgedChannel: %s\r\n",
				      queuename, qe->chan->uniqueid, peer->name, member->interface,
				      (long)time(NULL) - qe->start,peer->uniqueid);
		ast_copy_string(oldcontext, qe->chan->context, sizeof(oldcontext));
		ast_copy_string(oldexten, qe->chan->exten, sizeof(oldexten));
		time(&callstart);

		bridge = ast_bridge_call(qe->chan,peer, &bridge_config);

		if (strcasecmp(oldcontext, qe->chan->context) || strcasecmp(oldexten, qe->chan->exten)) {
			ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "TRANSFER", "%s|%s", qe->chan->exten, qe->chan->context);
		} else if (qe->chan->_softhangup) {
			ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "COMPLETECALLER", "%ld|%ld",
				      (long)(callstart - qe->start), (long)(time(NULL) - callstart));
			if (qe->parent->eventwhencalled)
				manager_event(EVENT_FLAG_AGENT, "AgentComplete",
					      "Queue: %s\r\n"
					      "Uniqueid: %s\r\n"
					      "Channel: %s\r\n"
					      "Member: %s\r\n"
					      "HoldTime: %ld\r\n"
					      "TalkTime: %ld\r\n"
					      "Reason: caller\r\n",
					      queuename, qe->chan->uniqueid, peer->name, member->interface,
					      (long)(callstart - qe->start), (long)(time(NULL) - callstart));
		} else {
			ast_queue_log(queuename, qe->chan->uniqueid, peer->name, "COMPLETEAGENT", "%ld|%ld", (long)(callstart - qe->start), (long)(time(NULL) - callstart));
			if (qe->parent->eventwhencalled)
				manager_event(EVENT_FLAG_AGENT, "AgentComplete",
					      "Queue: %s\r\n"
					      "Uniqueid: %s\r\n"
					      "Channel: %s\r\n"
					      "HoldTime: %ld\r\n"
					      "TalkTime: %ld\r\n"
					      "Reason: agent\r\n",
					      queuename, qe->chan->uniqueid, peer->name, (long)(callstart - qe->start),
					      (long)(time(NULL) - callstart));
		}

		if(bridge != AST_PBX_NO_HANGUP_PEER)
			ast_hangup(peer);
		update_queue(qe->parent, member);
		if (bridge == 0) 
			res = 1; /* JDG: bridge successfully, leave app_queue */
		else 
			res = bridge; /* bridge error, stay in the queue */
	}
out:
	hangupcalls(outgoing, NULL);
	return res;
}

static int wait_a_bit(struct queue_ent *qe)
{
	/* Don't need to hold the lock while we setup the outgoing calls */
	int retrywait = qe->parent->retry * 1000;

	return ast_waitfordigit(qe->chan, retrywait);
}

static struct member * interface_exists(struct ast_call_queue *q, char *interface)
{
	struct member *mem;

	if (q)
		for (mem = q->members; mem; mem = mem->next)
			if (!strcasecmp(interface, mem->interface))
				return mem;

	return NULL;
}


/* Dump all members in a specific queue to the database
 *
 * <pm_family>/<queuename> = <interface>;<penalty>;<paused>[|...]
 *
 */
static void dump_queue_members(struct ast_call_queue *pm_queue)
{
	struct member *cur_member;
	char value[PM_MAX_LEN];
	int value_len = 0;
	int res;

	memset(value, 0, sizeof(value));

	if (!pm_queue)
		return;

	for (cur_member = pm_queue->members; cur_member; cur_member = cur_member->next) {
		if (!cur_member->dynamic)
			continue;

		res = snprintf(value + value_len, sizeof(value) - value_len, "%s;%d;%d%s",
			       cur_member->interface, cur_member->penalty, cur_member->paused,
			       cur_member->next ? "|" : "");
		if (res != strlen(value + value_len)) {
			ast_log(LOG_WARNING, "Could not create persistent member string, out of space\n");
			break;
		}
		value_len += res;
	}
	
	if (value_len && !cur_member) {
		if (ast_db_put(pm_family, pm_queue->name, value))
			ast_log(LOG_WARNING, "failed to create persistent dynamic entry!\n");
	} else
		/* Delete the entry if the queue is empty or there is an error */
		ast_db_del(pm_family, pm_queue->name);
}

static int remove_from_queue(char *queuename, char *interface)
{
	struct ast_call_queue *q;
	struct member *last_member, *look;
	int res = RES_NOSUCHQUEUE;

	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		ast_mutex_lock(&q->lock);
		if (!strcmp(q->name, queuename)) {
			if ((last_member = interface_exists(q, interface))) {
				if ((look = q->members) == last_member) {
					q->members = last_member->next;
				} else {
					while (look != NULL) {
						if (look->next == last_member) {
							look->next = last_member->next;
							break;
						} else {
							 look = look->next;
						}
					}
				}
				manager_event(EVENT_FLAG_AGENT, "QueueMemberRemoved",
						"Queue: %s\r\n"
						"Location: %s\r\n",
					q->name, last_member->interface);
				free(last_member);

				if (queue_persistent_members)
					dump_queue_members(q);

				res = RES_OKAY;
			} else {
				res = RES_EXISTS;
			}
			ast_mutex_unlock(&q->lock);
			break;
		}
		ast_mutex_unlock(&q->lock);
	}
	AST_LIST_UNLOCK(&queues);
	return res;
}

static int add_to_queue(char *queuename, char *interface, int penalty, int paused, int dump)
{
	struct ast_call_queue *q;
	struct member *new_member;
	int res = RES_NOSUCHQUEUE;

	/* \note Ensure the appropriate realtime queue is loaded.  Note that this
	 * short-circuits if the queue is already in memory. */
	q = load_realtime_queue(queuename);

	AST_LIST_LOCK(&queues);

	if (q) {
		ast_mutex_lock(&q->lock);
		if (interface_exists(q, interface) == NULL) {
			new_member = create_queue_member(interface, penalty, paused);

			if (new_member != NULL) {
				new_member->dynamic = 1;
				new_member->next = q->members;
				q->members = new_member;
				manager_event(EVENT_FLAG_AGENT, "QueueMemberAdded",
						"Queue: %s\r\n"
						"Location: %s\r\n"
						"Membership: %s\r\n"
						"Penalty: %d\r\n"
						"CallsTaken: %d\r\n"
						"LastCall: %d\r\n"
						"Status: %d\r\n"
						"Paused: %d\r\n",
						q->name, new_member->interface, new_member->dynamic ? "dynamic" : "static",
						new_member->penalty, new_member->calls, (int)new_member->lastcall, new_member->status, new_member->paused);

				if (dump)
					dump_queue_members(q);

				res = RES_OKAY;
			} else {
				res = RES_OUTOFMEMORY;
			}
		} else {
			res = RES_EXISTS;
		}
		ast_mutex_unlock(&q->lock);
	}
	AST_LIST_UNLOCK(&queues);
	return res;
}

static int set_member_paused(char *queuename, char *interface, int paused)
{
	int found = 0;
	struct ast_call_queue *q;
	struct member *mem;

	/* Special event for when all queues are paused - individual events still generated */

	if (ast_strlen_zero(queuename))
		ast_queue_log("NONE", "NONE", interface, (paused ? "PAUSEALL" : "UNPAUSEALL"), "%s", "");

	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		ast_mutex_lock(&q->lock);
		if (ast_strlen_zero(queuename) || !strcasecmp(q->name, queuename)) {
			if ((mem = interface_exists(q, interface))) {
				found++;
				if (mem->paused == paused)
					ast_log(LOG_DEBUG, "%spausing already-%spaused queue member %s:%s\n", (paused ? "" : "un"), (paused ? "" : "un"), q->name, interface);
				mem->paused = paused;

				if (queue_persistent_members)
					dump_queue_members(q);

				ast_queue_log(q->name, "NONE", interface, (paused ? "PAUSE" : "UNPAUSE"), "%s", "");

				manager_event(EVENT_FLAG_AGENT, "QueueMemberPaused",
					"Queue: %s\r\n"
					"Location: %s\r\n"
					"Paused: %d\r\n",
						q->name, mem->interface, paused);
			}
		}
		ast_mutex_unlock(&q->lock);
	}
	AST_LIST_UNLOCK(&queues);

	if (found)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

/* Reload dynamic queue members persisted into the astdb */
static void reload_queue_members(void)
{
	char *cur_ptr;	
	char *queue_name;
	char *member;
	char *interface;
	char *penalty_tok;
	int penalty = 0;
	char *paused_tok;
	int paused = 0;
	struct ast_db_entry *db_tree;
	struct ast_db_entry *entry;
	struct ast_call_queue *cur_queue;
	char queue_data[PM_MAX_LEN];

	AST_LIST_LOCK(&queues);

	/* Each key in 'pm_family' is the name of a queue */
	db_tree = ast_db_gettree(pm_family, NULL);
	for (entry = db_tree; entry; entry = entry->next) {

		queue_name = entry->key + strlen(pm_family) + 2;

		AST_LIST_TRAVERSE(&queues, cur_queue, list) {
			ast_mutex_lock(&cur_queue->lock);
			if (!strcmp(queue_name, cur_queue->name))
				break;
			ast_mutex_unlock(&cur_queue->lock);
		}

		if (!cur_queue) {
			/* If the queue no longer exists, remove it from the
			 * database */
			ast_db_del(pm_family, queue_name);
			continue;
		} else
			ast_mutex_unlock(&cur_queue->lock);

		if (ast_db_get(pm_family, queue_name, queue_data, PM_MAX_LEN))
			continue;

		cur_ptr = queue_data;
		while ((member = strsep(&cur_ptr, "|"))) {
			if (ast_strlen_zero(member))
				continue;

			interface = strsep(&member, ";");
			penalty_tok = strsep(&member, ";");
			paused_tok = strsep(&member, ";");

			if (!penalty_tok) {
				ast_log(LOG_WARNING, "Error parsing persistent member string for '%s' (penalty)\n", queue_name);
				break;
			}
			penalty = strtol(penalty_tok, NULL, 10);
			if (errno == ERANGE) {
				ast_log(LOG_WARNING, "Error converting penalty: %s: Out of range.\n", penalty_tok);
				break;
			}
			
			if (!paused_tok) {
				ast_log(LOG_WARNING, "Error parsing persistent member string for '%s' (paused)\n", queue_name);
				break;
			}
			paused = strtol(paused_tok, NULL, 10);
			if ((errno == ERANGE) || paused < 0 || paused > 1) {
				ast_log(LOG_WARNING, "Error converting paused: %s: Expected 0 or 1.\n", paused_tok);
				break;
			}

			if (option_debug)
				ast_log(LOG_DEBUG, "Reload Members: Queue: %s  Member: %s  Penalty: %d  Paused: %d\n", queue_name, interface, penalty, paused);
			
			if (add_to_queue(queue_name, interface, penalty, paused, 0) == RES_OUTOFMEMORY) {
				ast_log(LOG_ERROR, "Out of Memory when reloading persistent queue member\n");
				break;
			}
		}
	}

	AST_LIST_UNLOCK(&queues);
	if (db_tree) {
		ast_log(LOG_NOTICE, "Queue members successfully reloaded from database.\n");
		ast_db_freetree(db_tree);
	}
}

static int pqm_exec(struct ast_channel *chan, void *data)
{
	struct localuser *lu;
	char *parse;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "PauseQueueMember requires an argument ([queuename]|interface[|options])\n");
		return -1;
	}

	if (!(parse = ast_strdupa(data)))
		return -1;

	AST_STANDARD_APP_ARGS(args, parse);

	LOCAL_USER_ADD(lu);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	if (ast_strlen_zero(args.interface)) {
		ast_log(LOG_WARNING, "Missing interface argument to PauseQueueMember ([queuename]|interface[|options])\n");
		LOCAL_USER_REMOVE(lu);
		return -1;
	}

	if (set_member_paused(args.queuename, args.interface, 1)) {
		ast_log(LOG_WARNING, "Attempt to pause interface %s, not found\n", args.interface);
		if (priority_jump || ast_opt_priority_jumping) {
			if (ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) {
				pbx_builtin_setvar_helper(chan, "PQMSTATUS", "NOTFOUND");
				LOCAL_USER_REMOVE(lu);
				return 0;
			}
		}
		LOCAL_USER_REMOVE(lu);
		pbx_builtin_setvar_helper(chan, "PQMSTATUS", "NOTFOUND");
		return -1;
	}

	LOCAL_USER_REMOVE(lu);
	pbx_builtin_setvar_helper(chan, "PQMSTATUS", "PAUSED");
	return 0;
}

static int upqm_exec(struct ast_channel *chan, void *data)
{
	struct localuser *lu;
	char *parse;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "UnpauseQueueMember requires an argument ([queuename]|interface[|options])\n");
		return -1;
	}

	if (!(parse = ast_strdupa(data))) 
		return -1;

	AST_STANDARD_APP_ARGS(args, parse);

	LOCAL_USER_ADD(lu);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	if (ast_strlen_zero(args.interface)) {
		ast_log(LOG_WARNING, "Missing interface argument to PauseQueueMember ([queuename]|interface[|options])\n");
		LOCAL_USER_REMOVE(lu);
		return -1;
	}

	if (set_member_paused(args.queuename, args.interface, 0)) {
		ast_log(LOG_WARNING, "Attempt to unpause interface %s, not found\n", args.interface);
		if (priority_jump || ast_opt_priority_jumping) {
			if (ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) {
				pbx_builtin_setvar_helper(chan, "UPQMSTATUS", "NOTFOUND");
				LOCAL_USER_REMOVE(lu);
				return 0;
			}
		}
		LOCAL_USER_REMOVE(lu);
		pbx_builtin_setvar_helper(chan, "UPQMSTATUS", "NOTFOUND");
		return -1;
	}

	LOCAL_USER_REMOVE(lu);
	pbx_builtin_setvar_helper(chan, "UPQMSTATUS", "UNPAUSED");
	return 0;
}

static int rqm_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *lu;
	char *parse, *temppos = NULL;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(options);
	);


	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "RemoveQueueMember requires an argument (queuename[|interface[|options]])\n");
		return -1;
	}

	if (!(parse = ast_strdupa(data)))
		return -1;

	AST_STANDARD_APP_ARGS(args, parse);

	LOCAL_USER_ADD(lu);

	if (ast_strlen_zero(args.interface)) {
		args.interface = ast_strdupa(chan->name);
		temppos = strrchr(args.interface, '-');
		if (temppos)
			*temppos = '\0';
	}

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	switch (remove_from_queue(args.queuename, args.interface)) {
	case RES_OKAY:
		ast_log(LOG_NOTICE, "Removed interface '%s' from queue '%s'\n", args.interface, args.queuename);
		pbx_builtin_setvar_helper(chan, "RQMSTATUS", "REMOVED");
		res = 0;
		break;
	case RES_EXISTS:
		ast_log(LOG_WARNING, "Unable to remove interface '%s' from queue '%s': Not there\n", args.interface, args.queuename);
		if (priority_jump || ast_opt_priority_jumping) 
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "RQMSTATUS", "NOTINQUEUE");
		res = 0;
		break;
	case RES_NOSUCHQUEUE:
		ast_log(LOG_WARNING, "Unable to remove interface from queue '%s': No such queue\n", args.queuename);
		pbx_builtin_setvar_helper(chan, "RQMSTATUS", "NOSUCHQUEUE");
		res = 0;
		break;
	case RES_OUTOFMEMORY:
		ast_log(LOG_ERROR, "Out of memory\n");
		break;
	}

	LOCAL_USER_REMOVE(lu);
	return res;
}

static int aqm_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *lu;
	char *parse, *temppos = NULL;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(interface);
		AST_APP_ARG(penalty);
		AST_APP_ARG(options);
	);
	int penalty = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "AddQueueMember requires an argument (queuename[|[interface]|[penalty][|options]])\n");
		return -1;
	}

	if (!(parse = ast_strdupa(data)))
		return -1;

	AST_STANDARD_APP_ARGS(args, parse);

	LOCAL_USER_ADD(lu);

	if (ast_strlen_zero(args.interface)) {
		args.interface = ast_strdupa(chan->name);
		temppos = strrchr(args.interface, '-');
		if (temppos)
			*temppos = '\0';
	}

	if (!ast_strlen_zero(args.penalty)) {
		if ((sscanf(args.penalty, "%d", &penalty) != 1) || penalty < 0) {
			ast_log(LOG_WARNING, "Penalty '%s' is invalid, must be an integer >= 0\n", args.penalty);
			penalty = 0;
		}
	}
	
	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}


	switch (add_to_queue(args.queuename, args.interface, penalty, 0, queue_persistent_members)) {
	case RES_OKAY:
		ast_log(LOG_NOTICE, "Added interface '%s' to queue '%s'\n", args.interface, args.queuename);
		pbx_builtin_setvar_helper(chan, "AQMSTATUS", "ADDED");
		res = 0;
		break;
	case RES_EXISTS:
		ast_log(LOG_WARNING, "Unable to add interface '%s' to queue '%s': Already there\n", args.interface, args.queuename);
		if (priority_jump || ast_opt_priority_jumping) 
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "AQMSTATUS", "MEMBERALREADY");
		res = 0;
		break;
	case RES_NOSUCHQUEUE:
		ast_log(LOG_WARNING, "Unable to add interface to queue '%s': No such queue\n", args.queuename);
		pbx_builtin_setvar_helper(chan, "AQMSTATUS", "NOSUCHQUEUE");
		res = 0;
		break;
	case RES_OUTOFMEMORY:
		ast_log(LOG_ERROR, "Out of memory adding member %s to queue %s\n", args.interface, args.queuename);
		break;
	}

	LOCAL_USER_REMOVE(lu);
	return res;
}

static int queue_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	int ringing=0;
	struct localuser *lu;
	const char *user_priority;
	const char *max_penalty_str;
	int prio;
	int max_penalty;
	enum queue_result reason = QUEUE_UNKNOWN;

	/* whether to exit Queue application after the timeout hits */
	int go_on = 0;

	char *parse;
	AST_DECLARE_APP_ARGS(args,
		 AST_APP_ARG(queuename);
		 AST_APP_ARG(options);
		 AST_APP_ARG(url);
		 AST_APP_ARG(announceoverride);
		 AST_APP_ARG(queuetimeoutstr);
	);
	
	/* Our queue entry */
	struct queue_ent qe;
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Queue requires an argument: queuename[|options[|URL][|announceoverride][|timeout]]\n");
		return -1;
	}
	
	parse = ast_strdupa(data);
	if (!parse) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		return -1;
	}
	AST_STANDARD_APP_ARGS(args, parse);

	LOCAL_USER_ADD(lu);

	/* Setup our queue entry */
	memset(&qe, 0, sizeof(qe));
	qe.start = time(NULL);

	/* set the expire time based on the supplied timeout; */
	if (args.queuetimeoutstr)
		qe.expire = qe.start + atoi(args.queuetimeoutstr);
	else
		qe.expire = 0;

	/* Get the priority from the variable ${QUEUE_PRIO} */
	user_priority = pbx_builtin_getvar_helper(chan, "QUEUE_PRIO");
	if (user_priority) {
		if (sscanf(user_priority, "%d", &prio) == 1) {
			if (option_debug)
				ast_log(LOG_DEBUG, "%s: Got priority %d from ${QUEUE_PRIO}.\n",
					chan->name, prio);
		} else {
			ast_log(LOG_WARNING, "${QUEUE_PRIO}: Invalid value (%s), channel %s.\n",
				user_priority, chan->name);
			prio = 0;
		}
	} else {
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "NO QUEUE_PRIO variable found. Using default.\n");
		prio = 0;
	}

	/* Get the maximum penalty from the variable ${QUEUE_MAX_PENALTY} */
	if ((max_penalty_str = pbx_builtin_getvar_helper(chan, "QUEUE_MAX_PENALTY"))) {
		if (sscanf(max_penalty_str, "%d", &max_penalty) == 1) {
			if (option_debug)
				ast_log(LOG_DEBUG, "%s: Got max penalty %d from ${QUEUE_MAX_PENALTY}.\n",
					chan->name, max_penalty);
		} else {
			ast_log(LOG_WARNING, "${QUEUE_MAX_PENALTY}: Invalid value (%s), channel %s.\n",
				max_penalty_str, chan->name);
			max_penalty = 0;
		}
	} else {
		max_penalty = 0;
	}

	if (args.options && (strchr(args.options, 'r')))
		ringing = 1;

	if (option_debug)  
		ast_log(LOG_DEBUG, "queue: %s, options: %s, url: %s, announce: %s, expires: %ld, priority: %d\n",
			args.queuename, args.options, args.url, args.announceoverride, (long)qe.expire, prio);

	qe.chan = chan;
	qe.prio = prio;
	qe.max_penalty = max_penalty;
	qe.last_pos_said = 0;
	qe.last_pos = 0;
	qe.last_periodic_announce_time = time(NULL);
	qe.last_periodic_announce_sound = 0;
	if (!join_queue(args.queuename, &qe, &reason)) {
		ast_queue_log(args.queuename, chan->uniqueid, "NONE", "ENTERQUEUE", "%s|%s", S_OR(args.url, ""),
			      S_OR(chan->cid.cid_num, ""));
check_turns:
		if (ringing) {
			ast_indicate(chan, AST_CONTROL_RINGING);
		} else {
			ast_moh_start(chan, qe.moh);
		}
		for (;;) {
			/* This is the wait loop for callers 2 through maxlen */

			res = wait_our_turn(&qe, ringing, &reason);
			/* If they hungup, return immediately */
			if (res < 0) {
				/* Record this abandoned call */
				record_abandoned(&qe);
				 ast_queue_log(args.queuename, chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
				if (option_verbose > 2) {
					ast_verbose(VERBOSE_PREFIX_3 "User disconnected from queue %s while waiting their turn\n", args.queuename);
				}
				res = -1;
				break;
			}
			if (!res) 
				break;
			if (valid_exit(&qe, res)) {
				ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
				break;
			}
		}
		if (!res) {
			int makeannouncement = 0;
			for (;;) {
				/* This is the wait loop for the head caller*/
				/* To exit, they may get their call answered; */
				/* they may dial a digit from the queue context; */
				/* or, they may timeout. */

				enum queue_member_status stat;

				/* Leave if we have exceeded our queuetimeout */
				if (qe.expire && (time(NULL) > qe.expire)) {
					record_abandoned(&qe);
					reason = QUEUE_TIMEOUT;
					res = 0;
					ast_queue_log(args.queuename, chan->uniqueid,"NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
					break;
				}

				if (makeannouncement) {
					/* Make a position announcement, if enabled */
					if (qe.parent->announcefrequency && !ringing)
						res = say_position(&qe);
					if (res && valid_exit(&qe, res)) {
						 ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
						break;
					}

				}
				makeannouncement = 1;

				/* Make a periodic announcement, if enabled */
				if (qe.parent->periodicannouncefrequency && !ringing)
					res = say_periodic_announcement(&qe);

				if (res && valid_exit(&qe, res)) {
					ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%c|%d", res, qe.pos);
					break;
				}

				/* Try calling all queue members for 'timeout' seconds */
				res = try_calling(&qe, args.options, args.announceoverride, args.url, &go_on);

				if (res) {
					if (res < 0) {
						if (!qe.handled) {
							record_abandoned(&qe);
							ast_queue_log(args.queuename, chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
						}
					} else if (res > 0)
						 ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
					break;
				}

				stat = get_member_status(qe.parent, qe.max_penalty);

				/* leave the queue if no agents, if enabled */
				if (qe.parent->leavewhenempty && (stat == QUEUE_NO_MEMBERS)) {
					record_abandoned(&qe);
					reason = QUEUE_LEAVEEMPTY;
					res = 0;
					break;
				}

				/* leave the queue if no reachable agents, if enabled */
				if ((qe.parent->leavewhenempty == QUEUE_EMPTY_STRICT) && (stat == QUEUE_NO_REACHABLE_MEMBERS)) {
					record_abandoned(&qe);
					reason = QUEUE_LEAVEUNAVAIL;
					res = 0;
					break;
				}

				/* Leave if we have exceeded our queuetimeout */
				if (qe.expire && (time(NULL) > qe.expire)) {
					record_abandoned(&qe);
					reason = QUEUE_TIMEOUT;
					res = 0;
					ast_queue_log(args.queuename, chan->uniqueid,"NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
					break;
				}

				/* OK, we didn't get anybody; wait for 'retry' seconds; may get a digit to exit with */
				res = wait_a_bit(&qe);
				if (res < 0) {
					record_abandoned(&qe);
					ast_queue_log(args.queuename, chan->uniqueid, "NONE", "ABANDON", "%d|%d|%ld", qe.pos, qe.opos, (long)time(NULL) - qe.start);
					if (option_verbose > 2) {
						ast_verbose(VERBOSE_PREFIX_3 "User disconnected from queue %s when they almost made it\n", args.queuename);
					}
					res = -1;
					break;
				}
				if (res && valid_exit(&qe, res)) {
					ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHKEY", "%s|%d", qe.digits, qe.pos);
					break;
				}
				/* exit after 'timeout' cycle if 'n' option enabled */
				if (go_on) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Exiting on time-out cycle\n");
					ast_queue_log(args.queuename, chan->uniqueid, "NONE", "EXITWITHTIMEOUT", "%d", qe.pos);
					record_abandoned(&qe);
					reason = QUEUE_TIMEOUT;
					res = 0;
					break;
				}
				/* Since this is a priority queue and 
				 * it is not sure that we are still at the head
				 * of the queue, go and check for our turn again.
				 */
				if (!is_our_turn(&qe)) {
					if (option_debug)
						ast_log(LOG_DEBUG, "Darn priorities, going back in queue (%s)!\n",
								qe.chan->name);
					goto check_turns;
				}
			}
		}
		/* Don't allow return code > 0 */
		if (res >= 0 && res != AST_PBX_KEEPALIVE) {
			res = 0;	
			if (ringing) {
				ast_indicate(chan, -1);
			} else {
				ast_moh_stop(chan);
			}			
			ast_stopstream(chan);
		}
		leave_queue(&qe);
		if (reason != QUEUE_UNKNOWN)
			set_queue_result(chan, reason);
	} else {
		ast_log(LOG_WARNING, "Unable to join queue '%s'\n", args.queuename);
		set_queue_result(chan, reason);
		res = 0;
	}
	LOCAL_USER_REMOVE(lu);
	return res;
}

static int queue_function_qac(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	int count = 0;
	struct ast_call_queue *q;
	struct localuser *lu;
	struct member *m;

	buf[0] = '\0';
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: queuename\n", cmd);
		return -1;
	}

	LOCAL_USER_ADD(lu);
	
	AST_LIST_LOCK(&queues);

	/* Find the right queue */
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!strcasecmp(q->name, data)) {
			ast_mutex_lock(&q->lock);
			break;
		}
	}

	AST_LIST_UNLOCK(&queues);

	if (q) {
		for (m = q->members; m; m = m->next) {
			/* Count the agents who are logged in and presently answering calls */
			if ((m->status != AST_DEVICE_UNAVAILABLE) && (m->status != AST_DEVICE_INVALID)) {
				count++;
			}
		}
		ast_mutex_unlock(&q->lock);
	} else
		ast_log(LOG_WARNING, "queue %s was not found\n", data);

	snprintf(buf, len, "%d", count);
	LOCAL_USER_REMOVE(lu);
	return 0;
}

static int queue_function_queuewaitingcount(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	int count = 0;
	struct ast_call_queue *q;
	struct localuser *lu;

	buf[0] = '\0';
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s requires an argument: queuename\n", cmd);
		return -1;
	}

	LOCAL_USER_ADD(lu);
	
	AST_LIST_LOCK(&queues);

	/* Find the right queue */
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!strcasecmp(q->name, data)) {
			ast_mutex_lock(&q->lock);
			break;
		}
	}

	AST_LIST_UNLOCK(&queues);

	if (q) {
		count = q->count;
		ast_mutex_unlock(&q->lock);
	} else
		ast_log(LOG_WARNING, "queue %s was not found\n", data);

	snprintf(buf, len, "%d", count);
	LOCAL_USER_REMOVE(lu);
	return 0;
}

static int queue_function_queuememberlist(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	struct localuser *u;
	struct ast_call_queue *q;
	struct member *m;

	/* Ensure an otherwise empty list doesn't return garbage */
	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "QUEUE_MEMBER_LIST requires an argument: queuename\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);

	AST_LIST_LOCK(&queues);

	/* Find the right queue */
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!strcasecmp(q->name, data)) {
			ast_mutex_lock(&q->lock);
			break;
		}
	}

	AST_LIST_UNLOCK(&queues);

	if (q) {
		int buflen = 0, count = 0;
		for (m = q->members; m; m = m->next) {
			/* strcat() is always faster than printf() */
			if (count++) {
				strncat(buf + buflen, ",", len - buflen - 1);
				buflen++;
			}
			strncat(buf + buflen, m->interface, len - buflen - 1);
			buflen += strlen(m->interface);
			/* Safeguard against overflow (negative length) */
			if (buflen >= len - 2) {
				ast_log(LOG_WARNING, "Truncating list\n");
				break;
			}
		}
		ast_mutex_unlock(&q->lock);
	} else
		ast_log(LOG_WARNING, "queue %s was not found\n", data);

	/* We should already be terminated, but let's make sure. */
	buf[len - 1] = '\0';
	LOCAL_USER_REMOVE(u);
	return 0;
}

static struct ast_custom_function queueagentcount_function = {
	.name = "QUEUEAGENTCOUNT",
	.synopsis = "Count number of agents answering a queue",
	.syntax = "QUEUEAGENTCOUNT(<queuename>)",
	.desc =
"Returns the number of members currently associated with the specified queue.\n"
"This function is deprecated.  You should use QUEUE_MEMBER_COUNT() instead.\n",
	.read = queue_function_qac,
};

static struct ast_custom_function queuemembercount_function = {
	.name = "QUEUE_MEMBER_COUNT",
	.synopsis = "Count number of members answering a queue",
	.syntax = "QUEUE_MEMBER_COUNT(<queuename>)",
	.desc =
"Returns the number of members currently associated with the specified queue.\n",
	.read = queue_function_qac,
};

static struct ast_custom_function queuewaitingcount_function = {
	.name = "QUEUE_WAITING_COUNT",
	.synopsis = "Count number of calls currently waiting in a queue",
	.syntax = "QUEUE_WAITING_COUNT(<queuename>)",
	.desc = 
"Returns the number of callers currently waiting in the specified queue.\n",
	.read = queue_function_queuewaitingcount,
};

static struct ast_custom_function queuememberlist_function = {
	.name = "QUEUE_MEMBER_LIST",
	.synopsis = "Returns a list of interfaces on a queue",
	.syntax = "QUEUE_MEMBER_LIST(<queuename>)",
	.desc =
"Returns a comma-separated list of members associated with the specified queue.\n",
	.read = queue_function_queuememberlist,
};

static void reload_queues(void)
{
	struct ast_call_queue *q;
	struct ast_config *cfg;
	char *cat, *tmp;
	struct ast_variable *var;
	struct member *prev, *cur;
	int new;
	char *general_val = NULL;
	char interface[80];
	int penalty;
	
	cfg = ast_config_load("queues.conf");
	if (!cfg) {
		ast_log(LOG_NOTICE, "No call queueing config file (queues.conf), so no call queues\n");
		return;
	}
	memset(interface, 0, sizeof(interface));
	AST_LIST_LOCK(&queues);
	use_weight=0;
	/* Mark all queues as dead for the moment */
	AST_LIST_TRAVERSE(&queues, q, list) {
		q->dead = 1;
	}
	/* Chug through config file */
	cat = NULL;
	while ((cat = ast_category_browse(cfg, cat)) ) {
		if (!strcasecmp(cat, "general")) {	
			/* Initialize global settings */
			queue_persistent_members = 0;
			if ((general_val = ast_variable_retrieve(cfg, "general", "persistentmembers")))
				queue_persistent_members = ast_true(general_val);
			autofill_default = 0;
			if ((general_val = ast_variable_retrieve(cfg, "general", "autofill")))
				autofill_default = ast_true(general_val);
		} else {	/* Define queue */
			/* Look for an existing one */
			AST_LIST_TRAVERSE(&queues, q, list) {
				if (!strcmp(q->name, cat))
					break;
			}
			if (!q) {
				/* Make one then */
				if (!(q = alloc_queue(cat))) {
					/* TODO: Handle memory allocation failure */
				}
				new = 1;
			} else
				new = 0;
			if (q) {
				if (!new)
					ast_mutex_lock(&q->lock);
				/* Re-initialize the queue, and clear statistics */
				init_queue(q);
				clear_queue(q);
				free_members(q, 0);
				prev = q->members;
				if (prev) {
					/* find the end of any dynamic members */
					while(prev->next)
						prev = prev->next;
				}
				for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
					if (!strcasecmp(var->name, "member")) {
						/* Add a new member */
						ast_copy_string(interface, var->value, sizeof(interface));
						if ((tmp = strchr(interface, ','))) {
							*tmp = '\0';
							tmp++;
							penalty = atoi(tmp);
							if (penalty < 0) {
								penalty = 0;
							}
						} else
							penalty = 0;
						cur = create_queue_member(interface, penalty, 0);
						if (cur) {
							if (prev)
								prev->next = cur;
							else
								q->members = cur;
							prev = cur;
						}
					} else {
						queue_set_param(q, var->name, var->value, var->lineno, 1);
					}
				}
				if (new) {
					AST_LIST_INSERT_HEAD(&queues, q, list);
				} else
					ast_mutex_unlock(&q->lock);
			}
		}
	}
	ast_config_destroy(cfg);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&queues, q, list) {
		if (q->dead) {
			AST_LIST_REMOVE_CURRENT(&queues, list);
			if (!q->count)
				destroy_queue(q);
			else
				ast_log(LOG_DEBUG, "XXX Leaking a little memory :( XXX\n");
		} else {
			for (cur = q->members; cur; cur = cur->next)
				cur->status = ast_device_state(cur->interface);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&queues);
}

static int __queues_show(struct mansession *s, int manager, int fd, int argc, char **argv, int queue_show)
{
	struct ast_call_queue *q;
	struct queue_ent *qe;
	struct member *mem;
	int pos;
	time_t now;
	char max_buf[80];
	char *max;
	size_t max_left;
	float sl = 0;
	char *term = manager ? "\r\n" : "\n";

	time(&now);
	if ((!queue_show && argc != 2) || (queue_show && argc != 3))
		return RESULT_SHOWUSAGE;

	/* We only want to load realtime queues when a specific queue is asked for. */
	if (queue_show)
		load_realtime_queue(argv[2]);

	AST_LIST_LOCK(&queues);
	if (AST_LIST_EMPTY(&queues)) {
		AST_LIST_UNLOCK(&queues);
		if (queue_show) {
			if (s)
				astman_append(s, "No such queue: %s.%s",argv[2], term);
			else
				ast_cli(fd, "No such queue: %s.%s",argv[2], term);
		} else {
			if (s)
				astman_append(s, "No queues.%s", term);
			else
				ast_cli(fd, "No queues.%s", term);
		}
		return RESULT_SUCCESS;
	}
	AST_LIST_TRAVERSE(&queues, q, list) {
		ast_mutex_lock(&q->lock);
		if (queue_show) {
			if (strcasecmp(q->name, argv[2]) != 0) {
				ast_mutex_unlock(&q->lock);
				if (!AST_LIST_NEXT(q, list)) {
					ast_cli(fd, "No such queue: %s.%s",argv[2], term);
					break;
				}
				continue;
			}
		}
		max_buf[0] = '\0';
		max = max_buf;
		max_left = sizeof(max_buf);
		if (q->maxlen)
			ast_build_string(&max, &max_left, "%d", q->maxlen);
		else
			ast_build_string(&max, &max_left, "unlimited");
		sl = 0;
		if(q->callscompleted > 0)
			sl = 100*((float)q->callscompletedinsl/(float)q->callscompleted);
		if (s)
			astman_append(s, "%-12.12s has %d calls (max %s) in '%s' strategy (%ds holdtime), W:%d, C:%d, A:%d, SL:%2.1f%% within %ds%s",
				q->name, q->count, max_buf, int2strat(q->strategy), q->holdtime, q->weight, q->callscompleted, q->callsabandoned,sl,q->servicelevel, term);
		else
			ast_cli(fd, "%-12.12s has %d calls (max %s) in '%s' strategy (%ds holdtime), W:%d, C:%d, A:%d, SL:%2.1f%% within %ds%s",
				q->name, q->count, max_buf, int2strat(q->strategy), q->holdtime, q->weight, q->callscompleted, q->callsabandoned,sl,q->servicelevel, term);
		if (q->members) {
			if (s)
				astman_append(s, "   Members: %s", term);
			else
				ast_cli(fd, "   Members: %s", term);
			for (mem = q->members; mem; mem = mem->next) {
				max_buf[0] = '\0';
				max = max_buf;
				max_left = sizeof(max_buf);
				if (mem->penalty)
					ast_build_string(&max, &max_left, " with penalty %d", mem->penalty);
				if (mem->dynamic)
					ast_build_string(&max, &max_left, " (dynamic)");
				if (mem->paused)
					ast_build_string(&max, &max_left, " (paused)");
				ast_build_string(&max, &max_left, " (%s)", devstate2str(mem->status));
				if (mem->calls) {
					ast_build_string(&max, &max_left, " has taken %d calls (last was %ld secs ago)",
							 mem->calls, (long)(time(NULL) - mem->lastcall));
				} else
					ast_build_string(&max, &max_left, " has taken no calls yet");
				if (s)
					astman_append(s, "      %s%s%s", mem->interface, max_buf, term);
				else
					ast_cli(fd, "      %s%s%s", mem->interface, max_buf, term);
			}
		} else if (s)
			astman_append(s, "   No Members%s", term);
		else	
			ast_cli(fd, "   No Members%s", term);
		if (q->head) {
			pos = 1;
			if (s)
				astman_append(s, "   Callers: %s", term);
			else
				ast_cli(fd, "   Callers: %s", term);
			for (qe = q->head; qe; qe = qe->next) {
				if (s)
					astman_append(s, "      %d. %s (wait: %ld:%2.2ld, prio: %d)%s", pos++, qe->chan->name,
						(long)(now - qe->start) / 60, (long)(now - qe->start) % 60, qe->prio, term);
				else
					ast_cli(fd, "      %d. %s (wait: %ld:%2.2ld, prio: %d)%s", pos++, qe->chan->name,
						(long)(now - qe->start) / 60, (long)(now - qe->start) % 60, qe->prio, term);
			}
		} else if (s)
			astman_append(s, "   No Callers%s", term);
		else
			ast_cli(fd, "   No Callers%s", term);
		if (s)
			astman_append(s, "%s", term);
		else
			ast_cli(fd, "%s", term);
		ast_mutex_unlock(&q->lock);
		if (queue_show)
			break;
	}
	AST_LIST_UNLOCK(&queues);
	return RESULT_SUCCESS;
}

static int queues_show(int fd, int argc, char **argv)
{
	return __queues_show(NULL, 0, fd, argc, argv, 0);
}

static int queue_show(int fd, int argc, char **argv)
{
	return __queues_show(NULL, 0, fd, argc, argv, 1);
}

static char *complete_queue(const char *line, const char *word, int pos, int state)
{
	struct ast_call_queue *q;
	char *ret = NULL;
	int which = 0;
	int wordlen = strlen(word);
	
	AST_LIST_LOCK(&queues);
	AST_LIST_TRAVERSE(&queues, q, list) {
		if (!strncasecmp(word, q->name, wordlen) && ++which > state) {
			ret = ast_strdup(q->name);	
			break;
		}
	}
	AST_LIST_UNLOCK(&queues);

	return ret;
}

/*!\brief callback to display queues status in manager 
   \addtogroup Group_AMI 
 */
static int manager_queues_show( struct mansession *s, struct message *m )
{
	char *a[] = { "show", "queues" };
	__queues_show(s, 1, -1, 2, a, 0);
	astman_append(s, "\r\n\r\n");	/* Properly terminate Manager output */

	return RESULT_SUCCESS;
} 

/* Dump queue status */
static int manager_queues_status( struct mansession *s, struct message *m )
{
	time_t now;
	int pos;
	char *id = astman_get_header(m,"ActionID");
	char *queuefilter = astman_get_header(m,"Queue");
	char *memberfilter = astman_get_header(m,"Member");
	char idText[256] = "";
	struct ast_call_queue *q;
	struct queue_ent *qe;
	float sl = 0;
	struct member *mem;

	astman_send_ack(s, m, "Queue status will follow");
	time(&now);
	AST_LIST_LOCK(&queues);
	if (!ast_strlen_zero(id)) {
		snprintf(idText,256,"ActionID: %s\r\n",id);
	}
	AST_LIST_TRAVERSE(&queues, q, list) {
		ast_mutex_lock(&q->lock);

		/* List queue properties */
		if (ast_strlen_zero(queuefilter) || !strcmp(q->name, queuefilter)) {
			if(q->callscompleted > 0)
				sl = 100*((float)q->callscompletedinsl/(float)q->callscompleted);
			astman_append(s, "Event: QueueParams\r\n"
						"Queue: %s\r\n"
						"Max: %d\r\n"
						"Calls: %d\r\n"
						"Holdtime: %d\r\n"
						"Completed: %d\r\n"
						"Abandoned: %d\r\n"
						"ServiceLevel: %d\r\n"
						"ServicelevelPerf: %2.1f\r\n"
						"Weight: %d\r\n"
						"%s"
						"\r\n",
							q->name, q->maxlen, q->count, q->holdtime, q->callscompleted,
							q->callsabandoned, q->servicelevel, sl, q->weight, idText);
			/* List Queue Members */
			for (mem = q->members; mem; mem = mem->next) {
				if (ast_strlen_zero(memberfilter) || !strcmp(mem->interface, memberfilter)) {
					astman_append(s, "Event: QueueMember\r\n"
						"Queue: %s\r\n"
						"Location: %s\r\n"
						"Membership: %s\r\n"
						"Penalty: %d\r\n"
						"CallsTaken: %d\r\n"
						"LastCall: %d\r\n"
						"Status: %d\r\n"
						"Paused: %d\r\n"
						"%s"
						"\r\n",
							q->name, mem->interface, mem->dynamic ? "dynamic" : "static",
							mem->penalty, mem->calls, (int)mem->lastcall, mem->status, mem->paused, idText);
				}
			}
			/* List Queue Entries */
			pos = 1;
			for (qe = q->head; qe; qe = qe->next) {
				astman_append(s, "Event: QueueEntry\r\n"
					"Queue: %s\r\n"
					"Position: %d\r\n"
					"Channel: %s\r\n"
					"CallerID: %s\r\n"
					"CallerIDName: %s\r\n"
					"Wait: %ld\r\n"
					"%s"
					"\r\n", 
					q->name, pos++, qe->chan->name, 
					S_OR(qe->chan->cid.cid_num, "unknown"),
					S_OR(qe->chan->cid.cid_name, "unknown"),
					(long)(now - qe->start), idText);
			}
		}
		ast_mutex_unlock(&q->lock);
	}
	AST_LIST_UNLOCK(&queues);

	astman_append(s,
		"Event: QueueStatusComplete\r\n"
		"%s"
		"\r\n",idText);


	return RESULT_SUCCESS;
}

static int manager_add_queue_member(struct mansession *s, struct message *m)
{
	char *queuename, *interface, *penalty_s, *paused_s;
	int paused, penalty = 0;

	queuename = astman_get_header(m, "Queue");
	interface = astman_get_header(m, "Interface");
	penalty_s = astman_get_header(m, "Penalty");
	paused_s = astman_get_header(m, "Paused");

	if (ast_strlen_zero(queuename)) {
		astman_send_error(s, m, "'Queue' not specified.");
		return 0;
	}

	if (ast_strlen_zero(interface)) {
		astman_send_error(s, m, "'Interface' not specified.");
		return 0;
	}

	if (ast_strlen_zero(penalty_s))
		penalty = 0;
	else if (sscanf(penalty_s, "%d", &penalty) != 1)
		penalty = 0;

	if (ast_strlen_zero(paused_s))
		paused = 0;
	else
		paused = abs(ast_true(paused_s));

	switch (add_to_queue(queuename, interface, penalty, paused, queue_persistent_members)) {
	case RES_OKAY:
		astman_send_ack(s, m, "Added interface to queue");
		break;
	case RES_EXISTS:
		astman_send_error(s, m, "Unable to add interface: Already there");
		break;
	case RES_NOSUCHQUEUE:
		astman_send_error(s, m, "Unable to add interface to queue: No such queue");
		break;
	case RES_OUTOFMEMORY:
		astman_send_error(s, m, "Out of memory");
		break;
	}
	return 0;
}

static int manager_remove_queue_member(struct mansession *s, struct message *m)
{
	char *queuename, *interface;

	queuename = astman_get_header(m, "Queue");
	interface = astman_get_header(m, "Interface");

	if (ast_strlen_zero(queuename) || ast_strlen_zero(interface)) {
		astman_send_error(s, m, "Need 'Queue' and 'Interface' parameters.");
		return 0;
	}

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		astman_send_ack(s, m, "Removed interface from queue");
		break;
	case RES_EXISTS:
		astman_send_error(s, m, "Unable to remove interface: Not there");
		break;
	case RES_NOSUCHQUEUE:
		astman_send_error(s, m, "Unable to remove interface from queue: No such queue");
		break;
	case RES_OUTOFMEMORY:
		astman_send_error(s, m, "Out of memory");
		break;
	}
	return 0;
}

static int manager_pause_queue_member(struct mansession *s, struct message *m)
{
	char *queuename, *interface, *paused_s;
	int paused;

	interface = astman_get_header(m, "Interface");
	paused_s = astman_get_header(m, "Paused");
	queuename = astman_get_header(m, "Queue");	/* Optional - if not supplied, pause the given Interface in all queues */

	if (ast_strlen_zero(interface) || ast_strlen_zero(paused_s)) {
		astman_send_error(s, m, "Need 'Interface' and 'Paused' parameters.");
		return 0;
	}

	paused = abs(ast_true(paused_s));

	if (set_member_paused(queuename, interface, paused))
		astman_send_error(s, m, "Interface not found");
	else
		astman_send_ack(s, m, paused ? "Interface paused successfully" : "Interface unpaused successfully");
	return 0;
}

static int handle_add_queue_member(int fd, int argc, char *argv[])
{
	char *queuename, *interface;
	int penalty;

	if ((argc != 6) && (argc != 8)) {
		return RESULT_SHOWUSAGE;
	} else if (strcmp(argv[4], "to")) {
		return RESULT_SHOWUSAGE;
	} else if ((argc == 8) && strcmp(argv[6], "penalty")) {
		return RESULT_SHOWUSAGE;
	}

	queuename = argv[5];
	interface = argv[3];
	if (argc == 8) {
		if (sscanf(argv[7], "%d", &penalty) == 1) {
			if (penalty < 0) {
				ast_cli(fd, "Penalty must be >= 0\n");
				penalty = 0;
			}
		} else {
			ast_cli(fd, "Penalty must be an integer >= 0\n");
			penalty = 0;
		}
	} else {
		penalty = 0;
	}

	switch (add_to_queue(queuename, interface, penalty, 0, queue_persistent_members)) {
	case RES_OKAY:
		ast_cli(fd, "Added interface '%s' to queue '%s'\n", interface, queuename);
		return RESULT_SUCCESS;
	case RES_EXISTS:
		ast_cli(fd, "Unable to add interface '%s' to queue '%s': Already there\n", interface, queuename);
		return RESULT_FAILURE;
	case RES_NOSUCHQUEUE:
		ast_cli(fd, "Unable to add interface to queue '%s': No such queue\n", queuename);
		return RESULT_FAILURE;
	case RES_OUTOFMEMORY:
		ast_cli(fd, "Out of memory\n");
		return RESULT_FAILURE;
	default:
		return RESULT_FAILURE;
	}
}

static char *complete_add_queue_member(const char *line, const char *word, int pos, int state)
{
	/* 0 - add; 1 - queue; 2 - member; 3 - <member>; 4 - to; 5 - <queue>; 6 - penalty; 7 - <penalty> */
	switch (pos) {
	case 3:	/* Don't attempt to complete name of member (infinite possibilities) */
		return NULL;
	case 4:	/* only one possible match, "to" */
		return state == 0 ? ast_strdup("to") : NULL;
	case 5:	/* <queue> */
		return complete_queue(line, word, pos, state);
	case 6: /* only one possible match, "penalty" */
		return state == 0 ? ast_strdup("penalty") : NULL;
	case 7:
		if (state < 100) {	/* 0-99 */
			char *num;
			if ((num = ast_malloc(3))) {
				sprintf(num, "%d", state);
			}
			return num;
		} else {
			return NULL;
		}
	default:
		return NULL;
	}
}

static int handle_remove_queue_member(int fd, int argc, char *argv[])
{
	char *queuename, *interface;

	if (argc != 6) {
		return RESULT_SHOWUSAGE;
	} else if (strcmp(argv[4], "from")) {
		return RESULT_SHOWUSAGE;
	}

	queuename = argv[5];
	interface = argv[3];

	switch (remove_from_queue(queuename, interface)) {
	case RES_OKAY:
		ast_cli(fd, "Removed interface '%s' from queue '%s'\n", interface, queuename);
		return RESULT_SUCCESS;
	case RES_EXISTS:
		ast_cli(fd, "Unable to remove interface '%s' from queue '%s': Not there\n", interface, queuename);
		return RESULT_FAILURE;
	case RES_NOSUCHQUEUE:
		ast_cli(fd, "Unable to remove interface from queue '%s': No such queue\n", queuename);
		return RESULT_FAILURE;
	case RES_OUTOFMEMORY:
		ast_cli(fd, "Out of memory\n");
		return RESULT_FAILURE;
	default:
		return RESULT_FAILURE;
	}
}

static char *complete_remove_queue_member(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	struct ast_call_queue *q;
	struct member *m;

	/* 0 - add; 1 - queue; 2 - member; 3 - <member>; 4 - from; 5 - <queue> */
	if (pos > 5 || pos < 3)
		return NULL;
	if (pos == 4)	/* only one possible match, 'from' */
		return state == 0 ? ast_strdup("from") : NULL;

	if (pos == 5)	/* No need to duplicate code */
		return complete_queue(line, word, pos, state);

	/* here is the case for 3, <member> */
	if (!AST_LIST_EMPTY(&queues)) { /* XXX unnecessary ? the traverse does that for us */
		AST_LIST_TRAVERSE(&queues, q, list) {
			ast_mutex_lock(&q->lock);
			for (m = q->members ; m ; m = m->next) {
				if (++which > state) {
					ast_mutex_unlock(&q->lock);
					return ast_strdup(m->interface);
				}
			}
			ast_mutex_unlock(&q->lock);
		}
	}
	return NULL;
}

static char show_queues_usage[] = 
"Usage: show queues\n"
"       Provides summary information on call queues.\n";

static struct ast_cli_entry cli_show_queues = {
	{ "show", "queues", NULL }, queues_show, 
	"Show status of queues", show_queues_usage, NULL };

static char show_queue_usage[] = 
"Usage: show queue\n"
"       Provides summary information on a specified queue.\n";

static struct ast_cli_entry cli_show_queue = {
	{ "show", "queue", NULL }, queue_show, 
	"Show status of a specified queue", show_queue_usage, complete_queue };

static char aqm_cmd_usage[] =
"Usage: add queue member <channel> to <queue> [penalty <penalty>]\n";

static struct ast_cli_entry cli_add_queue_member = {
	{ "add", "queue", "member", NULL }, handle_add_queue_member,
	"Add a channel to a specified queue", aqm_cmd_usage, complete_add_queue_member };

static char rqm_cmd_usage[] =
"Usage: remove queue member <channel> from <queue>\n";

static struct ast_cli_entry cli_remove_queue_member = {
	{ "remove", "queue", "member", NULL }, handle_remove_queue_member,
	"Removes a channel from a specified queue", rqm_cmd_usage, complete_remove_queue_member };

static int unload_module(void *mod)
{
	int res;

	res = ast_cli_unregister(&cli_show_queue);
	res |= ast_cli_unregister(&cli_show_queues);
	res |= ast_cli_unregister(&cli_add_queue_member);
	res |= ast_cli_unregister(&cli_remove_queue_member);
	res |= ast_manager_unregister("Queues");
	res |= ast_manager_unregister("QueueStatus");
	res |= ast_manager_unregister("QueueAdd");
	res |= ast_manager_unregister("QueueRemove");
	res |= ast_manager_unregister("QueuePause");
	ast_devstate_del(statechange_queue, NULL);
	res |= ast_unregister_application(app_aqm);
	res |= ast_unregister_application(app_rqm);
	res |= ast_unregister_application(app_pqm);
	res |= ast_unregister_application(app_upqm);
	res |= ast_custom_function_unregister(&queueagentcount_function);
	res |= ast_custom_function_unregister(&queuemembercount_function);
	res |= ast_custom_function_unregister(&queuememberlist_function);
	res |= ast_custom_function_unregister(&queuewaitingcount_function);
	res |= ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

static int load_module(void *mod)
{
	int res;
	
	res = ast_register_application(app, queue_exec, synopsis, descrip);
	res |= ast_cli_register(&cli_show_queue);
	res |= ast_cli_register(&cli_show_queues);
	res |= ast_cli_register(&cli_add_queue_member);
	res |= ast_cli_register(&cli_remove_queue_member);
	res |= ast_devstate_add(statechange_queue, NULL);
	res |= ast_manager_register( "Queues", 0, manager_queues_show, "Queues" );
	res |= ast_manager_register( "QueueStatus", 0, manager_queues_status, "Queue Status" );
	res |= ast_manager_register( "QueueAdd", EVENT_FLAG_AGENT, manager_add_queue_member, "Add interface to queue." );
	res |= ast_manager_register( "QueueRemove", EVENT_FLAG_AGENT, manager_remove_queue_member, "Remove interface from queue." );
	res |= ast_manager_register( "QueuePause", EVENT_FLAG_AGENT, manager_pause_queue_member, "Makes a queue member temporarily unavailable" );
	res |= ast_register_application(app_aqm, aqm_exec, app_aqm_synopsis, app_aqm_descrip) ;
	res |= ast_register_application(app_rqm, rqm_exec, app_rqm_synopsis, app_rqm_descrip) ;
	res |= ast_register_application(app_pqm, pqm_exec, app_pqm_synopsis, app_pqm_descrip) ;
	res |= ast_register_application(app_upqm, upqm_exec, app_upqm_synopsis, app_upqm_descrip) ;
	res |= ast_custom_function_register(&queueagentcount_function);
	res |= ast_custom_function_register(&queuemembercount_function);
	res |= ast_custom_function_register(&queuememberlist_function);
	res |= ast_custom_function_register(&queuewaitingcount_function);

	if (!res) {	
		reload_queues();
		if (queue_persistent_members)
			reload_queue_members();
	}

	return res;
}

static int reload(void *mod)
{
	reload_queues();
	return 0;
}

static const char *description(void)
{
	return tdesc;
}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD(MOD_1, reload, NULL, NULL);

