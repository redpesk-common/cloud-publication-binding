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


// Le contexte de sensor loader au moment de l'API n'est retrouvé avec le request ****

#define _GNU_SOURCE


#include "modbus-binding.h"

#include <ctl-config.h>
#include <filescan-utils.h>

#ifndef MB_DEFAULT_POLLING_FEQ
#define MB_DEFAULT_POLLING_FEQ 10
#endif

static int ModbusConfig(afb_api_t api, CtlSectionT *section, json_object *rtusJ);
static int cloudConfig(afb_api_t api, CtlSectionT *section, json_object *rtusJ);

// Config Section definition (note: controls section index should match handle
// retrieval in HalConfigExec)
static CtlSectionT ctrlSections[] = {
    { .key = "onload", .loadCB = OnloadConfig },
    { .key = "modbus", .loadCB = ModbusConfig },
    { .key = "redis-cloud", .loadCB = cloudConfig },
    { .key = NULL }
};

static void stopReplication (afb_req_t request) {
    char * msg = "Stopping replication";

    AFB_API_NOTICE(request->api, "%s called", __func__);
    json_object *requestJ, *responseJ = NULL;

    afb_req_success_f(request,json_object_new_string(msg), NULL);
    return;
}

static void startReplication (afb_req_t request) {
    char response[233];
    json_object *queryJ =  afb_req_json(request);
    int err, status;
    char *returnedError = NULL, *returnedInfo = NULL;
    json_object *requestJ, *responseJ = NULL;

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
        status = ERROR;
        snprintf (response, sizeof(response), "Replication failed");
        afb_req_fail_f(request,json_object_new_string(response), NULL);
        return;
    }
    snprintf (response, sizeof(response), "Replication started. Remote side replied: %s", json_object_to_json_string(responseJ));
    afb_req_success_f(request,json_object_new_string(response), NULL);

    return;
}

static void PingTest (afb_req_t request) {
    static int count=0;
    char response[32];
    json_object *queryJ =  afb_req_json(request);

    snprintf (response, sizeof(response), "Pong=%d", count++);
    AFB_API_NOTICE (request->api, "%s:ping count=%d query=%s", afb_api_name(request->api), count, json_object_get_string(queryJ));
    afb_req_success_f(request,json_object_new_string(response), NULL);

    return;
}

static void InfoRtu (afb_req_t request) {
    json_object *elemJ;
    int err, idx;
    int verbose=0;
    int length =0;
    ModbusRtuT *rtus = (ModbusRtuT*) afb_req_get_vcbdata(request);
    json_object *queryJ =  afb_req_json(request);
    json_object *responseJ= json_object_new_array();

    err= wrap_json_unpack(queryJ, "{s?i s?i !}", "verbose", &verbose, "length", &length);
    if (err) {
        afb_req_fail_f (request, "ModbusRtuAdmin", "ListRtu: invalid 'json' query=%s", json_object_get_string(queryJ));
        goto OnErrorExit;
    }

    // loop on every defined RTU
    for (idx=0; rtus[idx].uid; idx++) {
        switch (verbose) {
            case 0:
                wrap_json_pack (&elemJ, "{ss ss}", "uid", rtus[idx].uid);
                break;
            case 1:
            default:
                wrap_json_pack (&elemJ, "{ss ss ss}", "uid", rtus[idx].uid, "uri",rtus[idx].uri, "info", rtus[idx].info);
                break;
            case 2:
                //err= ModbusRtuIsConnected (request->api, &rtus[idx]);
                err = -1;
                if (err <0) {
                    wrap_json_pack (&elemJ, "{ss ss ss}", "uid", rtus[idx].uid, "uri",rtus[idx].uri, "info", rtus[idx].info);;
                } else {
                    wrap_json_pack (&elemJ, "{ss ss ss sb}", "uid", rtus[idx].uid, "uri",rtus[idx].uri, "info", rtus[idx].info, "connected", err);;
                }
                break;
        }

        json_object_array_add(responseJ, elemJ);
    }
   
    afb_req_success(request, responseJ, NULL);
    return;

OnErrorExit:
    return;
}

// Static verb not depending on Modbus json config file
static afb_verb_t CtrlApiVerbs[] = {
    /* VERB'S NAME         FUNCTION TO CALL         SHORT DESCRIPTION */
    { .verb = "ping",     .callback = PingTest    , .info = "Cloud API ping test"},
    { .verb = "info",     .callback = InfoRtu     , .info = "Cloud API info"},
    { .verb = "start",     .callback = startReplication     , .info = "Start replication"},
    { .verb = "stop",     .callback = stopReplication     , .info = "Stop replication"},
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
static void RtuDynRequest(afb_req_t request) {

    // retrieve action handle from request and execute the request
    json_object *queryJ = afb_req_json(request);
    ModbusRtuT* rtu= (ModbusRtuT*) afb_req_get_vcbdata(request);
    ModbusRtuRequest (request, rtu, queryJ);
}

static void SensorDynRequest(afb_req_t request) {

    // retrieve action handle from request and execute the request
    json_object *queryJ = afb_req_json(request);
    ModbusSensorT* sensor = (ModbusSensorT*) afb_req_get_vcbdata(request);
    ModbusSensorRequest (request, sensor, queryJ);
}

static int SensorLoadOne(afb_api_t api, ModbusRtuT *rtu, ModbusSensorT *sensor, json_object *sensorJ) {
    int err = 0;
    const char *type=NULL;
    const char *format=NULL;
    const char *privilege=NULL;
    afb_auth_t *authent=NULL;
    json_object *argsJ=NULL;
    char* apiverb;
    ModbusSourceT source;

    // should already be allocated
    assert (sensorJ);

    // set default values
    memset(sensor, 0, sizeof (ModbusSensorT));
    sensor->rtu   = rtu;
    sensor->hertz = rtu->hertz;
    sensor->iddle = rtu->iddle;
    sensor->count = 1;

    err = wrap_json_unpack(sensorJ, "{ss,ss,si,s?s,s?s,s?s,s?i,s?i,s?i,s?o !}",
                "uid", &sensor->uid,
                "type", &type,
                "register", &sensor->registry,
                "info", &sensor->info,
                "privilege", &privilege,
                "format", &format,
                "hertz", &sensor->hertz,
                "iddle", &sensor->iddle,
                "count", &sensor->count,
                "args", &argsJ);
    if (err) {
        AFB_API_ERROR(api, "SensorLoadOne: Fail to parse sensor: %s", json_object_to_json_string(sensorJ));
        goto OnErrorExit;
    }

    // find modbus register type/function callback
    sensor->function = mbFunctionFind (api, type);
    if (!sensor->function) goto FunctionErrorExit;

    // find encode/decode callback
    sensor->format = mbEncoderFind (api, format);
    if (!sensor->format) goto TypeErrorExit;

    // Fulup should insert global auth here
    sensor->api = api;
    if (privilege) {
       authent= (afb_auth_t*) calloc(1, sizeof (afb_auth_t));
       authent->type = afb_auth_Permission;
       authent->text = privilege;
    }

    // if defined call format init callback
    if (sensor->format->initCB) {
        source.sensor = sensor->uid;
        source.api = api;
        source.context=NULL;
        err = sensor->format->initCB (&source, argsJ);
        if (err) {
            AFB_API_ERROR(api, "SensorLoadOne: fail to init format verb=%s", apiverb);
            goto OnErrorExit;
        }
        // remember context for further encode/decode callback 
        sensor->context = source.context;
    }

    err=asprintf (&apiverb, "%s/%s", rtu->prefix, sensor->uid);
    err = afb_api_add_verb(api, (const char*) apiverb, sensor->info, SensorDynRequest, sensor, authent, 0, 0);
    if (err) {
        AFB_API_ERROR(api, "SensorLoadOne: fail to register API verb=%s", apiverb);
        goto OnErrorExit;
    }

    return 0;

TypeErrorExit:
    AFB_API_ERROR(api, "SensorLoadOne: missing or invalid Modus Type code JSON=%s", json_object_to_json_string(sensorJ));
    return -1;
FunctionErrorExit:
    AFB_API_ERROR(api, "SensorLoadOne: missing or invalid Modus Type=%s JSON=%s", type, json_object_to_json_string(sensorJ));
    return -1;
OnErrorExit:
    return -1;    
}

static int ModbusLoadOne(afb_api_t api, ModbusRtuT *rtu, json_object *rtuJ) {
    int err = 0;
    json_object *sensorsJ;
    afb_auth_t *authent=NULL;
    char *adminCmd;

    // should already be allocated
    assert (rtuJ); 
    assert (api);

    memset(rtu, 0, sizeof (ModbusRtuT)); // default is empty
    err = wrap_json_unpack(rtuJ, "{ss,s?s,s?s,s?s,s?i,s?s,s?i,s?i,s?i,s?i,s?i,s?i,so}",
            "uid", &rtu->uid,
            "info", &rtu->info,
            "uri", &rtu->uri,
            "privileges", &rtu->privileges,
            "autostart", &rtu->autostart,
            "prefix", &rtu->prefix,
            "slaveid", &rtu->slaveid,
            "debug", &rtu->debug,
            "timeout", &rtu->timeout,
            "idlen", &rtu->idlen,
            "hertz", &rtu->hertz,
            "iddle", &rtu->iddle,
            "sensors", &sensorsJ);
    if (err) {
        AFB_API_ERROR(api, "Fail to parse rtu JSON : (%s)", json_object_to_json_string(rtuJ));
        goto OnErrorExit;
    }  

    // create an admin command for RTU
    if (rtu->privileges) {
       authent= (afb_auth_t*) calloc(1, sizeof (afb_auth_t));
       authent->type = afb_auth_Permission;
       authent->text = rtu->privileges;
    }

    // if not API prefix let's use RTU uid
    if (!rtu->prefix) rtu->prefix= rtu->uid;

    // set default pooling frequency
    if (!rtu->hertz) rtu->hertz=MB_DEFAULT_POLLING_FEQ;

    err=asprintf (&adminCmd, "%s/%s", rtu->prefix, "admin");
    err= afb_api_add_verb(api, adminCmd, rtu->info, RtuDynRequest, rtu, authent, 0, 0);
    if (err) {
        AFB_API_ERROR(api, "ModbusLoadOne: fail to register API uid=%s verb=%s info=%s", rtu->uid, adminCmd, rtu->info);
        goto OnErrorExit;
    }

    // if uri is provided let's try to connect now
    if (rtu->uri && rtu->autostart) {
        err = ModbusRtuConnect (api, rtu);
        if (err) {
            AFB_API_ERROR(api, "ModbusLoadOne: fail to connect TCP/RTU uid=%s uri=%s", rtu->uid, rtu->uid);
            if (rtu->autostart > 1) goto OnErrorExit;
        }
    }

    // loop on sensors
    if (json_object_is_type(sensorsJ, json_type_array)) {
        int count = (int)json_object_array_length(sensorsJ);
        rtu->sensors= (ModbusSensorT*)calloc(count + 1, sizeof (ModbusSensorT));

        for (int idx = 0; idx < count; idx++) {
            json_object *sensorJ = json_object_array_get_idx(sensorsJ, idx);
            err = SensorLoadOne(api, rtu, &rtu->sensors[idx], sensorJ);
            if (err) goto OnErrorExit;
        }

    } else {
        rtu->sensors= (ModbusSensorT*) calloc(2, sizeof(ModbusSensorT));
        err= SensorLoadOne(api, rtu, &rtu->sensors[0], sensorsJ);
        if (err) goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;    
}

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

static int ModbusConfig(afb_api_t api, CtlSectionT *section, json_object *rtusJ) {
    ModbusRtuT *rtus;
    int err;

    return 0;

    // everything is done during initial config call
    if (!rtusJ) return 0;

    // modbus array is close with a nullvalue;
    if (json_object_is_type(rtusJ, json_type_array)) {
        int count = (int)json_object_array_length(rtusJ);
        rtus =  (ModbusRtuT*) calloc(count + 1, sizeof (ModbusRtuT));

        for (int idx = 0; idx < count; idx++) {
            json_object *rtuJ = json_object_array_get_idx(rtusJ, idx);
            err = ModbusLoadOne(api, &rtus[idx], rtuJ);
            if (err) goto OnErrorExit;
        }

    } else {
        rtus = (ModbusRtuT*)calloc(2, sizeof (ModbusRtuT));
        err = ModbusLoadOne(api, &rtus[0], rtusJ);
        if (err) goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    AFB_API_ERROR (api, "Fail to initialise Modbus check Json Config");
    return -1;    
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

    AFB_API_NOTICE(api, "Controller in afbBindingEntry");

    // register Code Encoders before plugin get loaded
    //mbRegisterCoreEncoders();

    envConfig= getenv("CONTROL_CONFIG_PATH");
    if (!envConfig) envConfig = CONTROL_CONFIG_PATH;

    status=asprintf (&searchPath,"%s:%s/etc", envConfig, GetBindingDirPath(api));
    AFB_API_NOTICE(api, "Json config directory : %s", searchPath);

    const char* prefix = "control";
    const char* configPath = CtlConfigSearch(api, searchPath, prefix);
    if (!configPath) {
        AFB_API_ERROR(api, "afbBindingEntry: No %s-%s* config found in %s ", prefix, GetBinderName(), searchPath);
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
