/*
* Copyright (C) 2016-2019 "IoT.bzh"
* Author Fulup Ar Foll <fulup@iot.bzh>
* Author Fulup Ar Foll <romain@iot.bzh>
* Author Fulup Ar Foll <sebastien@iot.bzh>
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

#include "redis-replication-binding.h"

#include <ctl-config.h>
#include <filescan-utils.h>

#ifndef MB_DEFAULT_POLLING_FEQ
#define MB_DEFAULT_POLLING_FEQ 10
#endif

#define API_REPLY_SUCCESS "success"
#define API_REPLY_FAILURE "failed"

static int cloudConfig(afb_api_t api, CtlSectionT *section, json_object *rtusJ);

// Config Section definition (note: controls section index should match handle
// retrieval in HalConfigExec)
static CtlSectionT ctrlSections[] = {
    { .key = "onload", .loadCB = OnloadConfig },
    { .key = "redis-cloud", .loadCB = cloudConfig },
    { .key = NULL }
};

static void stopReplicationCb (afb_req_t request) {
    char * msg = "Stopping replication";

    AFB_API_NOTICE(request->api, "%s called", __func__);

    afb_req_success_f(request,json_object_new_string(msg), NULL);
    return;
}

static void startReplicationCb (afb_req_t request) {
    char response[233];
    int err;
    char *returnedError = NULL, *returnedInfo = NULL;
    json_object *responseJ = NULL;

#define REDIS_CLOUD_API "redis-from-cloud"
#define REDIS_CLOUD_VERB "ping"

    AFB_API_NOTICE(request->api, "%s: calling %s verb of API %s", __func__, REDIS_CLOUD_VERB, REDIS_CLOUD_API);
    err = afb_api_call_sync(request->api, REDIS_CLOUD_API, REDIS_CLOUD_VERB, NULL, &responseJ, &returnedError, &returnedInfo);
    if (err) {
        AFB_API_ERROR(request->api,
			      "Something went wrong during call to verb '%s' of api '%s' with error '%s' and info '%s'",
                  REDIS_CLOUD_VERB, REDIS_CLOUD_API,
                  returnedError ? returnedError : "not returned",
			      returnedInfo ? returnedInfo : "not returned");
        afb_req_fail_f(request,API_REPLY_FAILURE, "Replication failed");
        return;
    }
    snprintf (response, sizeof(response), "Replication started. Remote side replied: %s", json_object_to_json_string(responseJ));
    afb_req_success_f(request,json_object_new_string(response), NULL);

    return;
}

static void PingCb (afb_req_t request) {
    static int count=0;
    char response[32];
    json_object *queryJ =  afb_req_json(request);

    snprintf (response, sizeof(response), "Pong=%d", count++);
    AFB_API_NOTICE (request->api, "%s:ping count=%d query=%s", afb_api_name(request->api), count, json_object_get_string(queryJ));
    afb_req_success_f(request, json_object_new_string(response), NULL);

    return;
}

static void InfoCb (afb_req_t request) {
    AFB_API_NOTICE(request->api, "%s called. Not implemented !", __func__);
    afb_req_fail(request, API_REPLY_FAILURE, "Not implemented! Need to check Gwen's Markdown");
}

// Static verb not depending on main json config file
static afb_verb_t CtrlApiVerbs[] = {
    /* VERB'S NAME         FUNCTION TO CALL         SHORT DESCRIPTION */
    { .verb = "ping",     .callback = PingCb    , .info = "Cloud API ping test"},
    { .verb = "info",     .callback = InfoCb, .info = "Cloud API info"},
    { .verb = "start",     .callback = startReplicationCb     , .info = "Start DB replication"},
    { .verb = "stop",     .callback = stopReplicationCb     , .info = "Stop DB replication"},
    { .verb = NULL} /* marker for end of the array */
};

static int CtrlLoadStaticVerbs (afb_api_t api, afb_verb_t *verbs, void *vcbdata) {
    int errcount=0;

    for (int idx=0; verbs[idx].verb; idx++) {
        AFB_API_NOTICE(api, "Registering static verb '%s' info='%s'", CtrlApiVerbs[idx].verb, CtrlApiVerbs[idx].info);
        errcount+= afb_api_add_verb(api, CtrlApiVerbs[idx].verb, CtrlApiVerbs[idx].info, CtrlApiVerbs[idx].callback, vcbdata, 0, 0,0);
    }

    return errcount;
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
    // XXX: do not seal for now as we moved static verbs defs in main entry
    //afb_api_seal(api);

    return error;    
}

int afbBindingEntry(afb_api_t api) {
    int status = 0;
    int err = 0;
    char *searchPath, *envConfig;
    afb_api_t handle;

    // Use __func__ as the real name is mangled
    AFB_API_NOTICE(api, "Controller in %s", __func__);

    // register Code Encoders before plugin get loaded
    //mbRegisterCoreEncoders();

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

    // create one API per config file (Pre-V3 return code ToBeChanged)
    handle = afb_api_new_api(api, ctrlConfig->api, ctrlConfig->info, 1, CtrlLoadOneApi, ctrlConfig);
    if (!handle){
        AFB_API_ERROR(api, "afbBindingEntry failed to create API");
        status = ERROR;
        goto _exit_afbBindingEntry;
    }

    // add static controls verbs
    err = CtrlLoadStaticVerbs (handle, CtrlApiVerbs, (void*) NULL);
    if (err) {
        AFB_API_ERROR(api, "afbBindingEntry fail to register static API verbs");
        status = ERROR;
        goto _exit_afbBindingEntry;
    }


_exit_afbBindingEntry:
    free(searchPath);
    return status;
}
