#!/usr/bin/env python

import redis
import datetime
import textwrap

class SeanaticTS:
    def __init__ (self, keyname, firstTs, lastTs):
        self.key = keyname
        self.firstTimeStamp = firstTs
        self.lastTimeStamp = lastTs

    def __str__ (self):
        s = f"""Key    : {self.key}
firstTS: {self.firstTimeStamp} - [{ts_to_str(self.firstTimeStamp)}] 
lastTS : {self.lastTimeStamp} - [{ts_to_str(self.lastTimeStamp)}]"""
    
        # dedent() does not work when first line is not indented?
        return textwrap.dedent(s)

def get_db_keys(r):
    return r.keys('SIEMENS_ET200SP.*')

def build_ts_info(r, keys):

    ts_info = []
    for k in keys:
        info = r.ts().info(k)
        series = SeanaticTS(k.decode(), info.first_time_stamp,
                info.lastTimeStamp)
        ts_info.append(series)

    return ts_info

def ts_to_str(ts):

    # Python datetime uses microseconds
    dt = datetime.datetime.fromtimestamp(ts / 1000)
    s = dt.strftime('%d %b %Y - %H:%M:%S %f')
    return s

if __name__ == '__main__':

    r = redis.Redis(host='localhost', port=6379, db=0)
    keys = get_db_keys(r)
    print (f'Found {len(keys)} keys')

    print (f'Getting info for {keys[0]}')
    key_info = r.ts().info(keys[0])

    seanatic_series = build_ts_info(r, keys)
    for s in seanatic_series:
        print(s)

    firstTs = sorted(seanatic_series, key=lambda x: x.firstTimeStamp)[0]
    lastTs = sorted(seanatic_series, key=lambda x: x.lastTimeStamp)[-1]

    print(f'First TS:\n{firstTs}')
    print(f'Last TS:\n{lastTs}')
