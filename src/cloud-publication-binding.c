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

#define TIMER_GET_TYPE(timer) (((cpTimer *)timer->context)->type)

#define SENSOR_CLASS "WIRED_WIND_WS310"
#define SENSOR_CLASS_ID SENSOR_CLASS "_ID"

typedef enum {
    CP_TIMER_MAIN,
    CP_TIMER_RETRY
} cpTimerType;

typedef struct {
    cpTimerType type;
    uint32_t retryCount;
} cpTimer;

int retryDelays[] = {1000, 2000, 2000, TIMER_RETRY_MAX_DELAY};
int retryDelaysSz = (sizeof(retryDelays)/sizeof(retryDelays[0]));

static int cloudConfig(afb_api_t api, CtlSectionT *section, json_object *rtusJ);
static int callVerbSync (afb_api_t api, const char * apiToCall, const char * verbToCall,
                         json_object * argsJ, int * disconnected);
static void callVerbAsync (afb_api_t api, const char * apiToCall, const char * verbToCall,
                          json_object * argsJ, void
                          (*callback)( void *closure, struct json_object
                          *object, const char *error, const char * info,
                          afb_api_t api), void *closure);
static int redisReplTimerCb(TimerHandleT *timer);
static TimerHandleT * createCpTimer (cpTimerType type, int retryCnt, afb_api_t api);
static int getTimerDelay (cpTimerType type, int currentCnt);
static char * timerTypeToStr (cpTimerType type);

// Config Section definition (note: controls section index should match handle
// retrieval in HalConfigExec)
static CtlSectionT ctrlSections[] = {
    { .key = "onload", .loadCB = OnloadConfig },
    { .key = "redis-cloud", .loadCB = cloudConfig },
    { .key = NULL }
};


static TimerHandleT * createCpTimer (cpTimerType type, int retryCnt, afb_api_t api) 
    {
    TimerHandleT *timer;

    timer = calloc(1, sizeof (TimerHandleT) + sizeof(cpTimer));

    if (type == CP_TIMER_RETRY) {
        timer->count = 1;
        timer->uid = "Cloud publication retry timer";
    } else if (type == CP_TIMER_MAIN) {
        timer->count = CP_TIMER_RUN_FOREVER;
        timer->uid = "Cloud publication main timer";
    } else {
        assert (1);
    }
    timer->delay = getTimerDelay(type, retryCnt);
    timer->context = timer + 1; // we store the cpTimer just after
    timer->evtSource = NULL; // should always be NULL as per the docs
    timer->api = api;

    timer->callback = NULL;
    timer->freeCB = NULL;

    ((cpTimer *) timer->context)->type = type;
    ((cpTimer *) timer->context)->retryCount = retryCnt;

    AFB_API_DEBUG(api, "Created timer w/ delay %d and type %s", timer->delay,
                  timerTypeToStr (type));
    return timer;
    }

static char * timerTypeToStr (cpTimerType type) {
    assert (type == CP_TIMER_RETRY || type == CP_TIMER_MAIN);
    return type == CP_TIMER_RETRY ? "retry" : "main";
}

static int getTimerDelay (cpTimerType type, int currentCnt) {
    assert (type == CP_TIMER_RETRY || type == CP_TIMER_MAIN);

    // retry timer counts always start at 1
    if (type == CP_TIMER_RETRY) {
        if (currentCnt-1 >= retryDelaysSz) {
            return TIMER_RETRY_MAX_DELAY;
        } else {
            return retryDelays[currentCnt-1];
        }
    } else {
        return CP_TIMER_MAIN_DELAY;
    }
}

static void stopPublicationCb (afb_req_t request) {
    afb_api_t api = afb_req_get_api(request);
    TimerHandleT * timerHandle= afb_api_get_userdata(api);

    AFB_API_DEBUG(request->api, "%s called", __func__);

    if (timerHandle == NULL) {
        AFB_API_ERROR(api, "replication has not been started yet!");
        return;
    }

    TimerEvtStop(timerHandle);
    afb_api_set_userdata (api, NULL);
    afb_req_success_f(request,json_object_new_string("Replication stopped"), NULL);
    return;
}

void tsMrangeCallCb(void *closure, struct json_object *mRangeResultJ, const char *error, 
                    const char * info, afb_api_t api) {
    int err;
    int disconnected = 0;
    TimerHandleT * timerHandle= afb_api_get_userdata(api);
    TimerHandleT * newTimer;
    cpTimerType currentTimerType;
    int currentTimerRetryCnt;

    AFB_API_DEBUG(api, "%s: called", __func__);

    currentTimerType = ((cpTimer *) timerHandle->context)->type;
    assert (currentTimerType == CP_TIMER_RETRY || currentTimerType == CP_TIMER_MAIN);

    currentTimerRetryCnt = ((cpTimer *) timerHandle->context)->retryCount;

    AFB_API_DEBUG(api, "%s: %s timer type, %d retry count", __func__, 
                 timerTypeToStr(currentTimerType), currentTimerRetryCnt);

    if (error){
        AFB_API_ERROR(api, "failure to retrieve database records via ts_mrange(): %s [%s]!",
                      error, info == NULL ? "[no info]": info);
        return;
    }

    //AFB_API_DEBUG(api, "ts_mrange() returned %s", json_object_get_string(mRangeResultJ));

    // json_objet_get() necessary to increment refcount of object
    err = callVerbSync (api, REDIS_CLOUD_API, REDIS_LOCAL_VERB_TS_MINSERT, json_object_get(mRangeResultJ),
                        &disconnected);
    if (err) {
        AFB_API_ERROR(api, "failure to call ts_minsert() to replicate data!");
        return;
    }

    if (disconnected) {
        // the cloud side is disconnected: stop the current timer and create a
        // new one, potentially with an updated delay if there was already a
        // previous disconnection
        TimerEvtStop(timerHandle); // this does not free the handle yet
        afb_api_set_userdata (api, NULL);

        newTimer = createCpTimer(CP_TIMER_RETRY, currentTimerRetryCnt+1, api);

        AFB_API_NOTICE(api, "cloud side disconnected, retrying in %d seconds", newTimer->delay/1000);
        TimerEvtStart(api, newTimer, redisReplTimerCb, newTimer->context);
        afb_api_set_userdata(newTimer->api, newTimer);
    } else {
        // we are connected: this could be normal execution flow or a reconnection
        // if this is a reconnection (we were using a retry timer), we switch back
        // to the main timer, otherwise we do nothing

        if (TIMER_GET_TYPE(timerHandle) == CP_TIMER_RETRY) {
            TimerEvtStop(timerHandle); // this does not free the handle yet
            afb_api_set_userdata (api, NULL);

            AFB_API_NOTICE(api, "cloud side now reconnected, resuming publication");
            newTimer = createCpTimer(CP_TIMER_MAIN, 0, api);

            TimerEvtStart(api, newTimer, redisReplTimerCb, newTimer->context);
            afb_api_set_userdata(newTimer->api, newTimer);
        }
    }
    }

static int redisReplTimerCb(TimerHandleT *timer) {
    int err;
    static int callCnt = 0;
    json_object * mrangeArgsJ;
    afb_api_t api = timer->api;

    callCnt++;
    AFB_API_DEBUG(api, "%s called %d times via %s timer (delay: %d)", __func__, callCnt,
                  timerTypeToStr(TIMER_GET_TYPE(timer)), timer->delay);

    err = wrap_json_pack (&mrangeArgsJ, "{ s:s, s:s, s:s }", "class", SENSOR_CLASS, "fromts", "-", "tots", "+");
    if (err){
        AFB_API_ERROR(api, "ts_mrange() argument packing failed!");
        return 0;
    }

    callVerbAsync (api, REDIS_LOCAL_API, REDIS_LOCAL_VERB_TS_MRANGE, mrangeArgsJ, tsMrangeCallCb, NULL);
    return 1;
}

static void startPublicationCb (afb_req_t request) {
    TimerHandleT *timerHandle;
    json_object * aggregArgsJ;
    json_object * aggregArgsParamsJ;
    afb_api_t api = afb_req_get_api(request);
    int err;
    int disconnected = 0;

    assert (api);

    err = wrap_json_pack (&aggregArgsParamsJ, "{s:s, s:i}", "type", "avg", "bucket", 500);
    if (err){
        afb_req_fail_f(request,API_REPLY_FAILURE, "aggregation parameters argument packing failed!");
        return;
    }

    err = wrap_json_pack (&aggregArgsJ, "{s:s, s:s, s: {s:s, s:i}}", "name", SENSOR_CLASS_ID, "class", SENSOR_CLASS,
                             "aggregation", "type", "avg", "bucket", 500);
    if (err){
        afb_req_fail_f(request,API_REPLY_FAILURE, "aggregation argument packing failed!");
        return;
    }

    // Request resampling being done for all future records
    err = callVerbSync (request->api, REDIS_LOCAL_API, REDIS_LOCAL_VERB_TS_MAGGREGATE, aggregArgsJ, &disconnected);
    if (err) {
        afb_req_fail_f(request,API_REPLY_FAILURE, "redis resampling request failed!");
        return;
    }

    timerHandle = createCpTimer(CP_TIMER_MAIN, 0, api);

    TimerEvtStart(api, timerHandle, redisReplTimerCb, timerHandle->context);
    afb_api_set_userdata(timerHandle->api, timerHandle);

    afb_req_success_f(request,json_object_new_string("replication started successfully"), NULL);
    return;
}

static int callVerbSync (afb_api_t api, const char * apiToCall, const char * verbToCall,
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

static void callVerbAsync (afb_api_t api, const char * apiToCall, const char * verbToCall,
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

static void PingCb (afb_req_t request) {
    static int count=0;
    char response[PING_VERB_RESPONSE_SIZE];
    json_object *queryJ =  afb_req_json(request);

    snprintf (response, sizeof(response), "Pong=%d", count++);
    AFB_API_NOTICE (request->api, "%s:ping count=%d query=%s", afb_api_name(request->api), count, json_object_get_string(queryJ));
    afb_req_success_f(request, json_object_new_string(response), NULL);

    return;
}

static void InfoCb (afb_req_t request) {
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
    { .verb = "ping",     .callback = PingCb    , .info = "Cloud publication ping test"},
    { .verb = "info",     .callback = InfoCb, .info = "Cloud publication info request"},
    { .verb = "start",     .callback = startPublicationCb     , .info = "Start cloud publication"},
    { .verb = "stop",     .callback = stopPublicationCb     , .info = "Stop cloud publication"},
    { .verb = NULL} /* marker for end of the array */
};

static int cloudConfig(afb_api_t api, CtlSectionT *section, json_object *rtusJ) {

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
