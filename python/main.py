#!/usr/bin/env python

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

import datetime
import logging
import os
import redis
import textwrap
import time

from redis_db import RedisDB
from config import setup_config
from sync import sync_keys, sync_intervals
from threading import Thread

logger = logging.getLogger('seanatic')


def connect_local_redis(config):
    """Connect to the local Redis DB."""

    redis_local = None
    try:
        desc = 'redis local'
        redis_local = RedisDB(desc, config.redis_local_host,
                              config.redis_local_port, config,
                              sync_support=True)
    except redis.exceptions.ConnectionError as e:
        logger.critical(f'main: error connecting to {desc}: {e}')

    return redis_local


def connect_remote_redis(config):
    """Connect to the remote Redis DB."""

    redis_cloud = None
    try:
        desc = 'redis cloud'
        redis_cloud = RedisDB(desc, config.redis_cloud_host,
                              config.redis_cloud_port, config,
                              sync_support=False)
    except redis.exceptions.ConnectionError as e:
        logger.critical(f'main: error connecting to {desc}: {e}')

    return redis_cloud


def connect_databases(config):
    """Connect databases (local and remote/cloud sides)."""

    redis_local = connect_local_redis(config)
    redis_cloud = connect_remote_redis(config)

    return (redis_local, redis_cloud)


def start_sync_entry(*args):
    """Entry point for the synchronization engine."""

    (name, redis_local, redis_cloud, config) = args

    while True:
        logger.info(f'{name}: syncing keys ...')
        sync_keys(redis_local, redis_cloud, config.sync_key_label_ts,
                  config.compaction_key_suffix, config.compaction_enabled,
                  config.aggregator, config.bucket_duration, config.verbosity)

        logger.info(f'{name}: syncing intervals ...')
        status = sync_intervals(redis_local, redis_cloud,
                                config.sync_key_label_ts, config.verbosity)

        if status != 0:
            logger.critical(f'{name}: critical sync error! Exiting.')
            return -1

        poll_freq = config.sync_db_poll_freq
        s = f'{name}: sleeping for {poll_freq} secs before next DB poll'
        logger.info(s)
        time.sleep(poll_freq)

        # At this point, a sync has been completed. Refresh both keys and
        # intervals before going on with the next round of sync.
        # We do this here instead of at the loop entry because the RedisDB
        # constructor has already done key parsing+interval computation for the
        # first sync iteration.

        # Refresh keys. This will give the new timestamps for the TS keys and
        # fetch potentially new keys (the latter would mean new sensors got
        # added to a system during sync so unlikely ...)
        logger.info(f'{name}: refreshing key list ...')
        redis_local.parse_db_keys()
        redis_cloud.parse_db_keys()

        # Recompute a new sync time range from the key information
        logger.info(f'{name}: refreshing interval list ...')
        (firstTs, lastTs) = redis_local.get_sync_time_range()

        if lastTs != redis_local.lastTs:
            # The new end timestamp differs from the current one, this means new
            # entries got added since the last sync.  Setup a new sync round.
            logger.info(f'{name}: new DB entries detected, recomputing '
                        f'interval ...')
            redis_local.firstTs = redis_local.lastTs
            redis_local.lastTs = lastTs

            redis_local.generate_sync_info()
            # generate_sync_info() will have potentially loaded indicators from
            # the DB, make sure we properly set the sync pending one after that
            # + resync the computed runtime values to the DB.
            redis_local.sync_info.mark_sync_as_pending()
            redis_local.sync_info.persist_settings_to_db()
        else:
            logger.info(f'{name}: no new DB entries detected, pending...')


def start_sync(redis_local, redis_cloud, config):
    """Synchronization engine start routine."""

    thread_name = 'sync_engine_thread'
    t = Thread(target=start_sync_entry, name=thread_name,
               args=(thread_name, redis_local, redis_cloud, config))
    t.start()


def main():
    """Main code entry point when running as a script."""

    config = setup_config()
    if config is None:
        return

    (redis_local, redis_cloud) = connect_databases(config)
    if config.sync_autostart:
        logger.info('main: synchronization autostart enabled. Proceeding...')
        start_sync(redis_local, redis_cloud, config)
    else:
        logger.info('main: synchronization autostart disabled. Exiting')
        return


if __name__ == '__main__':
    main()
