/*
 * Copyright (C) 2018 "IoT.bzh"
 * Author "Arthur Guyader" <arthur.guyader@iot.bzh>
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

#define _GNU_SOURCE

#include "modbus-binding.h"
#include <ctl-plugin.h>
#include <time.h>

#include <math.h>

CTLP_CAPI_REGISTER("raymarine-anemometer");

struct windDirectionS {
    int offset_angle_deg;
};

// Allocate wind data once at init time
static int initWindDirection (ModbusSourceT *source, json_object *argsJ) {
    struct windDirectionS *ctx = malloc(sizeof(struct windDirectionS));
    json_object *tmpJ;
    ctx->offset_angle_deg = 0;

    // extract 'offset' value from json sensor config args
    if(json_object_object_get_ex(argsJ, "step", &tmpJ) < 0) {
        free(ctx);
        return -1;
    }

    ctx->offset_angle_deg = json_object_get_int(tmpJ);

    // save context in source handle
    source->context = ctx;

    return 0;
}


static int decodeWindDirection (ModbusSourceT *source, ModbusFormatCbT *format, uint16_t *data, uint index, json_object **responseJ) {
    *responseJ = json_object_new_object();
    const struct windDirectionS *ctx = (const struct windDirectionS*)source->context;

    double sin_modbus = data[0];
    double cos_modbus = data[1];

    const double sin_min = 1450;
    const double sin_max = 3270;

    const double cos_min = 1460;
    const double cos_max = 3290;

    double sin = ((sin_modbus - sin_min) / (sin_max - sin_min)) * 2 - 1;
    double cos = ((cos_modbus - cos_min) / (cos_max - cos_min)) * 2 - 1;

    double angle = fmod(atan2(cos, sin) + M_PI + ctx->offset_angle_deg / 180 * M_PI, 2 * M_PI);
    json_object_object_add (*responseJ, "windDirection", json_object_new_double(angle));
    return 0;
}


static int encodeWindDirection(ModbusSourceT *source, ModbusFormatCbT *format, json_object *sourceJ, uint16_t **response, uint index) {

   if (!json_object_is_type (sourceJ, json_type_int))  goto OnErrorExit;
   //int16_t value = (int16_t)json_object_get_int (sourceJ);
   return 0;

OnErrorExit:
    AFB_API_ERROR(source->api, "encodeWindDirection: [%s] not an interger", json_object_get_string(sourceJ));
    return 1;
}

struct windSpeedS {
    int old_count;
    int count;
    int delay;
    struct timespec time;
};

// Allocate wind speed data once at init time
static int initWindSpeed (ModbusSourceT *source, json_object *argsJ) {
    struct windSpeedS *ctx = malloc(sizeof(struct windSpeedS));
    ctx->old_count = 0;
    ctx->count = 0;
    ctx->delay = 0;

    // save context in source handle
    source->context = ctx;

    return 0;
}

static int decodeWindSpeed(ModbusSourceT *source, ModbusFormatCbT *format, uint16_t *data, uint index, json_object **responseJ) {
    struct windSpeedS *ctx = (struct windSpeedS *)source->context;
    struct timespec new_time;

    *responseJ = json_object_new_object();
    int new_count = data[0];

    if(ctx->time.tv_nsec == 0) { // first pass
        json_object_object_add (*responseJ, "windSpeed", json_object_new_int(0));
        clock_gettime(CLOCK_REALTIME, &new_time);
        ctx->time = new_time;
        return 0;
    }

    clock_gettime(CLOCK_REALTIME, &new_time);
    int delay_ms = (int)((new_time.tv_sec - ctx->time.tv_sec)*1000 + (new_time.tv_nsec - ctx->time.tv_nsec)/1000000);
    ctx->delay = ctx->delay + delay_ms;
    ctx->count = ctx->count + new_count - ctx->old_count;
    ctx->delay = ctx->delay + delay_ms;

    if(ctx->delay < 5000)
        return -1;

    double second = ctx->delay / 1000;
    double meter = ctx->count * 1.25;

    double meter_second = meter / second;
    json_object_object_add (*responseJ, "windSpeed", json_object_new_double(meter_second));

    ctx->count = 0;
    ctx->delay = 0;

    ctx->old_count = new_count;
    ctx->time = new_time;

    return 0;
}

static int encodeWindSpeed(ModbusSourceT *source, ModbusFormatCbT *format, json_object *sourceJ, uint16_t **response, uint index) {

   if (!json_object_is_type (sourceJ, json_type_int))  goto OnErrorExit;
   //int16_t value = (int16_t)json_object_get_int (sourceJ);
   return 0;

OnErrorExit:
    AFB_API_ERROR(source->api, "encodeWindDirection: [%s] not an interger", json_object_get_string(sourceJ));
    return 1;
}
// encode/decode callbacks
static ModbusFormatCbT cesEncoders[] = {
    {.uid="windDirection", .info="return wind direction", .nbreg=2, .decodeCB=decodeWindDirection, .encodeCB=encodeWindDirection, .initCB=initWindDirection},
    {.uid="windSpeed", .info="return wind speed", .nbreg=1, .decodeCB=decodeWindSpeed, .encodeCB=encodeWindSpeed, .initCB=initWindSpeed},
    {.uid=NULL} // must be NULL terminated
};

CTLP_ONLOAD(plugin, registryCB) {
    registerCbT callback = (registerCbT)registryCB;
    assert (callback);
    (*callback) (plugin->uid, cesEncoders);
    return 0;
}
