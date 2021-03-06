/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2003 - 2006, Aheeva Technology.
 *
 * Claude Klimos (claude.klimos@aheeva.com)
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
 *
 * A license has been granted to Digium (via disclaimer) for the use of
 * this code.
 */

#include <stdio.h>
#include <stdlib.h>

#include "asterisk.h"
 
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/dsp.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/app.h"


static char *app = "AMD";
static char *synopsis = "Attempts to detect answering machines";
static char *descrip =
"  AMD([initialSilence][|greeting][|afterGreetingSilence][|totalAnalysisTime]\n"
"      [|minimumWordLength][|betweenWordsSilence][|maximumNumberOfWords]\n"
"      [|silenceThreshold])\n"
"  This application attempts to detect answering machines at the beginning\n"
"  of outbound calls.  Simply call this application after the call\n"
"  has been answered (outbound only, of course).\n"
"  When loaded, AMD reads amd.conf and uses the parameters specified as\n"
"  default values. Those default values get overwritten when calling AMD\n"
"  with parameters.\n"
"- 'initialSilence' is the maximum silence duration before the greeting. If\n"
"   exceeded then MACHINE.\n"
"- 'greeting' is the maximum length of a greeting. If exceeded then MACHINE.\n"
"- 'afterGreetingSilence' is the silence after detecting a greeting.\n"
"   If exceeded then HUMAN.\n"
"- 'totalAnalysisTime' is the maximum time allowed for the algorithm to decide\n"
"   on a HUMAN or MACHINE.\n"
"- 'minimumWordLength'is the minimum duration of Voice to considered as a word.\n"
"- 'betweenWordsSilence' is the minimum duration of silence after a word to \n"
"   consider the audio that follows as a new word.\n"
"- 'maximumNumberOfWords'is the maximum number of words in the greeting. \n"
"   If exceeded then MACHINE.\n"
"- 'silenceThreshold' is the silence threshold.\n"
"This application sets the following channel variable upon completion:\n"
"    AMDSTATUS - This is the status of the answering machine detection.\n"
"                Possible values are:\n"
"                MACHINE | HUMAN | NOTSURE | HANGUP\n"
"    AMDCAUSE - Indicates the cause that led to the conclusion.\n"
"               Possible values are:\n"
"               TOOLONG-<%d total_time>\n"
"               INITIALSILENCE-<%d silenceDuration>-<%d initialSilence>\n"
"               HUMAN-<%d silenceDuration>-<%d afterGreetingSilence>\n"
"               MAXWORDS-<%d wordsCount>-<%d maximumNumberOfWords>\n"
"               LONGGREETING-<%d voiceDuration>-<%d greeting>\n";

#define STATE_IN_WORD       1
#define STATE_IN_SILENCE    2

/* Some default values for the algorithm parameters. These defaults will be overwritten from amd.conf */
static int dfltInitialSilence       = 2500;
static int dfltGreeting             = 1500;
static int dfltAfterGreetingSilence = 800;
static int dfltTotalAnalysisTime    = 5000;
static int dfltMinimumWordLength    = 100;
static int dfltBetweenWordsSilence  = 50;
static int dfltMaximumNumberOfWords = 3;
static int dfltSilenceThreshold     = 256;

static void isAnsweringMachine(struct ast_channel *chan, void *data)
{
	int res = 0, ret = 0;

	struct ast_frame *f = NULL;

	struct ast_dsp *silenceDetector;         /* silence detector dsp */
	int dspsilence = 0;
	int readFormat;
	int framelength;

	int inInitialSilence         = 1;
	int inGreeting               = 0;
	int voiceDuration            = 0;
	int silenceDuration          = 0;
	int iTotalTime               = 0;
	int iWordsCount              = 0;
	int currentState             = STATE_IN_SILENCE;
	int previousState            = STATE_IN_SILENCE;
	int consecutiveVoiceDuration = 0;
	char amdCause[256]           = "";
	char amdStatus[256]          = "";

	/* Lets set the initial values of the variables that will control the algorithm.
	   The initial values are the default ones. If they are passed as arguments
	   when invoking the application, then the default values will be overwritten
	   by the ones passed as parameters. */
	int initialSilence       = dfltInitialSilence;
	int greeting             = dfltGreeting;
	int afterGreetingSilence = dfltAfterGreetingSilence;
	int totalAnalysisTime    = dfltTotalAnalysisTime;
	int minimumWordLength    = dfltMinimumWordLength;
	int betweenWordsSilence  = dfltBetweenWordsSilence;
	int maximumNumberOfWords = dfltMaximumNumberOfWords;
	int silenceThreshold     = dfltSilenceThreshold;

	char *parse;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(argInitialSilence);
			     AST_APP_ARG(argGreeting);
			     AST_APP_ARG(argAfterGreetingSilence);
			     AST_APP_ARG(argTotalAnalysisTime);
			     AST_APP_ARG(argMinimumWordLength);
			     AST_APP_ARG(argBetweenWordsSilence);
			     AST_APP_ARG(argMaximumNumberOfWords);
			     AST_APP_ARG(argSilenceThreshold);
	);
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "AMD: %s %s %s (Fmt: %d)\n", chan->name ,chan->cid.cid_ani, chan->cid.cid_rdnis, chan->readformat);

	/* Lets parse the arguments. */
	if (ast_strlen_zero(data)) {
		ast_log(LOG_NOTICE, "AMD using the default parameters.\n");
	} else {
		/* Some arguments have been passed. Lets parse them and overwrite the defaults. */
		parse = ast_strdupa(data);

		AST_STANDARD_APP_ARGS(args, parse);

		if (!ast_strlen_zero(args.argInitialSilence)) {
			initialSilence = atoi(args.argInitialSilence);
		}
		if (!ast_strlen_zero(args.argGreeting)) {
			greeting = atoi(args.argGreeting);
		}
		if (!ast_strlen_zero(args.argAfterGreetingSilence)) {
			afterGreetingSilence = atoi(args.argAfterGreetingSilence);
		}
		if (!ast_strlen_zero(args.argTotalAnalysisTime)) {
			totalAnalysisTime = atoi(args.argTotalAnalysisTime);
		}
		if (!ast_strlen_zero(args.argMinimumWordLength)) {
			minimumWordLength = atoi(args.argMinimumWordLength);
		}
		if (!ast_strlen_zero(args.argBetweenWordsSilence)) {
			betweenWordsSilence = atoi(args.argBetweenWordsSilence);
		}
		if (!ast_strlen_zero(args.argMaximumNumberOfWords)) {
			maximumNumberOfWords = atoi(args.argMaximumNumberOfWords);
		}
		if (!ast_strlen_zero(args.argSilenceThreshold)) {
			silenceThreshold = atoi(args.argSilenceThreshold);
		}
	}

	/* Now we're ready to roll! */
	
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "AMD: initialSilence [%d] greeting [%d] afterGreetingSilence [%d] "
		"totalAnalysisTime [%d] minimumWordLength [%d] betweenWordsSilence [%d] maximumNumberOfWords [%d] silenceThreshold [%d] \n",
				initialSilence, greeting, afterGreetingSilence, totalAnalysisTime,
				minimumWordLength, betweenWordsSilence, maximumNumberOfWords, silenceThreshold );

	readFormat = chan->readformat;
	res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
	if (res < 0 ) {
		ast_log(LOG_WARNING, "AMD: Channel [%s]. Unable to set to linear mode, giving up\n", chan->name );
		pbx_builtin_setvar_helper(chan , "AMDSTATUS" , "" );
		pbx_builtin_setvar_helper(chan , "AMDCAUSE" , "" );
		return;
	}

	silenceDetector = ast_dsp_new();
	if (!silenceDetector ) {
		ast_log(LOG_WARNING, "AMD: Channel [%s]. Unable to create silence detector :(\n", chan->name );
		pbx_builtin_setvar_helper(chan , "AMDSTATUS" , "" );
		pbx_builtin_setvar_helper(chan , "AMDCAUSE" , "" );
		return;
	}
	ast_dsp_set_threshold(silenceDetector, silenceThreshold );

	while ((ret = ast_waitfor(chan, totalAnalysisTime)))
	{
		if (ret < 0 || !(f = ast_read(chan))) {
			/* No Frame OR Error on ast_waitfor : Called Party Must Have Dropped */
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "AMD: HANGUP\n");
			if (option_debug)
				ast_log(LOG_DEBUG, "Got hangup\n");
			strcpy(amdStatus , "HANGUP" );
			strcpy(amdCause , "" );
			break;
		}
		if (f->frametype == AST_FRAME_VOICE ) {
			framelength = (ast_codec_get_samples(f) / DEFAULT_SAMPLES_PER_MS);
			iTotalTime += framelength;
			if (iTotalTime >= totalAnalysisTime ) {
				if (option_verbose > 2)	
					ast_verbose(VERBOSE_PREFIX_3 "AMD: Channel [%s]. Too long...\n", chan->name );
				ast_frfree(f);
				strcpy(amdStatus , "NOTSURE" );
				sprintf(amdCause , "TOOLONG-%d", iTotalTime );
				break;
			}
			dspsilence = 0;
			ast_dsp_silence(silenceDetector, f, &dspsilence);
			if (dspsilence ) {
				silenceDuration = dspsilence;
				if (silenceDuration >= betweenWordsSilence ) {
					if (currentState != STATE_IN_SILENCE ) {
						previousState = currentState;
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "AMD: Changed state to STATE_IN_SILENCE\n");
					}
					currentState  = STATE_IN_SILENCE;
					consecutiveVoiceDuration = 0;
				}
				if (inInitialSilence == 1  && silenceDuration >= initialSilence ) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "AMD: ANSWERING MACHINE: silenceDuration:%d initialSilence:%d\n",
							silenceDuration, initialSilence );
					ast_frfree(f);
					strcpy(amdStatus , "MACHINE" );
					sprintf(amdCause , "INITIALSILENCE-%d-%d", silenceDuration, initialSilence );
					break;
				}

				if (silenceDuration >= afterGreetingSilence  &&  inGreeting == 1 ) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "AMD: HUMAN: silenceDuration:%d afterGreetingSilence:%d\n",
							silenceDuration, afterGreetingSilence );
					ast_frfree(f);
					strcpy(amdStatus , "HUMAN" );
					sprintf(amdCause , "HUMAN-%d-%d", silenceDuration, afterGreetingSilence );
					break;
				}
			} else {
				consecutiveVoiceDuration += framelength;
				voiceDuration += framelength;

				/* If I have enough consecutive voice to say that I am in a Word, I can only increment the
					number of words if my previous state was Silence, which means that I moved into a word. */
				if (consecutiveVoiceDuration >= minimumWordLength ) {
					if (currentState == STATE_IN_SILENCE ) {
						iWordsCount++;
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "AMD: Word detected. iWordsCount:%d\n", iWordsCount );
						previousState = currentState;
						currentState = STATE_IN_WORD;
					}
				}

				if (iWordsCount >= maximumNumberOfWords ) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "AMD: ANSWERING MACHINE: iWordsCount:%d\n", iWordsCount );
					ast_frfree(f);
					strcpy(amdStatus , "MACHINE" );
					sprintf(amdCause , "MAXWORDS-%d-%d", iWordsCount, maximumNumberOfWords );
					break;
				}

				if (inGreeting == 1  &&  voiceDuration >= greeting ) {
					if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "AMD: ANSWERING MACHINE: voiceDuration:%d greeting:%d\n", voiceDuration, greeting);
					 ast_frfree(f);
					strcpy(amdStatus , "MACHINE" );
					sprintf(amdCause , "LONGGREETING-%d-%d", voiceDuration, greeting );
					break;
				}
				if (voiceDuration >= minimumWordLength ) {
					silenceDuration = 0;
					inInitialSilence = 0;
					inGreeting = 1;
				}
			}
		}
		ast_frfree(f);
	}
	if (!ret) {
		/* It took too long to get a frame back. Giving up. */
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "AMD: Channel [%s]. Too long...\n", chan->name );
		strcpy(amdStatus , "NOTSURE" );
		sprintf(amdCause , "TOOLONG-%d", iTotalTime );
	}

	pbx_builtin_setvar_helper(chan , "AMDSTATUS" , amdStatus );
	pbx_builtin_setvar_helper(chan , "AMDCAUSE" , amdCause );

	/* If We Started With A Valid Read Format, Return To It... */
	if (readFormat && chan->_state == AST_STATE_UP) {
		res = ast_set_read_format(chan, readFormat );
		if (res)
			ast_log(LOG_WARNING, "AMD: Unable to restore read format on '%s'\n", chan->name);
	}

	/* Free The Silence Detector DSP */
	ast_dsp_free(silenceDetector );

	return;
}


static int amd_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;

	LOCAL_USER_ADD(u);
	isAnsweringMachine(chan, data);
	LOCAL_USER_REMOVE(u);

	return 0;
}

static void load_config(void)
{
	struct ast_config *cfg;
	char *cat;
	struct ast_variable *var;

	cfg = ast_config_load("amd.conf");

	if (!cfg) {
		ast_log(LOG_ERROR, "Configuration file amd.conf missing.\n");
		return;
	}

	cat = ast_category_browse(cfg, NULL);

	while (cat) {
		if (!strcasecmp(cat, "general") ) {
			var = ast_variable_browse(cfg, cat);
			while (var) {
				if (!strcasecmp(var->name, "initial_silence")) {
					dfltInitialSilence = atoi(var->value);
				} else if (!strcasecmp(var->name, "greeting")) {
					dfltGreeting = atoi(var->value);
				} else if (!strcasecmp(var->name, "after_greeting_silence")) {
					dfltAfterGreetingSilence = atoi(var->value);
				} else if (!strcasecmp(var->name, "silence_threshold")) {
					dfltSilenceThreshold = atoi(var->value);
				} else if (!strcasecmp(var->name, "total_analysis_time")) {
					dfltTotalAnalysisTime = atoi(var->value);
				} else if (!strcasecmp(var->name, "min_word_length")) {
					dfltMinimumWordLength = atoi(var->value);
				} else if (!strcasecmp(var->name, "between_words_silence")) {
					dfltBetweenWordsSilence = atoi(var->value);
				} else if (!strcasecmp(var->name, "maximum_number_of_words")) {
					dfltMaximumNumberOfWords = atoi(var->value);
				} else {
					ast_log(LOG_WARNING, "%s: Cat:%s. Unknown keyword %s at line %d of amd.conf\n",
						app, cat, var->name, var->lineno);
				}
				var = var->next;
			}
		}
		cat = ast_category_browse(cfg, cat);
	}
	ast_config_destroy(cfg);

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "AMD defaults: initialSilence [%d] greeting [%d] afterGreetingSilence [%d] "
		"totalAnalysisTime [%d] minimumWordLength [%d] betweenWordsSilence [%d] maximumNumberOfWords [%d] silenceThreshold [%d] \n",
				dfltInitialSilence, dfltGreeting, dfltAfterGreetingSilence, dfltTotalAnalysisTime,
				dfltMinimumWordLength, dfltBetweenWordsSilence, dfltMaximumNumberOfWords, dfltSilenceThreshold );

	return;
}

static int unload_module(void *mod)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

static int load_module(void *mod)
{
	__mod_desc = mod;
	load_config();
	return ast_register_application(app, amd_exec, synopsis, descrip);
}

static int reload(void *mod)
{
	load_config();
	return 0;
}

static const char *description(void)
{
	return "Answering Machine Detection Application";
}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD(MOD_1, reload, NULL, NULL);
