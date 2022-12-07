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
import os
import yaml

logger = logging.getLogger('seanatic')


class SeanaticConfig:
    """A class holding the in-memory representation of the configuration."""

    def __init__(self, cfg_yaml):

        self.verbosity = cfg_yaml['verbosity']

        if self.verbosity == 1:
            self.loglevel = logging.INFO
        elif self.verbosity >= 2:
            self.loglevel = logging.DEBUG

        self.redis_local_host = cfg_yaml['databases']['redis-local']['host']
        self.redis_local_port = cfg_yaml['databases']['redis-local']['port']
        self.redis_cloud_host = cfg_yaml['databases']['redis-cloud']['host']
        self.redis_cloud_port = cfg_yaml['databases']['redis-cloud']['port']
        self.sync_autostart = cfg_yaml['sync']['autostart']
        self.sync_db_poll_freq = cfg_yaml['sync']['db_poll_freq']
        self.time_interval_start_idx = cfg_yaml['sync']['time_interval_start_idx']
        self.time_interval_nb = cfg_yaml['sync']['time_interval_nb']
        self.time_interval_size = cfg_yaml['sync']['time_interval_size']
        self.sync_key_label_ts = cfg_yaml['sync']['key_label_ts']
        self.sync_key_label = cfg_yaml['sync']['key_label']
        self.compaction_enabled = cfg_yaml['sync']['compaction']['enabled']
        self.compaction_key_suffix = cfg_yaml['sync']['compaction']['key_suffix']
        self.bucket_duration = cfg_yaml['sync']['compaction']['bucket_duration']
        self.aggregator = cfg_yaml['sync']['compaction']['aggregator']

    def __str__(self):
        s = f'''Synchronization engine configuration:
verbosity                 : {self.verbosity}
redis_local               : host={self.redis_local_host} port={self.redis_local_port}
redis_cloud               : host={self.redis_cloud_host} port={self.redis_cloud_port}
db poll frequency         : {self.sync_db_poll_freq} secs
sync autostart            : {'enabled' if self.sync_autostart else 'disabled'}
time interval start index : {self.time_interval_start_idx}
time intervals to sync    : {self.time_interval_nb}
time interval size        : {self.time_interval_size}
compaction enabled        : {'yes' if self.compaction_enabled else 'no'}
compaction key suffix     : {self.compaction_key_suffix}
bucket duration           : {self.bucket_duration}
aggregator                : {self.aggregator}'''
        return s


def setup_config():
    """Setup the script configuration bits."""

    cfg_file = os.path.join(os.path.dirname(__file__), 'config.yaml')
    try:
        with open(cfg_file) as f:
            cfg_yaml = yaml.safe_load(f)
    except FileNotFoundError as e:
        # Logger not yet setup so we use standard print()
        print(f'Error: cannot find configuration file: {e}!')
        return None

    cfg = SeanaticConfig(cfg_yaml)
    logging.basicConfig(level=cfg.loglevel, format='%(message)s')
    logger.info(cfg)

    return cfg
