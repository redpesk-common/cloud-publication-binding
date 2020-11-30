/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author Fulup Ar Foll <fulup@iot.bzh>
 * Author Fulup Ar Foll <romain@iot.bzh>
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


#ifndef _MODBUS_BINDING_INCLUDE_
#define _MODBUS_BINDING_INCLUDE_

// usefull classical include
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define  AFB_BINDING_VERSION 3
#include <afb/afb-binding.h>
#include <afb-timer.h>
#include <wrap-json.h>


#ifndef ERROR
  #define ERROR -1
#endif

typedef enum {
  MB_TYPE_UNSET=0,      // Null is not a valid default
  MB_COIL_STATUS,       // Func Code Read=01 WriteSingle=05 WriteMultiple=15
  MB_COIL_INPUT,        // Func Code (read single only)=02
  MB_REGISTER_INPUT,    // Func Code (read single only)=04 
  MB_REGISTER_HOLDING,  // Func Code Read=03 WriteSingle=06 WriteMultiple=16
} ModbusTypeE;          

// hack to get double link rtu<->sensor
typedef struct ModbusSensorS ModbusSensorT;
typedef struct ModbusFunctionCbS ModbusFunctionCbT;
typedef struct ModbusRtuS ModbusRtuT;
typedef struct ModbusEncoderCbS ModbusFormatCbT;
typedef struct ModbusSourceS ModbusSourceT;

struct ModbusEncoderCbS {
  const char *uid;
  const char *info;
  const uint nbreg;
  int  subtype;
  int (*encodeCB)(ModbusSourceT *source, struct ModbusEncoderCbS *format, json_object *sourceJ, uint16_t **response, uint index);
  int (*decodeCB)(ModbusSourceT *source, struct ModbusEncoderCbS *format, uint16_t *data, uint index, json_object **responseJ);
  int (*initCB)(ModbusSourceT *source, json_object *argsJ);
};

struct ModbusSourceS {
  const char *sensor;
  afb_api_t api;
  void *context;
};

 struct ModbusRtuS {
  const char *uid;
  const char *info;
  const char *privileges;
  const char *prefix;
  const char *uri;
  const int timeout;
  const int iddle;
  const int slaveid;
  const int debug;
  uint hertz;  // default pooling frequency when subscribing to sensors
  const uint idlen;  // no default for slaveid len but use 1 when nothing given
  const uint autostart;  // 0=no 1=try 2=mandatory
  void *context;
  ModbusSensorT *sensors;
};

struct ModbusSensorS {
  const char *uid;
  const char *info;
  const uint registry;
  uint count;
  uint hertz;
  uint iddle;
  uint16_t *buffer; 
  ModbusFormatCbT *format;
  ModbusFunctionCbT *function;
  ModbusRtuT *rtu;
  TimerHandleT *timer;
  afb_api_t api;
  afb_event_t event;
  void *context;
};

struct ModbusFunctionCbS {
  const char *uid;
  const char *info;
  ModbusTypeE type;
  int (*readCB) (ModbusSensorT *sensor, json_object **outputJ);
  int (*writeCB)(ModbusSensorT *sensor, json_object *inputJ);
  int (*WReadCB)(ModbusSensorT *sensor, json_object *inputJ, json_object **outputJ);
} ;

typedef struct {
  uint16_t *buffer; 
  uint count;
  int iddle;
  ModbusSensorT *sensor;
} ModbusEvtT;


// modbus-glue.c
void ModbusSensorRequest (afb_req_t request, ModbusSensorT *sensor, json_object *queryJ);
void ModbusRtuRequest (afb_req_t request, ModbusRtuT *rtu, json_object *queryJ);
int ModbusRtuConnect (afb_api_t api, ModbusRtuT *rtu);
int ModbusRtuIsConnected (afb_api_t api, ModbusRtuT *rtu);
ModbusFunctionCbT * mbFunctionFind (afb_api_t api, const char *uri);

// modbus-encoder.c
ModbusFormatCbT *mbEncoderFind (afb_api_t api, const char *uri) ;
void mbEncoderRegister (char *uid, ModbusFormatCbT *encoderCB);
void mbRegisterCoreEncoders (void);
typedef void (*registerCbT) (const char *uid, ModbusFormatCbT *encoderCB);

#endif /* _MODBUS_BINDING_INCLUDE_ */
