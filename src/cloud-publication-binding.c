/*
* Copyright (C) 2020 "IoT.bzh"
* Author Vincent Rubiolo <vincent.rubiolo@iot.bzh>
* based on original work from Fulup Ar Foll <fulup@iot.bzh>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#define _GNU_SOURCE

#include "cloud-publication-binding.h"

#include <ctl-config.h>
#include <afb/afb-binding.h>
#include <afb-timer.h>

#define CP_TIMER_RUN_FOREVER -1
#define CP_TIMER_MAIN_DELAY 100

#define PING_VERB_RESPONSE_SIZE 33

#define REDIS_CLOUD_API "redis-cloud"
#define REDIS_CLOUD_VERB_PING "ping"

#define REDIS_LOCAL_API "redis"
#define REDIS_LOCAL_VERB_TS_MRANGE "ts_mrange"
#define REDIS_LOCAL_VERB_TS_MAGGREGATE "ts_maggregate"
#define REDIS_LOCAL_VERB_TS_MINSERT "ts_minsert"

#define API_REPLY_SUCCESS "success"
#define API_REPLY_FAILURE "failed"

#define TIMER_RETRY_MAX_DELAY 10000

#define SENSOR_CLASS "WIRED_WIND_WS310"
#define SENSOR_CLASS_ID SENSOR_CLASS "_ID"

struct publication_state
{
    bool in_progress;
    int retry_count;
    afb_api_t api;
    json_object *obj;
};

struct publication_state current_state = {
    .in_progress = false,
    .retry_count = 0,
    .api = 0,
    .obj = 0
};

int retryDelays[] = {1000, 2000, 2000, TIMER_RETRY_MAX_DELAY};
int retryDelaysSz = (sizeof(retryDelays)/sizeof(retryDelays[0]));

static int cloud_config(afb_api_t api, CtlSectionT *section, json_object *rtusJ);
static int call_verb_sync (afb_api_t api, const char * apiToCall, const char * verbToCall,
                         json_object * argsJ, int * disconnected);
static void call_verb_async (afb_api_t api, const char * apiToCall, const char * verbToCall,
                          json_object * argsJ, void
                          (*callback)( void *closure, struct json_object
                          *object, const char *error, const char * info,
                          afb_api_t api), void *closure);
static void publish_job(int signum, void *arg);
static void repush_job(int signum, void *arg);

// Config Section definition (note: controls section index should match handle
// retrieval in HalConfigExec)
static CtlSectionT ctrlSections[] = {
    { .key = "onload", .loadCB = OnloadConfig },
    { .key = "redis-cloud", .loadCB = cloud_config },
    { .key = NULL }
};

static void stop_publication() {
    if (current_state.in_progress) {
        current_state.in_progress = false;
	json_object_put(current_state.obj);
	current_state.obj = NULL;
    }
}

static void stop_publication_cb (afb_req_t request) {

    AFB_REQ_DEBUG(request, "%s called", __func__);

    if (!current_state.in_progress) {
        AFB_REQ_ERROR(request, "replication has not been started yet!");
        afb_req_success_f(request, NULL, "Already stopped");
        return;
    }
    stop_publication();
    afb_req_success_f(request, NULL, "Replication stopped");
    return;
}

void push_data_reply_cb(void *closure, struct json_object *mResultJ,
                    const char *error, const char * info, afb_api_t api) {

    int err;
    int delay;
    void (*job)(int,void*);

    // nothing if stopped
    if (!current_state.in_progress) {
        return;
    }

    // check status
    if (error == NULL) {
        // we are connected: this could be normal execution flow or a reconnection
        // In any case, we restart publication.
        json_object_put(current_state.obj);
        current_state.obj = NULL;
        job = publish_job;
        delay = CP_TIMER_MAIN_DELAY;
        current_state.retry_count = 0;
    }
    else if (strcmp(error, "disconnected") == 0) {
        // the cloud side is disconnected: stop the current timer and set the
        // next job to be a retry, potentially with an updated delay if there was
        // already a previous disconnection
        job = repush_job;
        delay = retryDelays[current_state.retry_count];
        current_state.retry_count += current_state.retry_count < 
                        ((sizeof retryDelays / sizeof *retryDelays) - 1);

        AFB_API_NOTICE(current_state.api, "cloud side disconnected, retrying in %d seconds", delay / 1000);
    }
    else {
        // the error is of an other unexpected kind
        AFB_API_ERROR(current_state.api, "failure to call ts_minsert() to publish data!");
        stop_publication();
        return;
    }

    // queue publication job
    err = afb_api_queue_job(current_state.api, job, 0, 0, -delay);
    if (err < 0) {
        AFB_API_ERROR(current_state.api, "failure to queue publication job!");
        stop_publication();
    }
}

void push_data() {
    // nothing if stopped
    if (!current_state.in_progress) {
        return;
    }

    afb_api_call(current_state.api, REDIS_CLOUD_API, REDIS_LOCAL_VERB_TS_MINSERT,
                         json_object_get(current_state.obj), push_data_reply_cb, 0);
}

void ts_mrange_call_cb(void *closure, struct json_object *mRangeResultJ, const char *error, 
                    const char * info, afb_api_t api) {

    AFB_API_DEBUG(api, "%s: called, retry count: %d, in-progress %d", __func__, 
                            current_state.retry_count, (int)current_state.in_progress);

    // check errors
    if (error){
        AFB_API_ERROR(api, "failure to retrieve database records via ts_mrange(): %s [%s]!",
                      error, info == NULL ? "[no info]": info);
        stop_publication();
        return;
    }

    // nothing if stopped
    if (!current_state.in_progress) {
        return;
    }

    //AFB_API_DEBUG(api, "ts_mrange() returned %s", json_object_get_string(mRangeResultJ));

    current_state.obj = json_object_get(mRangeResultJ);
    push_data();
}

static void repush_job(int signum, void *arg) {
    static int callCnt = 0;
    if (signum) {
        AFB_API_ERROR(current_state.api, "signal %s caught in repush job", strsignal(signum));
        stop_publication();
    }
    else {
        AFB_API_DEBUG(current_state.api, "repush_job iter %d", ++callCnt);
        push_data();
    }
}

static void publish_job(int signum, void *arg) {
    int err;
    static int callCnt = 0;
    json_object * mrangeArgsJ;

    if (signum) {
        AFB_API_ERROR(current_state.api, "signal %s caught in publication job", strsignal(signum));
        stop_publication();
    }
    else {
        AFB_API_DEBUG(current_state.api, "publish_job iter %d", ++callCnt);
        err = wrap_json_pack (&mrangeArgsJ, "{ s:s, s:s, s:s }", "class", SENSOR_CLASS, "fromts", "-", "tots", "+");
        if (!err) {
            call_verb_async (current_state.api, REDIS_LOCAL_API,
                             REDIS_LOCAL_VERB_TS_MRANGE, mrangeArgsJ, ts_mrange_call_cb, NULL);
        } else {
            AFB_API_ERROR(current_state.api, "ts_mrange() argument packing failed!");
            stop_publication();
            return;
        }
    }
}

static void start_publication_cb (afb_req_t request) {
    json_object * aggregArgsJ;
    afb_api_t api = afb_req_get_api(request);
    int err;
    int disconnected = 0;

    assert (api);

    // check state
    if (current_state.in_progress) {
        afb_req_success_f(request, NULL, "already started");
        return;
    }
    current_state.api = api;
    current_state.in_progress = true;
    current_state.retry_count = 0;

    err = wrap_json_pack (&aggregArgsJ, "{s:s, s:s, s: {s:s, s:i}}", "name", SENSOR_CLASS_ID, "class", SENSOR_CLASS,
                             "aggregation", "type", "avg", "bucket", 500);
    if (err){
        current_state.in_progress = false;
        afb_req_fail_f(request,API_REPLY_FAILURE, "aggregation argument packing failed!");
        return;
    }

    // Request resampling being done for all future records
    err = call_verb_sync (api, REDIS_LOCAL_API, REDIS_LOCAL_VERB_TS_MAGGREGATE, aggregArgsJ, &disconnected);
    if (err) {
        current_state.in_progress = false;
        afb_req_fail_f(request,API_REPLY_FAILURE, "redis resampling request failed!");
        return;
    }

    err = afb_api_queue_job(api, publish_job, 0, 0, -CP_TIMER_MAIN_DELAY);
    if (err < 0) {
        current_state.in_progress = false;
        afb_req_fail_f(request,API_REPLY_FAILURE, "redis resampling request failed!");
        return;
    }

    afb_req_success_f(request, NULL, "replication started successfully");
    return;
}

static int call_verb_sync (afb_api_t api, const char * apiToCall, const char * verbToCall,
                      json_object * argsJ, int * disconnected) {
    int err;
    char *returnedError = NULL, *returnedInfo = NULL;
    json_object *responseJ = NULL;

    AFB_API_DEBUG(api, "%s: %s/%s sync call with args %s", __func__, apiToCall, verbToCall,
                  json_object_to_json_string(argsJ));

    err = afb_api_call_sync(api, apiToCall, verbToCall, argsJ, &responseJ, &returnedError, &returnedInfo);

    if (err) {
        AFB_API_ERROR(api,
			      "error during call to verb '%s' of api '%s' with error '%s' and info '%s'",
                  returnedError ? returnedError : "none",
                  returnedInfo ? returnedInfo : "none",
                  verbToCall, apiToCall);
        free(returnedError);
        free(returnedInfo);
        return -1;
    } 

    // no protocol error but a higher level one
    if (returnedError) { 
        AFB_API_DEBUG(api, "%s: %s/%s sync call returned OK but error detected: %s", __func__, apiToCall,
                    verbToCall, returnedError);
        if (strcmp (returnedError, "disconnected") == 0) {
            *disconnected = 1;
        }
        free (returnedError);
    }
    AFB_API_DEBUG(api, "%s: %s/%s sync call performed. Remote side replied: %s", __func__, apiToCall, verbToCall,
                  json_object_to_json_string(responseJ));

    return 0;
}

static void call_verb_async (afb_api_t api, const char * apiToCall, const char * verbToCall,
                          json_object * argsJ,
                          void (*callback)(
                            void *closure,
                            struct json_object *object,
                            const char *error,
                            const char * info,
                            afb_api_t api),
			              void *closure) {

    AFB_API_DEBUG(api, "%s: %s/%s async call with args %s", __func__, apiToCall, verbToCall,
                  json_object_to_json_string(argsJ));

    afb_api_call(api, apiToCall, verbToCall, argsJ, callback, closure);

    AFB_API_DEBUG(api, "%s: %s/%s async call performed", __func__, apiToCall, verbToCall);
}

static void ping_cb (afb_req_t request) {
    static int count=0;
    char response[PING_VERB_RESPONSE_SIZE];
    json_object *queryJ =  afb_req_json(request);

    snprintf (response, sizeof(response), "Pong=%d", count++);
    AFB_API_NOTICE (request->api, "%s:ping count=%d query=%s", afb_api_name(request->api), count, json_object_get_string(queryJ));
    afb_req_success_f(request, json_object_new_string(response), NULL);

    return;
}

static void info_cb (afb_req_t request) {
    json_object * infoArgsJ;
	enum json_tokener_error jerr;

	infoArgsJ = json_tokener_parse_verbose(infoVerb, &jerr);
	if (infoArgsJ == NULL || jerr != json_tokener_success) {
        afb_req_fail_f(request,API_REPLY_FAILURE, "failure while packing info() verb arguments (error: %d)!", jerr);
        return;
    }
    afb_req_success_f(request, infoArgsJ, NULL);
}

// Static verb not depending on main json config file
static afb_verb_t CtrlApiVerbs[] = {
    /* VERB'S NAME         FUNCTION TO CALL         SHORT DESCRIPTION */
    { .verb = "ping",     .callback = ping_cb    , .info = "Cloud publication ping test"},
    { .verb = "info",     .callback = info_cb, .info = "Cloud publication info request"},
    { .verb = "start",     .callback = start_publication_cb     , .info = "Start cloud publication"},
    { .verb = "stop",     .callback = stop_publication_cb     , .info = "Stop cloud publication"},
    { .verb = NULL} /* marker for end of the array */
};

static int cloud_config(afb_api_t api, CtlSectionT *section, json_object *rtusJ) {

    static int callCnt = 0;

    if (callCnt == 0) {
        AFB_API_NOTICE (api, "%s: init time", __func__);
    } else if (callCnt == 1) {
        AFB_API_NOTICE (api, "%s: exec time", __func__);
    }
    callCnt++;
    return 0;

}

static int CtrlInitOneApi(afb_api_t api) {
    int err = 0;

    // retrieve section config from api handle
    CtlConfigT* ctrlConfig = (CtlConfigT*)afb_api_get_userdata(api);

    err = CtlConfigExec(api, ctrlConfig);
    if (err) {
        AFB_API_ERROR(api, "Error at CtlConfigExec step");
        return err;
    }

    // make userdata available to API users
    afb_api_set_userdata (api, NULL);
    return err;
}

static int CtrlLoadOneApi(void* vcbdata, afb_api_t api) {
    CtlConfigT* ctrlConfig = (CtlConfigT*)vcbdata;

    // save closure as api's data context
    // note: this is mandatory for the controller to work
    afb_api_set_userdata(api, ctrlConfig);

    // load section for corresponding API
    int error = CtlLoadSections(api, ctrlConfig, ctrlSections);

    // init and seal API function
    afb_api_on_init(api, CtrlInitOneApi);

    return error;    
}

int afbBindingEntry(afb_api_t api) {
    int status = 0;
    int err = 0; char *searchPath, *envConfig; afb_api_t handle;
   
    // Use __func__ as the real name is mangled
    AFB_API_NOTICE(api, "Controller in %s", __func__);

    envConfig= getenv("CONTROL_CONFIG_PATH");
    if (!envConfig) envConfig = CONTROL_CONFIG_PATH;

    status=asprintf (&searchPath,"%s:%s/etc", envConfig, GetBindingDirPath(api));
    AFB_API_NOTICE(api, "Json config directory : %s", searchPath);

    const char* configPath = CtlConfigSearch(api, searchPath, NULL);
    if (!configPath) {
        AFB_API_ERROR(api, "afbBindingEntry: No %s* config found in %s ", GetBinderName(), searchPath);
        status = ERROR;
        goto _exit_afbBindingEntry;
    }

    // load config file and create API
    CtlConfigT* ctrlConfig = CtlLoadMetaData(api, configPath);
    if (!ctrlConfig) {
        AFB_API_ERROR(api, "afbBindingEntry No valid control config file in:\n-- %s", configPath);
        status = ERROR;
        goto _exit_afbBindingEntry;
    }

    AFB_API_NOTICE(api, "Controller API='%s' info='%s'", ctrlConfig->api, ctrlConfig->info);

    handle = afb_api_new_api(api, ctrlConfig->api, ctrlConfig->info, 0, CtrlLoadOneApi, ctrlConfig);
    if (!handle){
        AFB_API_ERROR(api, "afbBindingEntry failed to create API");
        status = ERROR;
        goto _exit_afbBindingEntry;
    }

    // add static controls verbs
    err = afb_api_set_verbs_v3(handle, CtrlApiVerbs);
    if (err < 0) {
        AFB_API_ERROR(api, "afbBindingEntry fail to register static API verbs");
        status = ERROR;
        goto _exit_afbBindingEntry;
    }

_exit_afbBindingEntry:
    free(searchPath);
    return status;
}
