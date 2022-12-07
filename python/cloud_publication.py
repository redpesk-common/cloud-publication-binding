#!/usr/bin/env python
#
###########################################################################
# Copyright (C) 2022 IoT.bzh Company
# Author Vincent Rubiolo <vincent.rubiolo@iot.bzh>
#
# $RP_BEGIN_LICENSE$
# Commercial License Usage
#  Licensees holding valid commercial IoT.bzh licenses may use this file in
#  accordance with the commercial license agreement provided with the
#  Software or, alternatively, in accordance with the terms contained in
#  a written agreement between you and The IoT.bzh Company. For licensing terms
#  and conditions see https://www.iot.bzh/terms-conditions. For further
#  information use the contact form at https://www.iot.bzh/contact.
#
# GNU General Public License Usage
#  Alternatively, this file may be used under the terms of the GNU General
#  Public license version 3. This license is as published by the Free Software
#  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
#  of this file. Please review the following information to ensure the GNU
#  General Public License requirements will be met
#  https://www.gnu.org/licenses/gpl-3.0.html.
# $RP_END_LICENSE$
#
###########################################################################

import _afbpyglue as libafb
import datetime
import os
import redis
import textwrap

import main
import sync

import time

count = 0
binding_config = None
redis_local = None
redis_cloud = None
binder = None


def cp_notice(message):
    """Send a notice-level message for the binder."""

    libafb.notice(binder, "%s: %s", libafb.config(binder, "uid"), message)


def cp_error(message):
    """Send an error-level message for the binder."""

    libafb.error(binder, "%s: %s", libafb.config(binder, "uid"), message)


def start_pub_cb(rqt, *args):
    """Start the cloud publication engine."""

    cp_notice('starting synchronization')
    main.start_sync(redis_local, redis_cloud, binding_config)
    return (0, {"message": "publication started"})


def stop_pub_cb(rqt, *args):
    """Stop the cloud publication engine."""

    cp_notice('stopping synchronization')
    return (0, {"message": "publication stopped"})


def ping_cb(rqt, *args):
    """The ping callback for the cloud publication binding."""

    global count
    count += 1
    return (0, {"pong": count})


def bandwidth_get_cb(rqt, *args):
    """Get the bandwidth level."""

    bp_level = redis_local.sync_info.get_bandwidth_level()
    return (0, {"bandwidth_level": f"{bp_level}"})


def bandwidth_set_cb(rqt, *args):
    """Set the bandwidth level."""

    try:
        redis_local.sync_info.set_bandwidth_level(args[0])
    except sync.SyncInfoException as e:
        return (-1, {"error": f"{e.args}"})

    return (0, {"message": "bandwidth level updated"})


# Cloud publication API verbs (the binding verbs)
cloud_pub_verbs = [
    {'uid': 'cp-test', 'verb': 'ping', 'callback': ping_cb,
     'info': 'ping verb, use it to test the binding is alive'},
    {'uid': 'cp-start', 'verb': 'sync/start',
        'callback': start_pub_cb, 'info': 'start cloud publication'},
    {'uid': 'cp-stop', 'verb': 'sync/stop',
        'callback': stop_pub_cb, 'info': 'stop cloud publication'},
    {'uid': 'cp-bandwidth-set', 'verb': 'bandwidth/set', 'callback': bandwidth_set_cb,
     'info': 'set bandwidth level (high/medium/low/none)'},
    {'uid': 'cp-bandwidth-get', 'verb': 'bandwidth/get', 'callback': bandwidth_get_cb,
     'info': 'get current bandwidth level'},
]

# Locally defined cloud publication API
cloud_pub_api = {
    'uid': 'cloud-pub-api',
    'api': 'cloud-pub',
    'class': 'test',
    'info': 'cloud publication binding API',
    'verbose': 9,
    'export': 'public',
    'verbs': cloud_pub_verbs,
    'alias': ['/devtools:/usr/share/afb-ui-devtools/binder'],
}

# Local Redis represents the Redis store we are reading from. In this case, we
# are doing an actual binding load (contrary to redis-cloud).
# XXX: using public since protected fails on afb_api_ws_add_server() as it tries
# to export the API locally
redis_local_binding = {
    'uid': 'redis-tsdb-local',
    'export': 'public',
    'path': 'redis-binding.so',
    'ldpath': [os.path.join(os.getenv('HOME'), '.local/redpesk/share/redis-tsdb-binding/lib')],
}

# Cloud Redis API. This represents the Redis store we are writing into. Note that we
# import its associated API, as opposed to loading a dedicated binding.
redis_cloud_api = {
    'uid': 'redis-tsdb-cloud',
    'export': 'public',
    'uri': 'tcp:localhost:21212/redis-cloud',
    'ldpath': [os.path.join(os.getenv('HOME'), '.local/redpesk/redis-tsdb-binding/lib')],
}

# Define and instantiate a binder
binder_opts = {
    'uid': 'cloud-pub-python',
    'port': 1234,
    'verbose': 9,
    'rootdir': '.',
    'trapfaults': True
}


def callsync(binder, api, verb, *args):
    '''
    Wrapper for synchronous AFB calls
    '''

    #r = callsync(binder, 'redis', 'info')
    #print(f'Answer: {r[0]}')
    #r = callsync(binder, 'redis-cloud', 'ping')

    libafb.notice(binder, f'Calling {api}/{verb} [args: {args}]')
    r = libafb.callsync(binder, api, verb, *args)
    if r.status == 0:
        libafb.notice(
            binder, f'{api}/{verb}: call was successful. Answer: {r.args}')
    else:
        libafb.warning(
            binder, f'{api}/{verb}: call failed with error {r.args}')

    return r.args


def main_loop_cb(binder, nohandle):
    """Binding main loop."""

    global binding_config
    global redis_local
    global redis_cloud

    binding_config = main.setup_config()
    if binding_config is None:
        cp_error("error parsing configuration. Aborting.")
        # Configuration error, abort.
        return -1
    else:
        cp_notice("binder configuration parsing successful")

    # Database connections
    cp_notice("connecting to databases")
    (redis_local, redis_cloud) = \
        main.connect_databases(binding_config)
    if redis_local is None or redis_cloud is None:
        cp_error("error during databases connection. Aborting.")
        return -1
    else:
        cp_notice("database connections successful")

    # Ready to run, start the endless loop
    cp_notice("binder ready")

    # Let the library handle option configurations like autostart)
    if binding_config.sync_autostart:
        cp_notice('synchronization autostart enabled. Proceeding...')
        main.start_sync(redis_local, redis_cloud, binding_config)
    else:
        cp_notice('synchronization autostart disabled. Exiting')

    return 0


# create and start binder
binder = libafb.binder(binder_opts)

# local side redis
libafb.binding(redis_local_binding)
# cloud side redis
libafb.apiadd(redis_cloud_api)
# cloud publication API
libafb.apiadd(cloud_pub_api)

# should never return
status = libafb.loopstart(binder, main_loop_cb)
if status < 0:
    cp_error("Error during binder main loop execution!")
else:
    cp_notice("Binder main loop successfully exited.")
