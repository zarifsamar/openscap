/*
 * Copyright 2010 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      Maros Barabas <mbarabas@redhat.com>
 */

/* OVAL & OSCAP common */
#include <oval_probe.h>
#include <oval_agent_api.h>
#include <oval_results.h>
#include <oval_variables.h>
#include <error.h>
#include <text.h>
#include <assert.h>

#include "oscap-tool.h"

static int app_collect_oval(const struct oscap_action *action);
static int app_evaluate_oval(const struct oscap_action *action);
static int app_oval_xslt(const struct oscap_action *action);
static bool getopt_oval(int argc, char **argv, struct oscap_action *action);
static int app_analyse_oval(const struct oscap_action *action);

static struct oscap_module* OVAL_SUBMODULES[];
static struct oscap_module* OVAL_GEN_SUBMODULES[];

struct oscap_module OSCAP_OVAL_MODULE = {
    .name = "oval",
    .parent = &OSCAP_ROOT_MODULE,
    .summary = "Open Vulnerability and Assessment Language",
    .submodules = OVAL_SUBMODULES
};

static struct oscap_module OVAL_VALIDATE = {
    .name = "validate-xml",
    .parent = &OSCAP_OVAL_MODULE,
    .summary = "Validate OVAL XML content",
    .usage = "[options] oval-file.xml",
    .help =
        "Options:\n"
        "   --syschar\r\t\t\t\t - Valiadate OVAL system characteristics\n"
        "   --definitions\r\t\t\t\t - Valiadate OVAL definitions\n"
        "   --results\r\t\t\t\t - Valiadate OVAL results\n"
        "   --file-version <version>\r\t\t\t\t - Use schema for given version of OVAL",
    .opt_parser = getopt_oval,
    .func = app_validate_xml
};

static struct oscap_module OVAL_EVAL = {
    .name = "eval",
    .parent = &OSCAP_OVAL_MODULE,
    .summary = "Probe the system and evaluate definitions from OVAL Definition file",
    .usage = "[options] oval-definitions.xml",
    .help =
        "Options:\n"
	"   --id <definition-id>\r\t\t\t\t - ID of the definition we want to evaluate.\n"
        "   --result-file <file>\r\t\t\t\t - Write OVAL Results into file.\n"
        "   --report-file <file>\r\t\t\t\t - Write results HTML report into file.\n"
        "   --skip-valid\r\t\t\t\t - Skip validation.\n",
    .opt_parser = getopt_oval,
    .func = app_evaluate_oval
};

static struct oscap_module OVAL_COLLECT = {
    .name = "collect",
    .parent = &OSCAP_OVAL_MODULE,
    .summary = "Probe the system and create system characteristics",
    .usage = "oval-definitions.xml",
    .opt_parser = getopt_oval,
    .func = app_collect_oval
};

static struct oscap_module OVAL_ANALYSE = {
    .name = "analyse",
    .parent = &OSCAP_OVAL_MODULE,
    .summary = "Evaluate provided system characteristics file",
    .usage = "[options] oval-definitions.xml system-characteristics.xml" ,
    .help =
	"Options:\n"
	"   --result-file <file>\r\t\t\t\t - Write OVAL Results into file.\n",
    .opt_parser = getopt_oval,
    .func = app_analyse_oval
};

static struct oscap_module OVAL_GENERATE = {
    .name = "generate",
    .parent = &OSCAP_OVAL_MODULE,
    .summary = "Convert an OVAL file to other formats",
    .usage_extra = "<subcommand> [sub-options] oval-file.xml",
    .submodules = OVAL_GEN_SUBMODULES
};

static struct oscap_module OVAL_REPORT = {
    .name = "report",
    .parent = &OVAL_GENERATE,
    .summary = "Generate a HTML report from OVAL results file",
    .usage = "[options] oval-file.xml",
    .help =
        "Options:\n"
        "   --output <file>\r\t\t\t\t - Write the HTML into file.",
    .opt_parser = getopt_oval,
    .user = "oval-results-report.xsl",
    .func = app_oval_xslt
};

static struct oscap_module* OVAL_GEN_SUBMODULES[] = {
    &OVAL_REPORT,
    NULL
};
static struct oscap_module* OVAL_SUBMODULES[] = {
    &OVAL_COLLECT,
    &OVAL_EVAL,
    &OVAL_ANALYSE,
    &OVAL_VALIDATE,
    &OVAL_GENERATE,
    NULL
};


int VERBOSE;

struct oval_usr {
	int result_false;
	int result_true;
	int result_error;
	int result_unknown;
	int result_neval;
	int result_napp;
};

static int oval_gen_report(const char *infile, const char *outfile)
{
    return app_xslt(infile, "oval-results-report.xsl", outfile, NULL);
}

static int app_oval_callback(const struct oscap_reporter_message *msg, void *arg)
{

	if (VERBOSE >= 0)
		printf("Definition %s: %s\n",
		       oscap_reporter_message_get_user1str(msg),
		       oval_result_get_text(oscap_reporter_message_get_user2num(msg)));
	switch ((oval_result_t) oscap_reporter_message_get_user2num(msg)) {
	case OVAL_RESULT_TRUE:
		((struct oval_usr *)arg)->result_true++;
		break;
	case OVAL_RESULT_FALSE:
		((struct oval_usr *)arg)->result_false++;
		break;
	case OVAL_RESULT_ERROR:
		((struct oval_usr *)arg)->result_error++;
		break;
	case OVAL_RESULT_UNKNOWN:
		((struct oval_usr *)arg)->result_unknown++;
		break;
	case OVAL_RESULT_NOT_EVALUATED:
		((struct oval_usr *)arg)->result_neval++;
		break;
	case OVAL_RESULT_NOT_APPLICABLE:
		((struct oval_usr *)arg)->result_napp++;
		break;
	default:
		break;
	}

	return 0;
}

int app_collect_oval(const struct oscap_action *action)
{
	int ret;
	struct oval_sysinfo *sysinfo;

	/* import definitions */
	struct oval_definition_model *def_model = oval_definition_model_import(action->f_oval);

	/* create empty syschar model */
	struct oval_syschar_model *sys_model = oval_syschar_model_new(def_model);

	/* create probe session */
	struct oval_probe_session *pb_sess = oval_probe_session_new(sys_model);

	/* query sysinfo */
	ret = oval_probe_query_sysinfo(pb_sess, &sysinfo);
	if (ret != 0) {
		oval_probe_session_destroy(pb_sess);
		oval_syschar_model_free(sys_model);
		oval_definition_model_free(def_model);
		return OSCAP_ERROR;
	}
	oval_syschar_model_set_sysinfo(sys_model, sysinfo);
	oval_sysinfo_free(sysinfo);

	/* query objects */
	ret = oval_probe_query_objects(pb_sess);
	if (ret != 0) {
		oval_probe_session_destroy(pb_sess);
		oval_syschar_model_free(sys_model);
		oval_definition_model_free(def_model);
		return OSCAP_ERROR;
	}

	/* report */
	oval_syschar_model_export(sys_model, "/dev/stdout");

	/* destroy */
	oval_probe_session_destroy(pb_sess);
	oval_syschar_model_free(sys_model);
	oval_definition_model_free(def_model);

	return OSCAP_OK;
}


int app_evaluate_oval(const struct oscap_action *action)
{

	struct oval_usr *usr = NULL;
	int ret = OSCAP_OK;
	struct oval_definition_model *def_model;
	oval_agent_session_t *sess;

	/* validate */
	if ( action->validate ) {
		if (!oscap_validate_document(action->f_oval, OSCAP_DOCUMENT_OVAL_DEFINITIONS, NULL,
		    (action->verbosity >= 0 ? oscap_reporter_fd : NULL), stdout)) {
			if (oscap_err()) {
				fprintf(stderr, "ERROR: %s\n", oscap_err_desc());
				return OSCAP_FAIL;
			}
			fprintf(stdout, "%s\n", INVALID_DOCUMENT_MSG);
			return OSCAP_ERROR;
		}
	}
	def_model = oval_definition_model_import(action->f_oval);
	if (def_model == NULL) {
		fprintf(stderr, "Failed to import the definition model (%s).\n", action->f_oval);
		return OSCAP_ERROR;
	}

	sess = oval_agent_new_session(def_model, basename(action->f_oval));
	if (sess == NULL) {
		if (oscap_err())
			fprintf(stderr, "Error: (%d) %s\n", oscap_err_code(), oscap_err_desc());
		fprintf(stderr, "Failed to create new agent session.\n");
		return OSCAP_ERROR;
	}

	/* Init usr structure */
	usr = malloc(sizeof(struct oval_usr));
	memset(usr, 0, sizeof(struct oval_usr));

	/* Evaluation */
	if (action->id) {
		ret = oval_agent_eval_definition(sess, action->id);
		if (VERBOSE >= 0)
			printf("Definition %s: %s\n", action->id, oval_result_get_text(ret));
	} else
		ret = oval_agent_eval_system(sess, app_oval_callback, usr);

	if (VERBOSE >= 0) {
		printf("Evaluation done.\n");
	}

	if (ret == -1 && (oscap_err())) {
		fprintf(stderr, "Error: (%d) %s\n", oscap_err_code(), oscap_err_desc());
		return OSCAP_ERROR;
	}

	/* print report */
	if (VERBOSE >= 0 && !action->id) {
		fprintf(stdout, "===== REPORT =====\n");
		fprintf(stdout, "TRUE:          \r\t\t %d\n", usr->result_true);
		fprintf(stdout, "FALSE:         \r\t\t %d\n", usr->result_false);
		fprintf(stdout, "ERROR:         \r\t\t %d\n", usr->result_error);
		fprintf(stdout, "UNKNOWN:       \r\t\t %d\n", usr->result_unknown);
		fprintf(stdout, "NOT EVALUATED: \r\t\t %d\n", usr->result_neval);
		fprintf(stdout, "NOT APPLICABLE:\r\t\t %d\n", usr->result_napp);
	}

	/* export results to file */
	if (action->f_results != NULL) {
		/* get result model */
		struct oval_results_model *res_model = oval_agent_get_results_model(sess);

		/* set up directives */
		struct oval_result_directives *res_direct = oval_result_directives_new(res_model);
		oval_result_directives_set_reported(res_direct, OVAL_RESULT_TRUE | OVAL_RESULT_FALSE |
						    OVAL_RESULT_UNKNOWN | OVAL_RESULT_NOT_EVALUATED |
						    OVAL_RESULT_ERROR | OVAL_RESULT_NOT_APPLICABLE, true);

		oval_result_directives_set_content(res_direct,
						   OVAL_RESULT_TRUE |
						   OVAL_RESULT_FALSE |
						   OVAL_RESULT_UNKNOWN |
						   OVAL_RESULT_NOT_EVALUATED |
						   OVAL_RESULT_NOT_APPLICABLE |
						   OVAL_RESULT_ERROR,
						   OVAL_DIRECTIVE_CONTENT_FULL);

		
		/* export result model to XML */
		oval_results_model_export(res_model, res_direct, action->f_results);
		oval_result_directives_free(res_direct);

	        if (action->f_report != NULL)
        		oval_gen_report(action->f_results, action->f_report);
	}
	/* clean up */
	oval_agent_destroy_session(sess);
	oval_definition_model_free(def_model);

	/* return code */
	if (action->id) {
		if ((ret !=  OVAL_RESULT_FALSE) && (ret != OVAL_RESULT_UNKNOWN)) {
			return OSCAP_OK;
		} else {
			return OSCAP_FAIL;
		}
	}
	else {
		if ((usr->result_false == 0) && (usr->result_unknown == 0)) {
			free(usr);
			return OSCAP_OK;
		} else {
			free(usr);
			return OSCAP_FAIL;
		}
	}
}

static int app_analyse_oval(const struct oscap_action *action) {
	struct oval_definition_model *def_model;
	struct oval_syschar_model *sys_model;
	struct oval_results_model *res_model;
	struct oval_syschar_model *sys_models[2];

	def_model = oval_definition_model_import(action->f_oval);
        if (def_model == NULL) {
                fprintf(stderr, "Failed to import the definition model (%s).\n", action->f_oval);
                return OSCAP_ERROR;
        }

	sys_model = oval_syschar_model_new(def_model);
        if (oval_syschar_model_import(sys_model, action->f_syschar) ==  -1 ) {
                fprintf(stderr, "Failed to import the system characteristics model (%s).\n", action->f_syschar);
		if(oscap_err())
			fprintf(stderr, "ERROR: %s\n", oscap_err_desc());
		oval_definition_model_free(def_model);
                return OSCAP_ERROR;
        }

	sys_models[0] = sys_model;
	sys_models[1] = NULL;
	res_model = oval_results_model_new(def_model, sys_models);
	oval_results_model_eval(res_model);

	/* export results to file */
	if (action->f_results != NULL) {
		/* set up directives */
		struct oval_result_directives *res_direct = oval_result_directives_new(res_model);
		oval_result_directives_set_reported(res_direct, OVAL_RESULT_TRUE | OVAL_RESULT_FALSE |
						    OVAL_RESULT_UNKNOWN | OVAL_RESULT_NOT_EVALUATED |
						    OVAL_RESULT_ERROR | OVAL_RESULT_NOT_APPLICABLE, true);

		oval_result_directives_set_content(res_direct,
						   OVAL_RESULT_TRUE |
						   OVAL_RESULT_FALSE |
						   OVAL_RESULT_UNKNOWN |
						   OVAL_RESULT_NOT_EVALUATED |
						   OVAL_RESULT_NOT_APPLICABLE |
						   OVAL_RESULT_ERROR,
						   OVAL_DIRECTIVE_CONTENT_FULL);

		
		/* export result model to XML */
		oval_results_model_export(res_model, res_direct, action->f_results);
		oval_result_directives_free(res_direct);
	}
	return OSCAP_OK;
}

static int app_oval_xslt(const struct oscap_action *action)
{
    assert(action->module->user);
    return app_xslt(action->f_oval, action->module->user, action->f_results, NULL);
}


enum oval_opt {
    OVAL_OPT_RESULT_FILE = 1,
    OVAL_OPT_REPORT_FILE,
    OVAL_OPT_FILE_VERSION,
    OVAL_OPT_ID,
    OVAL_OPT_OUTPUT = 'o'
};

bool getopt_oval(int argc, char **argv, struct oscap_action *action)
{
	VERBOSE = action->verbosity;

	action->doctype = OSCAP_DOCUMENT_OVAL_DEFINITIONS;

	/* Command-options */
	struct option long_options[] = {
        // options
		{ "result-file",  required_argument, NULL, OVAL_OPT_RESULT_FILE  },
		{ "report-file",  required_argument, NULL, OVAL_OPT_REPORT_FILE  },
		{ "id",           required_argument, NULL, OVAL_OPT_ID           },
		{ "version",      required_argument, NULL, OVAL_OPT_FILE_VERSION },
		{ "output",       required_argument, NULL, OVAL_OPT_OUTPUT       },
        // flags
		{ "definitions",  no_argument, &action->doctype, OSCAP_DOCUMENT_OVAL_DEFINITIONS },
		{ "syschar",      no_argument, &action->doctype, OSCAP_DOCUMENT_OVAL_SYSCHAR     },
		{ "results",      no_argument, &action->doctype, OSCAP_DOCUMENT_OVAL_RESULTS     },
		{ "skip-valid",   no_argument, &action->validate, 0 },
        // end
		{ 0, 0, 0, 0 }
	};

	int c;
	while ((c = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
		switch (c) {
		case OVAL_OPT_RESULT_FILE: action->f_results = optarg; break;
		case OVAL_OPT_REPORT_FILE: action->f_report  = optarg; break;
		case OVAL_OPT_OUTPUT: action->f_results = optarg; break;
		case OVAL_OPT_ID: action->id = optarg; break;
		case OVAL_OPT_FILE_VERSION: action->file_version = optarg; break;
        	case 0: break;
		default: return oscap_module_usage(action->module, stderr, NULL);
		}
	}

	/* We should have Definitions file here */
	if (optind >= argc)
        	return oscap_module_usage(action->module, stderr, "Definitions file needs to be specified!");
	action->url_oval = argv[optind];

	/* We should have System Characteristics file here */
	if (action->module == &OVAL_ANALYSE) {
		if ((optind+1) > argc)
			return oscap_module_usage(action->module, stderr, "System characteristics file needs to be specified!");
		action->url_syschar = argv[optind + 1];
	}

	return true;
}

