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
import redis
import textwrap

from sync import SyncInterval, SyncInfo
from utils import ts_to_str

logger = logging.getLogger('seanatic')


class RedisDB:
    """
    A class representing a Redis DB to synchronize.

    This is the toplevel structure that encapsulates a Redis DB (e.g. local or
    cloud/remote).
    """

    def __init__(self, desc, hostname, port, config, sync_support):
        self.redis = redis.Redis(hostname, port)
        self.desc = desc
        self.config = config

        self.parse_db_keys()

        if sync_support:
            (self.firstTs, self.lastTs) = self.get_sync_time_range()
            self.generate_sync_info()

    def parse_db_keys(self):
        """Iterate over the DB keys and build associated information."""

        cfg = self.config
        # These sets hold the TS and standard key names retrieved from the
        # actual DB
        self.keynames_ts = self.get_db_keynames(self.redis,
                                                cfg.sync_key_label_ts)
        self.keynames = self.get_db_keynames(self.redis,
                                             cfg.sync_key_label)

        # List of higher level key objects
        self.keyobjs_ts = self.key_objects_from_names(self.redis,
                                                      self.keynames_ts,
                                                      cfg.sync_key_label_ts,
                                                      'RedisTS')

        self.keyobjs = self.key_objects_from_names(self.redis,
                                                   self.keynames,
                                                   cfg.sync_key_label, 'Redis')

    def generate_sync_info(self):
        """Build the synchronization engine internal information."""

        cfg = self.config

        # List of computed sync intervals
        (self.intervals, self.intervals_total_cnt) = \
            self.generate_sync_intervals(self.firstTs, self.lastTs,
                                         cfg.time_interval_size,
                                         cfg.time_interval_nb,
                                         cfg.time_interval_start_idx)

        self.sync_info = SyncInfo(self.redis, self.firstTs, self.lastTs,
                                  self.intervals_total_cnt,
                                  cfg.time_interval_size)

    def get_db_keynames(self, r_handle, sync_key_label):
        """Retrieve the list of database keys names."""

        keys = r_handle.keys(sync_key_label + '.*')
        logger.info(f'{self.desc}: found {len(keys)} keys using'
                    f' {sync_key_label}.*')

        # Use a set to facilitate further operations. There cannot be duplicate
        # keys in Redis (TimeSeries or no) so the conversion is not an issue.
        return set(keys)

    def key_objects_from_names(self, r_handle, keynames, sync_key_label,
                               key_type):
        """Build key objects from a list of key names.

        This returns a dictionary of higher level objects, indexed by key names.
        Both standard and TimeSeries keys are supported.
        """

        keyobjs = {}
        for k in keynames:
            if key_type == 'RedisTS':
                k_info = r_handle.ts().info(k)
                key = RedisTSKey(k.decode(), k_info.first_timestamp,
                                 k_info.last_timestamp, k_info.total_samples,
                                 sync_key_label)
            else:
                k_value = r_handle.get(k)
                key = RedisKey(k.decode(), k_value.decode(), sync_key_label)

            keyobjs[k] = key

        total_nb_samples = 0
        for k in keyobjs.keys():
            logger.debug(keyobjs[k])
            if key_type == 'RedisTS':
                total_nb_samples += keyobjs[k].totalSamples

        if key_type == 'RedisTS':
            logger.info(f'{self.desc}: database contains {total_nb_samples} '
                        f'samples')
        return keyobjs

    def get_sync_time_range(self):
        """
        Get first and last timestamps for all TS keys.

        The sync range will be the earliest and last timestamp values across
        all keys. Sort them to find out.
        """

        keyobjs = self.keyobjs_ts
        if len(keyobjs) == 0:
            return (None, None)

        # Convert toplevel dictionary into a list so we can sort out the
        # objects
        keyobjs_l = [keyobjs[k] for k in keyobjs.keys()]

        firstTs = sorted(keyobjs_l, key=lambda x: x.firstTimeStamp)[0]
        lastTs = sorted(keyobjs_l, key=lambda x: x.lastTimeStamp)[-1]

        logger.info(f'{self.desc}: entry with earliest timestamp:\n{firstTs}')
        logger.info(f'{self.desc}: entry with last timestamp:\n{lastTs}')

        first = firstTs.firstTimeStamp
        last = lastTs.lastTimeStamp

        logger.info(
            f'{self.desc}: time range span: {ts_to_str(first)} to {ts_to_str(last)}')
        return (first, last)

    def dump_intervals(self, intervals):
        """Debugging aid to dump interval lists."""

        # Avoid iteration if loglevel is not DEBUG
        interval_cnt = len(intervals)
        if self.config.loglevel == logging.DEBUG:
            for idx, inter in enumerate(intervals):
                s = f'{idx}/{interval_cnt}: {inter}'
                logger.debug(s)

    def generate_sync_intervals(self, first, last, time_interval_size,
                                time_interval_nb, time_interval_start_idx):
        """Generate a list of intervals to synchronize on."""

        intervals = []
        if first is None or last is None:
            return intervals

        lower = first
        upper = first + time_interval_size
        # First insertion here, so we don't miss the last interval (insertion needs
        # to be done after range computation)
        i = SyncInterval(lower, upper)
        intervals.append(i)

        while upper < last:
            # Slide just a bit to have a different timestamp
            lower = upper + 1
            upper = upper + time_interval_size

            if upper >= last:
                # End of the loop, we will exit after that iteration
                upper = last

            i = SyncInterval(lower, upper)
            intervals.append(i)

        logger.info(
            f'{self.desc}: split range into {len(intervals)} intervals')
        logger.info(f'{self.desc}: first interval: {intervals[0]}')
        logger.info(f'{self.desc}: last interval: {intervals[-1]})')
        self.dump_intervals(intervals)

        nb_intervals_total = len(intervals)

        # Work interval selection for debugging purposes
        # -1 means to operate on all intervals
        if time_interval_nb == -1:
            interval_cnt = nb_intervals_total
        else:
            interval_cnt = time_interval_nb

        # Prevent out of bounds accesses
        if time_interval_start_idx >= nb_intervals_total:
            logger.warn(f'{self.desc}: requested start index '
                        f'({time_interval_start_idx}) greater than '
                        f'total interval count ({nb_intervals_total}). '
                        f'Starting at index 0 instead.')
            time_interval_start_idx = 0

        end_idx = time_interval_start_idx + interval_cnt
        work_intervals = intervals[time_interval_start_idx:end_idx]

        logger.info(f'Working on {interval_cnt} intervals, starting '
                    f'at index {time_interval_start_idx}')
        logger.info(f'{self.desc}: first work interval: {work_intervals[0]}')
        logger.info(f'{self.desc}: last work interval: {work_intervals[-1]})')
        self.dump_intervals(work_intervals)

        return (work_intervals, nb_intervals_total)


class RedisTSKey:
    """A class representing a Redis time series key object."""

    def __init__(self, keyname, firstTs, lastTs, totalSamples, sync_key_label):
        self.name = keyname
        self.name_short = keyname.replace(sync_key_label + '.', '')
        self.firstTimeStamp = firstTs
        self.lastTimeStamp = lastTs
        self.totalSamples = totalSamples

    def __str__(self):
        s = f"""Key    : {self.name_short} ({self.name})
samples: {self.totalSamples}
firstTS: {self.firstTimeStamp} - [{ts_to_str(self.firstTimeStamp)}]
lastTS : {self.lastTimeStamp} - [{ts_to_str(self.lastTimeStamp)}]"""

        # dedent() does not work when first line is not indented?
        return textwrap.dedent(s)


class RedisKey:
    """A class representing a Redis normal (i.e. non TimeSeries) key object."""

    def __init__(self, keyname, keyvalue, sync_key_label):
        self.name = keyname
        self.name_short = keyname.replace(sync_key_label + '.', '')
        self.value = keyvalue

    def __str__(self):
        s = f"""Key    : {self.name_short} ({self.name})
value  : {self.value}"""

        # dedent() does not work when first line is not indented?
        return textwrap.dedent(s)
