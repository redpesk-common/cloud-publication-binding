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

#ifndef _CLOUD_PUB_BINDING_
#define _CLOUD_PUB_BINDING_

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

extern const char * infoVerb;

#endif /* _CLOUD_PUB_BINDING_ */
