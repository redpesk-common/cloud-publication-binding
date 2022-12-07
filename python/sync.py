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

import logging
import redis
import textwrap

from utils import ts_to_str

logger = logging.getLogger('seanatic')

# Need to solve a circular dependency issue if we want to move this routine to
# the redisDB class


def check_redis_reply(key, reply):
    """Check a given Redis reply for errors."""

    if reply is None or isinstance(reply, redis.exceptions.ResponseError):
        logger.warning(f'main: redis error for {key}: {reply}')
        return -1
    else:
        return 0


class SyncInfoException(Exception):
    """Exception class for the sync engine."""

    pass


class SyncInfo():
    """Manages synchronization information for a DB.

    The sync_metrics dict is what ends up being written to the database, from
    their value field (the db_value one is the one potentially read from the
    DB).
    """

    FIELD_INTERVAL_INDEX = 'interval_index'
    FIELD_INTERVAL_KEY = 'interval_key'
    FIELD_INTERVAL_KEY_IDX = 'interval_key_index'
    FIELD_SYNC_FINISHED = 'sync_finished'
    FIELD_SYNC_BANDWIDTH_LEVEL = 'bandwidth_level'

    def __init__(self, r_handle, firstTs, lastTs, intervals_total_cnt,
                 time_interval_size):
        self.redis = r_handle

        # Use -1 as a special initialization value (avoids handling None)
        self.sync_metrics = {
            'interval_index':
                {'key': 'CLOUD_PUB_SYNC_INTERVAL_IDX', 'db_value': -1,
                 'value': -1},
            'interval_key':
                {'key': 'CLOUD_PUB_SYNC_INTERVAL_KEY', 'db_value': -1,
                 'value': -1},
            'interval_key_index':
                {'key': 'CLOUD_PUB_SYNC_INTERVAL_KEY_IDX', 'db_value': -1,
                 'value': -1},
            'ts_start':
                {'key': 'CLOUD_PUB_SYNC_TS_START', 'db_value': -1,
                 'value': firstTs},
            'ts_end':
                {'key': 'CLOUD_PUB_SYNC_TS_END', 'db_value': -1,
                 'value': lastTs},
            'intervals_total_cnt':
                {'key': 'CLOUD_PUB_SYNC_INTERVALS_TOTAL_CNT',
                 'db_value': -1, 'value': intervals_total_cnt},
            'interval_size':
                {'key': 'CLOUD_PUB_SYNC_INTERVAL_SIZE',
                 'db_value': -1, 'value': time_interval_size},
            'sync_finished':
                {'key': 'CLOUD_PUB_SYNC_FINISHED',
                 'db_value': -1, 'value': -1},
            'bandwidth_level':
                {'key': 'CLOUD_PUB_SYNC_BANDWIDTH_LEVEL',
                 'db_value': -1, 'value': 'medium'},
        }
        # Find previous sync values in DB
        self.load_settings_from_db()

    def __str__(self):
        s = ""
        for k, v in self.sync_metrics.items():
            s += (f"{k:<19} - value: {v['value']:>13} / "
                  f"db_value: {v['db_value']:>13} - {v['key']}\n")

        s += ""

        # dedent() does not work when first line is not indented?
        return textwrap.dedent(s)

    def load_settings_from_db(self):
        """Load synchronization information from the DB."""

        sm = self.sync_metrics
        for k in sm.keys():
            db_key = sm[k]['key']
            db_val = self.redis.get(db_key)

            if db_val is not None:
                clean_value = db_val.decode()
                # Almost all values retrieved from the DB are numeric. Convert
                # the DB string values because of that.
                if (k != self.FIELD_INTERVAL_KEY and
                        k != self.FIELD_SYNC_BANDWIDTH_LEVEL):
                    clean_value = int(clean_value)

                sm[k]['db_value'] = clean_value

        # The sync completion indicator value is driven by what is in the DB,
        # not computed in-memory. Sync both immediately because of that.
        sm[self.FIELD_SYNC_FINISHED]['value'] = \
            sm[self.FIELD_SYNC_FINISHED]['db_value']

    def persist_settings_to_db(self, field_space=None):
        """Persist synchronization information into the DB.

        The field space argument allows to restrict what is written on disk (for
        frequent writes).
        """

        if field_space is None:
            f_space = self.sync_metrics.keys()
        else:
            f_space = field_space

        for k in f_space:
            val = self.sync_metrics[k]['value']
            res = self.redis.set(self.sync_metrics[k]['key'], val)

            if res is None:
                raise SyncInfoException(
                    f"{k}:{val}: cannot persist sync info!")

    def is_sync_resumable(self):
        """
        Check whether things are in place to properly resume a sync.

        Being able to resume a sync means the persistence layer contained all
        necessary information. If not, we sync the currently computed sync
        values to be able to start a sync from scratch.
        """

        s = self.sync_metrics

        # Any missing sync value means we cannot resume sync. If so, we sync the
        # currently computed values to disk
        for k in self.sync_metrics.keys():
            val = self.sync_metrics[k]['db_value']
            if val is None:
                logger.info(f"sync: did not find any DB value for {k}, cannot "
                            f"resume sync.")
                self.persist_settings_to_db()
                return False

        # Consistency check for stored values: we must have computed the same
        # values as the stored ones for the sync to be properly resumed.
        db_ts_start = s['ts_start']['db_value']
        db_ts_end = s['ts_end']['db_value']
        db_intervals_total_cnt = s['intervals_total_cnt']['db_value']
        db_interval_size = s['interval_size']['db_value']
        db_interval_index = s['interval_index']['db_value']
        db_bandwidth_level = s['bandwidth_level']['db_value']

        int_ts_start = s['ts_start']['value']
        int_ts_end = s['ts_end']['value']
        int_intervals_total_cnt = s['intervals_total_cnt']['value']
        int_interval_size = s['interval_size']['value']
        int_bandwidth_level = s['bandwidth_level']['value']

        resumable = False

        # Now check that the computed values match those in the database. Note
        # that we do no write/check the time_interval_{nb,start_idx} variables
        # as they are for debugging purposes (restrict the window of intervals
        # to work on).
        if db_interval_index == -1:
            # This is the case where we synced from scratch, persisted the
            # values in the DB but were interrupted before starting on the first
            # interval and write its sync info
            logger.info(f"sync: interval index value is -1. Syncing from "
                        f"scratch.")
        elif db_ts_start != int_ts_start:
            logger.info(f"sync: mismatch: db_ts_start: "
                        f"{db_ts_start}|int_ts_start: "
                        f"{int_ts_start}. Cannot resume sync.")
        elif db_ts_end != int_ts_end:
            logger.info(f"sync: mismatch: db_ts_end: {db_ts_end}|int_ts_end: "
                        f"{int_ts_end}. Cannot resume sync.")
        elif db_intervals_total_cnt != int_intervals_total_cnt:
            logger.info(f"sync: mismatch: db_intervals_total_cnt: "
                        f"{db_intervals_total_cnt}|int_intervals_total_cnt: "
                        f"{int_intervals_total_cnt}")
        elif db_interval_size != int_interval_size:
            logger.info(f"sync: mismatch: db_interval_size: {db_interval_size}|"
                        f"int_interval_size: {int_interval_size}")
        elif db_bandwidth_level != int_bandwidth_level:
            # The bandwidth levels must match for the sync to be resumed. Note
            # that this might be too strict here, we might relax that a little
            # later on.
            logger.info(f"sync: mismatch: db_bandwidth_level: "
                        f"{db_bandwidth_level}|int_bandwidth_level: "
                        f"{int_bandwidth_level}")
        else:
            # Retrieved values are consistent, use them
            # Note that we do not sync all values: the computed ones are
            # already identical to their DB counterparts.
            s['interval_index']['value'] = s['interval_index']['db_value']
            s['interval_key']['value'] = s['interval_key']['db_value']
            s['interval_key_index']['value'] = s['interval_key_index']['db_value']
            resumable = True
            logger.info(f"sync: resumption counters OK. Sync is resumable.")

        # Sync to disk to resync from scratch
        if not resumable:
            self.persist_settings_to_db()

        return resumable

    def mark_sync_as_finished(self):
        """
        Perform operations related to synchronization completion.
        """

        logger.info('sync: finished syncing database.')

        # Reset the sync counters and remove their values in the persistence
        # layer
        for k in self.sync_metrics.keys():
            self.sync_metrics[k]['value'] = -1
            self.sync_metrics[k]['db_value'] = -1

            db_key = self.sync_metrics[k]['key']
            res = self.redis.delete(db_key)
            if res is None:
                raise SyncInfoException(
                    f"{db_key}: cannot delete in database!")

        # Set the finish marker in the sync object and on disk
        self.set(self.FIELD_SYNC_FINISHED, 1)
        db_key = self.sync_metrics[self.FIELD_SYNC_FINISHED]['key']
        self.redis.set(db_key, 1)
        if res is None:
            raise SyncInfoException(f"{db_key}: cannot persist sync info!")

        logger.info('sync: successfully cleaned up sync resumption data')

    def is_sync_finished(self):
        """Check whether the sync has finished."""

        return (self.get(self.FIELD_SYNC_FINISHED) == 1)

    def mark_sync_as_pending(self):
        """Mark the synchronization operation as pending."""

        self.set(self.FIELD_SYNC_FINISHED, 0)

    def set(self, field_spec, value):
        """Setter for a class field value."""

        self.sync_metrics[field_spec]['value'] = value

    def get(self, field_spec):
        """Getter for a class field value."""

        return self.sync_metrics[field_spec]['value']

    def get_bandwidth_level(self):
        """Retrieve the bandwidth level for the sync engine."""

        return self.get(self.FIELD_SYNC_BANDWIDTH_LEVEL)

    def set_bandwidth_level(self, bandwidth_level):
        """Set the bandwidth level for the sync engine."""

        if bandwidth_level != 'none' and bandwidth_level != 'low' \
                and bandwidth_level != 'medium' \
                and bandwidth_level != 'high':
            raise SyncInfoException(f'invalid bandwidth level '
                                    f'"{bandwidth_level}"!')
        else:
            self.set(self.FIELD_SYNC_BANDWIDTH_LEVEL, bandwidth_level)


class SyncInterval:
    """A class representing a synchronization interval."""

    def __init__(self, start, end):
        self.start = start
        self.end = end

    def __str__(self):
        s = (f"""s: {self.start} [{ts_to_str(self.start)}] """
             f"""=> e: {self.end} [{ts_to_str(self.end)}]""")

        # dedent() does not work when first line is not indented?
        return textwrap.dedent(s)


def sync_keys(redis_local, redis_cloud, key_label_ts, compaction_key_suffix,
              compaction_enabled, aggregator, bucket_duration, verbosity):
    """
    Sync keys between the two DBs.

    Both standard and TS keys are supported:
    - for standard keys: this is where the actual sync occurs as we also insert
      their value.
    - for TS keys: we only create the time series here, this ensures that
      interval sync which occurs later (and adds the associated values) works fine.
    """

    # Sync RedisTS keys
    keys_ts = redis_local.keynames_ts - redis_cloud.keynames_ts
    logger.info(f'{redis_cloud.desc}: need to add {len(keys_ts)} TS keys')

    for k in keys_ts:
        logger.debug(f'{redis_cloud.desc}: adding TS key {k}')
        reply = redis_cloud.redis.ts().create(k,
                                              labels={'class': key_label_ts})
        check_redis_reply(k, reply)

    # Create rules and TS keys if compaction is enabled
    if compaction_enabled:
        logger.info(f'{redis_cloud.desc}: creating {len(keys_ts)} '
                    f'aggregated keys')
        for k in keys_ts:
            s = key_label_ts + compaction_key_suffix
            # k is a byte array, convert the strings to perform the replacement
            # operation
            compaction_key_name = k.replace(key_label_ts.encode(), s.encode())
            logger.debug(f'{redis_cloud.desc}: adding compaction TS key '
                         f'{compaction_key_name}')
            reply = redis_cloud.redis.ts().create(compaction_key_name,
                                                  labels={'class': s})
            check_redis_reply(k, reply)

            logger.debug(f'{redis_cloud.desc}: adding compaction rule for '
                         f'key {k}')
            reply = redis_cloud.redis.ts().createrule(k,
                                                      compaction_key_name,
                                                      aggregator,
                                                      bucket_duration)
            check_redis_reply(k, reply)

    # Sync normal keys. For those, we also insert their value
    keys = redis_local.keynames - redis_cloud.keynames
    logger.info(f'{redis_cloud.desc}: need to add {len(keys)} standard keys')

    for k in keys:
        value = redis_local.keyobjs[k].value
        logger.debug(f'{redis_cloud.desc}: adding standard key {k} '
                     f'with value {value}')
        reply = redis_cloud.redis.set(k, value)
        check_redis_reply(k, reply)


def sync_intervals(redis_local, redis_cloud, sync_key_label, verbosity):
    """Sync each time interval.

    This is the main synchronization engine routine. We can either, for each
    interval:
    1. Call ts.mrange() to retrieve the records. Then, iterate over each key and
    insert its associated values via a call to ts.madd(). This requires the keys
    to be present on the remote DB as ts.madd() does not create the keys. This
    is the chosen solution, with a startup step to sync the keys.
    2. Also call ts.mrange(). Then iterate over each key and for each key
    associated value, use ts.add() to insert it into the DB. This does not
    require the key to exist on the remote side but needs an additional loop
    over each key records. It is likely we bear a test for key existence as well
    for each call to ts.add().

    The engine needs to handle various state combinations:
    1. Syncing from scratch (no available sync info or it is corrupted)
    2. Resuming from a previous sync and there is still work to do
    3. Resuming/restarting and synchronization on the previous timespan has been
    completed.

    Empirical data for a typical smart boat production database, for one month
    of data over ~150 sensors, with each sensor being sampled at 1Hz.
    - with a interval time size of 1800000 ms (30min), we get as much as 1440
      records from the ts.mrange() calls
    - the full Seanatic DB has 270502967 samples for 1 month and 2 days, for
      ~150 sensors (sampling frequency is 1Hz). The RDB file is ~350MiB.
    - syncing the full DB using two local Redis DBs and the Python redis library
      for DB access takes about 1h10min

    Early profiling data shows that most time is spent at the connection layer,
    reading data from the DB (parse_response()).
    """

    intervals = redis_local.intervals
    nb_inter = len(intervals)
    sync_info = redis_local.sync_info

    # Check whether we need and can resume the sync
    if sync_info.is_sync_finished():
        logger.info('sync: database fully synchronized, nothing to do.')
        return 0
    else:
        # Shorthand as the computation is quite involved
        sync_is_resumable = sync_info.is_sync_resumable()

        if sync_is_resumable:
            interval_idx = sync_info.get(sync_info.FIELD_INTERVAL_INDEX)
            interval_key = sync_info.get(sync_info.FIELD_INTERVAL_KEY)
            interval_key_idx = sync_info.get(sync_info.FIELD_INTERVAL_KEY_IDX)
            logger.info(f'sync: resuming synchronization at interval index '
                        f'{interval_idx}, on key at index '
                        f'#{interval_key_idx} - {interval_key}')
        else:
            interval_idx = 0
            interval_key = None
            interval_key_idx = 0
            logger.info(f'sync: resume information not available. Syncing from '
                        f'scratch.')

    resumation_done = False
    while interval_idx < nb_inter:
        inter = intervals[interval_idx]
        # For the sake of prettiness, we display the interval counter (starting
        # at 1) whereas we operate on/save the indexes (starting at 0)
        hdr = f'[{interval_idx+1}/{nb_inter}]'
        logger.info(f'{hdr} Synchronizing interval {inter}')

        # Mark sync info for the interval. This is actually a double write in
        # case of continuation since we already wrote the info before being
        # interrupted
        sync_info.set(sync_info.FIELD_INTERVAL_INDEX, interval_idx)
        sync_info.persist_settings_to_db([sync_info.FIELD_INTERVAL_INDEX])

        # Call ts.mrange() to retrieve the list of records in this time interval
        # for the given key.
        #
        # ts.mrange() returns a list of dicts indexed by the TSDB keys like:
        # [{'KEY_NAME: [{}, [(1656337798944, 0.0), (1656338283885, 1.1), ...
        key_records = redis_local.redis.ts().mrange(inter.start, inter.end,
                                                    [f'class={sync_key_label}'])
        nb_key_records = len(key_records)

        # We rely on the fact the returned list order from ts.mrange() is stable
        # to be able to keep track of where we were. If that is not the case, we
        # will end up resyncing that interval at least partially, potentially
        # triggering write errors if the DB keys are in BLOCK mode.
        #
        # If the list order is indeed stable, the key at the resumation index
        # will be the same as for the previous sync. Check that. It is a fatal
        # error otherwise.
        if not resumation_done and sync_is_resumable:
            resumation_key = next(iter(key_records[interval_key_idx]))
            if resumation_key != interval_key:
                logger.critical(f"sync: discrepancy: mismatch between "
                                f"resumation key {resumation_key} and "
                                f"interval_key {interval_key} at "
                                f"index {interval_key_idx}!")
                return -1
            else:
                logger.info(f"sync: sanity check passed: "
                            f"resumation key {resumation_key} and "
                            f"interval_key {interval_key} match at "
                            f"index {interval_key_idx}")
            resumation_done = True

        # Iterate over keys so that we minimize the length of the list sent to
        # ts.madd(). We could also use a single ts.madd() call for the entire
        # interval but this would need more runtime memory and was the reason
        # the first implementations failed memory-wise.
        while interval_key_idx < nb_key_records:
            rec = key_records[interval_key_idx]

            # The iter() method is a lot more efficient than list(e.keys())
            key = next(iter(rec))
            ts_data = rec[key][1]

            hdr2 = f'[{interval_key_idx}/{nb_key_records}]'
            # Build format expected by ts.madd()
            values = [(key,) + vals for vals in ts_data]

            # Record the key we were at. The write operation can take some time
            # (e.g for 1k records at once). In case of a resumal, we'll restart
            # on this key, potentially triggering 'Error at upsert' errors for
            # the already written values if the DB is in BLOCK mode. This is
            # deemed acceptable for now.
            sync_info.set(sync_info.FIELD_INTERVAL_KEY, key)
            sync_info.set(sync_info.FIELD_INTERVAL_KEY_IDX, interval_key_idx)
            sync_info.persist_settings_to_db([sync_info.FIELD_INTERVAL_KEY,
                                             sync_info.FIELD_INTERVAL_KEY_IDX])

            # Write down the values in the DB
            if len(values) != 0:
                logger.info(f'{hdr} {hdr2} Inserting {len(ts_data)} '
                            f'key records via ts.madd() for {key}')
                logger.debug(f'{hdr} {hdr2} ts.madd() args: {values}')
                replies = redis_cloud.redis.ts().madd(values)
                for r in replies:
                    check_redis_reply(key, r)

            else:
                logger.info(f'{hdr} Skipping entry for {key} as it is empty '
                            f'in interval {inter}')

            interval_key_idx += 1

        interval_idx += 1
        # Once resumption has been done (or there is no resumption possible), we
        # need to reset the key index counter for each interval or we'll either
        # miss values for the next intervals or index outside of the array
        if resumation_done or not sync_is_resumable:
            interval_key_idx = 0

    # Sync done, update markers
    sync_info.mark_sync_as_finished()
    return 0
