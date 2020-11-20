/*
 * Copyright (C) 2018 "IoT.bzh"
 * Author "Fulup Ar Foll" <fulup@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	 http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define MODBUS_GET_INT32_FROM_INT16(tab_int16, index) ((tab_int16[(index)] << 16) + tab_int16[(index) + 1])

#define _GNU_SOURCE

#include "modbus-binding.h"
#include <ctl-plugin.h>

CTLP_CAPI_REGISTER("king_pigeon");

typedef struct {
    uint32_t previous;
    uint32_t step;
} rCountT;

static int decodePigeonInfo (ModbusSourceT *source, ModbusFormatCbT *format, uint16_t *data, uint index, json_object **responseJ) {

    *responseJ = json_object_new_object();
    json_object_object_add (*responseJ, "product", json_object_new_int(data[0]));
    json_object_object_add (*responseJ, "lot", json_object_new_int(data[1]));
    json_object_object_add (*responseJ, "serial", json_object_new_int(data[2]));
    json_object_object_add (*responseJ, "online", json_object_new_int(data[3]));
    json_object_object_add (*responseJ, "hardware", json_object_new_int(data[4]));
    json_object_object_add (*responseJ, "firmware", json_object_new_int(data[5]));

    return 0;
}


static int encodePigeonInfo(ModbusSourceT *source, ModbusFormatCbT *format, json_object *sourceJ, uint16_t **response, uint index) {
   
   if (!json_object_is_type (sourceJ, json_type_int))  goto OnErrorExit;
   (void)json_object_get_int (sourceJ);
   return 0;

OnErrorExit:
    AFB_API_ERROR(source->api, "encodePigeonInfo: [%s] not an interger", json_object_get_string(sourceJ));
    return 1;
}

static int decodeRCount (ModbusSourceT *source, ModbusFormatCbT *format, uint16_t *data, uint index, json_object **responseJ) {

    // extract context to get previous value
    rCountT *counter = (rCountT*) source->context;

    // convert two 16bits register into one uint32 value 
    uint32_t current= (int32_t) MODBUS_GET_INT32_FROM_INT16(data, index*format->nbreg);
    uint32_t diff;

    // 1st call get modbus value without sending a response to client
    if (counter->previous == 0) {
        counter->previous = current;
        goto NoResponse;

    } else {
        
        // compute counter diff 
        diff = current - counter->previous;
        if (diff < counter->step) goto NoResponse;
        // store current for next tic and return a response
        counter->previous = current;
        *responseJ = json_object_new_int64 ((int64_t)diff);
    }

    return 0;

NoResponse:
    *responseJ = NULL; // no response to provide
    return 0; // return 1 would cancel counter subscription an error 
}

// Alocate counter handle once at init time
static int initRCount (ModbusSourceT *source, json_object *argsJ) {
    rCountT * ctx = malloc (sizeof(rCountT));
    json_object *tmpJ;
    ctx->step = 1; // default value
    ctx->previous = 0; // never used

    // extract 'step' value from json sensor config args
    int done = json_object_object_get_ex(argsJ, "step", &tmpJ);
    if (done) ctx->step = json_object_get_int(tmpJ);

    // save context in source handle
    source->context = ctx;

    return 0;
}

// encode/decode callbacks
static ModbusFormatCbT pigeonEncoders[] = {
    {.uid="devinfo", .info="return KingPigeon Device Info as an array", .nbreg=6, .decodeCB=decodePigeonInfo, .encodeCB=encodePigeonInfo},
    {.uid="rcount", .info="Return Relative Count from Uint32", .nbreg=2, .decodeCB=decodeRCount, .encodeCB=NULL, .initCB=initRCount},
    {.uid=NULL} // must be NULL terminated
};

CTLP_ONLOAD(plugin, registryCB) {
    registerCbT callback = (registerCbT)registryCB;
    assert (callback);
    (*callback) (plugin->uid, pigeonEncoders);
    return 0;
}
