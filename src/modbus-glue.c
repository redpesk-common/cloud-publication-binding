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
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY Kidx, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE


#include <modbus.h>
#include <errno.h>
#include <sys/socket.h>
#include "modbus-binding.h"


static int ModBusFormatResponse (ModbusSensorT *sensor, json_object **responseJ) {
    ModbusFormatCbT *format = sensor->format;
    ModbusSourceT source;
    json_object *elemJ;
    int err;
    
    if (!format->decodeCB) {
        AFB_API_NOTICE(sensor->api, "ModBusFormatResponse: No decodeCB uid=%s", sensor->uid);
        goto OnErrorExit;
    }

        // create source info
    source.sensor = sensor->uid;
    source.api = sensor->api;
    source.context = sensor->context;

    if (sensor->count == 1) {
        err = format->decodeCB (&source, format, (uint16_t*)sensor->buffer, 0, responseJ);
        if (err) goto OnErrorExit;
    } else {
        *responseJ = json_object_new_array();
        for (int idx=0; idx < sensor->count; idx++) {
            err = sensor->format->decodeCB (&source,format, (uint16_t*)sensor->buffer, idx, &elemJ);
            if (err) goto OnErrorExit;
            json_object_array_add (*responseJ, elemJ);
        }
    }
    return 0;

OnErrorExit:
    return 1;
}

// try to reconnect when RTU close connection
static void ModbusReconnect (ModbusSensorT *sensor) {
    modbus_t *ctx = (modbus_t*)sensor->rtu->context; 
    AFB_API_NOTICE(sensor->api, "ModbusReconnect: Try reconnecting rtu=%s", sensor->rtu->uid);

    // force deconnection/reconnection
    modbus_close(ctx);
    int err = modbus_connect(ctx);
    if (err) {
        AFB_API_ERROR(sensor->api, "ModbusReconnect: Socket disconnected rtu=%s error=%s", sensor->rtu->uid, strerror(err)); 
    }
}

static int ModBusReadBits (ModbusSensorT *sensor, json_object **responseJ) {
    ModbusFunctionCbT *function = sensor->function;
    ModbusRtuT *rtu = sensor->rtu;
    modbus_t *ctx = (modbus_t*)rtu->context;    
    int err;

    // allocate input buffer on 1st read (all buffer are in 16bit for event diff processing)
    if (!sensor->buffer) sensor->buffer = (uint16_t*)calloc (sensor->count ,sizeof(uint16_t));
    uint8_t *data8= (uint8_t*) sensor->buffer;

    switch (function->type) {
    case MB_COIL_STATUS:  
        err= modbus_read_bits(ctx, sensor->registry, sensor->count, data8); 
        if (err != sensor->count) goto OnErrorExit;
        break;

    case MB_COIL_INPUT:
        err= modbus_read_input_bits(ctx, sensor->registry, sensor->count, data8); 
        if (err != sensor->count) goto OnErrorExit;
        break;

    default:
        err= 0;
        goto OnErrorExit;    
    }

    // if responseJ is provided build JSON response
    if (responseJ) {
        err = ModBusFormatResponse (sensor, responseJ);
        if (err) goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    AFB_API_ERROR(sensor->api, "ModBusReadBit: fail to read rtu=%s sensor=%s error=%s", rtu->uid, sensor->uid, modbus_strerror(errno)); 
    if (err == -1) ModbusReconnect (sensor);
    return 1;
}


static int ModBusReadRegisters (ModbusSensorT *sensor, json_object **responseJ) {
    ModbusFunctionCbT *function = sensor->function;
    ModbusFormatCbT *format = sensor->format;
    ModbusRtuT *rtu = sensor->rtu;
    modbus_t *ctx = (modbus_t*)rtu->context;    
    int err, regcount;

    regcount = sensor->count*format->nbreg; // number of register to read on device
    
    // allocate input buffer on 1st read
    if (!sensor->buffer) sensor->buffer= (uint16_t*)calloc (regcount, sizeof(uint16_t));

    switch (function->type) {
    case MB_REGISTER_INPUT:  
        err= modbus_read_input_registers(ctx, sensor->registry, regcount, (uint16_t*)sensor->buffer); 
        if (err != regcount ) goto OnErrorExit;
        break;

    case MB_REGISTER_HOLDING:
        err= modbus_read_registers(ctx, sensor->registry, regcount, (uint16_t*)sensor->buffer); 
        if (err != regcount) goto OnErrorExit;
        break;

    default:
        err=0;
        goto OnErrorExit;    
    }

    // if responseJ is provided build JSON response
    if (responseJ) {
        err = ModBusFormatResponse (sensor, responseJ);
        if (err) goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    AFB_API_ERROR(sensor->api, "ModBusReadRegisters: fail to read rtu=%s sensor=%s error=%s", rtu->uid, sensor->uid, modbus_strerror(errno)); 
    if (err == -1) ModbusReconnect (sensor);
    return 1;
}

static int ModBusWriteBits (ModbusSensorT *sensor, json_object *queryJ) {
    ModbusFormatCbT *format = sensor->format;
    ModbusRtuT *rtu = sensor->rtu;
    modbus_t *ctx = (modbus_t*)rtu->context;  
    json_object *elemJ;  
    int err, idx;

    uint8_t *data8 = (uint8_t *) alloca (sizeof(uint8_t)*format->nbreg * sensor->count);


    if (!json_object_is_type(queryJ, json_type_array)) {
        data8[0]= (uint8_t) json_object_get_boolean (queryJ);

        err= modbus_write_bit(ctx, sensor->registry, data8[0]); // Modbus function code 0x05 (force single coil).
        if (err != 1) goto OnErrorExit;

    } else {
        for (idx=0; idx < sensor->format->nbreg; idx++) {
            elemJ = json_object_array_get_idx (queryJ, idx);
            data8[idx] = (uint8_t) json_object_get_boolean (elemJ);
        }
        err= modbus_write_bits(ctx, sensor->registry, sensor->format->nbreg, data8); // Modbus function code 0x0F (force multiple coils).
        if (err != sensor->format->nbreg) goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    AFB_API_ERROR(sensor->api, "ModBusWriteBits: fail to write rtu=%s sensor=%s error=%s data=%s", rtu->uid, sensor->uid, modbus_strerror(errno), json_object_get_string(queryJ));  
    if (err == -1) ModbusReconnect (sensor);
    return 1;
}

static int ModBusWriteRegisters (ModbusSensorT *sensor, json_object *queryJ) {
    ModbusFormatCbT *format = sensor->format;
    ModbusRtuT *rtu = sensor->rtu;
    modbus_t *ctx = (modbus_t*)rtu->context;
    json_object *elemJ;
    int err = 0;
    int idx = 0;
    ModbusSourceT source;

    uint16_t *data16= (uint16_t *)alloca(sizeof(uint16_t) * format->nbreg * sensor->count);

    // create source info
    source.sensor = sensor->uid;
    source.api = sensor->api;
    source.context = sensor->context;

    if (!format->encodeCB) {
        AFB_API_NOTICE(sensor->api, "ModBusFormatResponse: No encodeCB uid=%s", sensor->uid);
        goto OnErrorExit; 
    }

    if (!json_object_is_type(queryJ, json_type_array))  {


        err= format->encodeCB (&source, format, queryJ, &data16, 0);
        if (err) goto OnErrorExit;
        if (format->nbreg == 1) {
            err= modbus_write_register(ctx, sensor->registry, data16[0]); // Modbus function code 0x06 (preset single register).
        } else {
            err= modbus_write_registers(ctx, sensor->registry, format->nbreg , data16); // Modbus function code 0x06 (preset single register).
        }
        if (err != format->nbreg) goto OnErrorExit;

    } else {
        for (idx=0; idx < sensor->format->nbreg; idx++) {
            elemJ = json_object_array_get_idx (queryJ, idx);
            err= format->encodeCB (&source, format, elemJ, &data16, idx);
            if (err) goto OnErrorExit; 
        }
        err= modbus_write_registers(ctx, sensor->registry, format->nbreg, data16); // Modbus function code 0x10 (preset multiple registers).
        if (err != format->nbreg) goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    AFB_API_ERROR(sensor->api, "ModBusWriteBits: fail to write rtu=%s sensor=%s error=%s data=%s", rtu->uid, sensor->uid, modbus_strerror(errno), json_object_get_string(queryJ));  
    if (err == -1) ModbusReconnect (sensor);
    return 1;
}

// Modbus Read/Write per register type/function Callback 
static ModbusFunctionCbT ModbusFunctionsCB[]= {
  {.uid="COIL_INPUT",      .type=MB_COIL_INPUT,       .info="Bollean ReadOnly register" , .readCB=ModBusReadBits},
  {.uid="COIL_HOLDING",    .type=MB_COIL_STATUS,      .info="Bollean ReadWrite register", .readCB=ModBusReadBits, .writeCB=ModBusWriteBits},
  {.uid="REGISTER_INPUT",  .type=MB_REGISTER_INPUT,   .info="INT16 ReadOnly register",    .readCB=ModBusReadRegisters},
  {.uid="REGISTER_HOLDING",.type=MB_REGISTER_HOLDING, .info="INT16 ReaWrite register",    .readCB=ModBusReadRegisters, .writeCB=ModBusWriteRegisters},

  {.uid = NULL} // should be NULL terminated
};

// return type/function callbacks from sensor type name
ModbusFunctionCbT * mbFunctionFind (afb_api_t api, const char *uid) {
    int idx;
    assert (uid);

    for (idx=0; ModbusFunctionsCB[idx].uid; idx++) {
        if (!strcasecmp (ModbusFunctionsCB[idx].uid, uid)) break;
    }

    return (&ModbusFunctionsCB[idx]); 
}

// Timer base sensor pooling tic send event if sensor value changed
static int ModbusTimerCallback (TimerHandleT *timer) {

    assert(timer);

    ModbusEvtT *context= (ModbusEvtT*) timer->context;
    ModbusSensorT *sensor = context->sensor;
    json_object *responseJ;
    int err, count;

    // update sensor buffer with current value without building JSON
    err = (sensor->function->readCB) (sensor, NULL);
    if (err) {
        AFB_API_ERROR(sensor->api, "ModbusTimerCallback: fail read sensor rtu=%s sensor=%s", sensor->rtu->uid, sensor->uid);
        goto OnErrorExit;
    }


    // if buffer change then update JSON and send event
    if (memcmp (context->buffer, sensor->buffer, sizeof(uint16_t)*sensor->format->nbreg*sensor->count) || !--context->iddle) {

        // if responseJ is provided build JSON response
        err = ModBusFormatResponse (sensor, &responseJ);
        if (err) goto OnErrorExit;

        // send event and it no more client remove event and timer
        count= afb_event_push (sensor->event, responseJ);
        if (count == 0) {
            afb_event_unref (sensor->event);
            sensor->event= NULL;
            free (context->buffer);
            TimerEvtStop (timer);
        } else {
            // save current sensor buffer for next comparison
            memcpy (context->buffer, sensor->buffer, sizeof(uint16_t) * sensor->format->nbreg * sensor->count); 
            context->iddle = sensor->iddle; // reset iddle counter
        }
    } 
    return 1;

OnErrorExit:
    return 1;  // Returning 0 would stop the timer
}

static int ModbusSensorEventCreate (ModbusSensorT *sensor, json_object **responseJ) {
    ModbusRtuT *rtu = sensor->rtu;
    int err;
    char *timeruid;
    ModbusEvtT *mbEvtHandle;

    if (!sensor->function->readCB) goto OnErrorExit;
    err = (sensor->function->readCB) (sensor, responseJ);
    if (err) {
        AFB_API_ERROR(sensor->api, "ModbusSensorEventCreate: fail read sensor rtu=%s sensor=%s", rtu->uid, sensor->uid);  
        goto OnErrorExit;
    }

    // if no even attach to sensor create one
    if (!sensor->event) {
        sensor->event = afb_api_make_event(sensor->api, sensor->uid);
        if (!sensor->event) {
            AFB_API_ERROR(sensor->api, "ModbusSensorEventCreate: fail to create event rtu=%s sensor=%s", rtu->uid, sensor->uid);  
            goto OnErrorExit;
        }

        sensor->timer = (TimerHandleT*)calloc (1, sizeof(TimerHandleT));
        sensor->timer->delay = (uint) 1000 / rtu->hertz;
        sensor->timer->count = -1; // run forever
        err=asprintf (&timeruid, "%s/%s", rtu->uid, sensor->uid);
        sensor->timer->uid = timeruid;
        
        mbEvtHandle = (ModbusEvtT*)calloc (1, sizeof(ModbusEvtT));
        mbEvtHandle->sensor = sensor;
        mbEvtHandle->iddle =  sensor->iddle;
        mbEvtHandle->buffer = (uint16_t*)calloc (sensor->format->nbreg*sensor->count,sizeof(uint16_t)); // keep track of old value

        TimerEvtStart (sensor->api, sensor->timer, ModbusTimerCallback, mbEvtHandle);
    } 
    return 0;

OnErrorExit:
    return 1;           
}

void ModbusSensorRequest (afb_req_t request, ModbusSensorT *sensor, json_object *queryJ) {
    assert (sensor);
    assert (sensor->rtu);
    ModbusRtuT *rtu = sensor->rtu;
    const char *action;
    json_object *dataJ, *responseJ=NULL;
    int err;

    if (!rtu->context) {
        afb_req_fail_f(request, "not-connected", "ModbusSensorRequest: RTU not connected rtu=%s sensor=%s query=%s"
            ,rtu->uid, sensor->uid, json_object_get_string(queryJ));
        goto OnErrorExit;
    };

    err= wrap_json_unpack(queryJ, "{ss s?o !}", "action", &action, "data", &dataJ);
    if (err) {
        afb_req_fail_f(request, "query-error", "ModbusSensorRequest: invalid 'json' rtu=%s sensor=%s query=%s"
            , rtu->uid, sensor->uid, json_object_get_string(queryJ));
        goto OnErrorExit;
    }

    if (!strcasecmp (action, "WRITE")) {
        if (!sensor->function->writeCB) goto OnWriteError;
        err = (sensor->function->writeCB) (sensor, dataJ);
        if (err) goto OnWriteError;

    } else if (!strcasecmp (action, "READ")) {
        if (!sensor->function->readCB) goto OnReadError;
        err = (sensor->function->readCB) (sensor, &responseJ);
        if (err) goto OnReadError;

    } else if (!strcasecmp (action, "SUBSCRIBE")) {
        err= ModbusSensorEventCreate (sensor, &responseJ);
        if (err) goto OnSubscribeError;
        err=afb_req_subscribe(request, sensor->event); 
        if (err) goto OnSubscribeError;

    }  else if (!strcasecmp (action, "UNSUBSCRIBE")) {   // Fulup ***** Virer l'event quand le count est à zero
        if (sensor->event) {
            err=afb_req_unsubscribe(request, sensor->event); 
            if (err) goto OnSubscribeError;
        }
    } else {
        afb_req_fail_f (request, "syntax-error", "ModbusSensorRequest: action='%s' UNKNOWN rtu=%s sensor=%s query=%s"
            , action, rtu->uid, sensor->uid, json_object_get_string(queryJ));
        goto OnErrorExit; 
    }
  
    // everything looks good let's response
    afb_req_success(request, responseJ, NULL);
    return;

    OnWriteError:
        afb_req_fail_f (request, "write-error", "ModbusSensorRequest: fail to write data=%s rtu=%s sensor=%s error=%s"
            , json_object_get_string(dataJ), rtu->uid, sensor->uid, modbus_strerror(errno));
        goto OnErrorExit; 

    OnReadError:
        afb_req_fail_f (request, "read-error", "ModbusSensorRequest: fail to read rtu=%s sensor=%s error=%s"
        , rtu->uid, sensor->uid, modbus_strerror(errno)); 
        goto OnErrorExit;

    OnSubscribeError:
        afb_req_fail_f (request, "subscribe-error","ModbusSensorRequest: fail to subscribe rtu=%s sensor=%s error=%s"
        , rtu->uid, sensor->uid, modbus_strerror(errno)); 
        goto OnErrorExit;

    OnErrorExit:
        return;
}

static char *ModbusParseURI (const char* uri, char **addr, int *port) {
    #define TCP_PREFIX "tcp://"
    char *chaine, *tcpport;
    int idx;
    static int prefixlen = sizeof(TCP_PREFIX)-1;

    // as today only TCP modbus is supported
    if (strncasecmp(uri, TCP_PREFIX, prefixlen)) goto OnErrorExit;

    // break URI in substring ignoring leading tcp:
    chaine= strdup(uri);   
    for (idx=prefixlen; idx < strlen(chaine); idx ++) {
        if (chaine[idx] == 0) break;
        if (chaine[idx] == ':') chaine[idx] = 0;
    }

    // extract IP addr and port
    *addr = &chaine[prefixlen];
    tcpport= &chaine[idx];
    sscanf (tcpport, "%d", port);

    return chaine;

OnErrorExit: 
    return NULL;
}

int ModbusRtuConnect (afb_api_t api, ModbusRtuT *rtu) {
    modbus_t *ctx;
    char *addr;
    int  port;

    char *dup = ModbusParseURI (rtu->uri, &addr, &port);
    if (!dup) {
        AFB_API_ERROR(api, "ModbusRtuConnect: fail to parse uid=%s uri=%s", rtu->uid, rtu->uri);
        goto OnErrorExit;
    }

    ctx = modbus_new_tcp (addr, port);
    if (modbus_connect(ctx) == -1) {
        AFB_API_ERROR(api, "ModbusRtuConnect: fail to connect TCP uid=%s addr=%s port=%d", rtu->uid, addr, port);
        modbus_free(ctx);
        goto OnErrorExit;
    }

    if (rtu->slaveid) {
        if (modbus_set_slave (ctx, rtu->slaveid) == -1) {
            AFB_API_ERROR(api, "ModbusRtuConnect: fail to set slaveid=%d uid=%s", rtu->slaveid, rtu->uid);
            modbus_free(ctx);
            goto OnErrorExit;
        }
    }

    if (rtu->timeout) {
        if (modbus_set_response_timeout (ctx, 0, rtu->timeout*1000) == -1) {
            AFB_API_ERROR(api, "ModbusRtuConnect: fail to set timeout=%d uid=%s", rtu->timeout, rtu->uid);
            modbus_free(ctx);
            goto OnErrorExit;
        }
    }

    if (rtu->debug) {
        if (modbus_set_debug (ctx, rtu->debug) == -1) {
            AFB_API_ERROR(api, "ModbusRtuConnect: fail to set debug=%d uid=%s", rtu->debug, rtu->uid);
            modbus_free(ctx);
            goto OnErrorExit;
        }
    }

    // store current libmodbus ctx with rtu handle
    rtu->context = (void*) ctx;
    return 0;

OnErrorExit:
    return 1;
}

void ModbusRtuSensorsId (ModbusRtuT *rtu, int verbose, json_object **responseJ) {
    json_object *elemJ, *dataJ;
    ModbusSensorT *sensor;
    int err;
    *responseJ= json_object_new_array();

    // loop on every sensors
    for (int idx=0; rtu->sensors[idx].uid; idx++) {
        sensor = &rtu->sensors[idx];
        switch (verbose) {
            default:
            case 1:
                wrap_json_pack (&elemJ, "{ss ss ss si si}", "uid", sensor->uid, "type", sensor->function->uid, "format", sensor->format->uid
                     , "count", sensor->count, "nbreg", sensor->format->nbreg*sensor->count);
                break;
            case 2:
                err = (sensor->function->readCB) (sensor, &dataJ);
                if (err) dataJ=NULL;
                wrap_json_pack (&elemJ, "{ss ss ss si si so}", "uid", sensor->uid, "type", sensor->function->uid, "format", sensor->format->uid
                     , "count", sensor->count, "nbreg", sensor->format->nbreg*sensor->count, "data", dataJ);
        }
        json_object_array_add (*responseJ, elemJ);

        fprintf (stderr, "*** %s **", json_object_get_string(elemJ));
    }
}

int ModbusRtuIsConnected (afb_api_t api, ModbusRtuT *rtu) {
    uint8_t response[MODBUS_MAX_PDU_LENGTH];
    modbus_t *ctx = (modbus_t*)rtu->context;
    int run;

    run= modbus_report_slave_id(ctx, sizeof(response), response); 
    if (run <0) goto OnErrorExit;

    if (run >0) return 1;
    else return 0;

OnErrorExit:
    AFB_API_ERROR(api, "ModbusRtuIsConnected: fail to get RTU=%s connection status err=%s", rtu->uid, modbus_strerror(errno));
    return -1;
}

void ModbusRtuRequest (afb_req_t request, ModbusRtuT *rtu, json_object *queryJ) {
    modbus_t *ctx = (modbus_t*)rtu->context;
    const char *action;
    const char *uri=NULL;
    int verbose=0;
    json_object *responseJ=NULL;
    int err;

    err= wrap_json_unpack(queryJ, "{ss s?s s?i !}", "action", &action, "verbose", &verbose, "uri", &uri);
    if (err) {
        afb_req_fail_f (request, "ModbusRtuAdmin", "invalid query rtu=%s query=%s", rtu->uid, json_object_get_string(queryJ));
        goto OnErrorExit;
    }

    if (!strcasecmp(action, "connect")) {

        if (rtu->context) {
            afb_req_fail_f (request, "ModbusRtuAdmin", "cannot connect twice rtu=%s query=%s", rtu->uid, json_object_get_string(queryJ));
            goto OnErrorExit;
        }

        if (!uri) {
            afb_req_fail_f (request, "ModbusRtuAdmin", "connot connect URI missing rtu=%s query=%s", rtu->uid, json_object_get_string(queryJ));
            goto OnErrorExit;
        }

        rtu->uri= uri;
        err = ModbusRtuConnect (request->api, rtu);
        if (err) {
            afb_req_fail_f (request, "ModbusRtuAdmin", "fail to parse uri=%s query=%s", uri, json_object_get_string(queryJ));
            goto OnErrorExit;
        } 

    } else if (!strcasecmp(action, "disconnect")) {
        modbus_close(ctx);
        modbus_free(ctx);
        rtu->context=NULL;
        
    } else if (!strcasecmp(action, "info")) {

        ModbusRtuSensorsId (rtu, verbose, &responseJ);
    }

    afb_req_success(request, responseJ, NULL);
    return;

OnErrorExit:
    return;    
}
