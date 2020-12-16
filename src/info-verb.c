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

const char * infoVerb = " \
{ \
  \"metadata\": { \
      \"uid\": \"cloud-pub\", \
      \"info\": \"Cloud publication binding: allows to publish target data to the cloud\", \
      \"version\": \"1.0\" \
    }, \
    \"groups\": [ \
      { \
        \"uid\": \"main\", \
        \"info\": \"General\", \
        \"verbs\": [ \
          { \
            \"uid\": \"start\", \
            \"info\": \"Enables cloud publication\", \
            \"verb\": \"start\", \
            \"usage\": { \
            }, \
            \"sample\": [ \
              { \
              } \
            ] \
          }, \
          { \
            \"uid\": \"stop\", \
            \"info\": \"Disables cloud publication\", \
            \"verb\": \"stop\", \
            \"usage\": { \
            }, \
            \"sample\": [ \
              { \
              } \
            ] \
          }, \
          { \
            \"uid\": \"info\", \
            \"info\": \"Generic information about the binding\", \
            \"verb\": \"info\", \
            \"usage\": { \
            }, \
            \"sample\": [ \
              { \
              } \
            ] \
          } \
        ] \
      } \
    ] \
} \
";