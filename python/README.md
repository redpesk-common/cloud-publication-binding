This is the Python version of the cloud-publication binding, leveraging
`libafb-binder` and `afb-libpython`.

# Features

## Time interval processing

The full datastore time interval (represented by the first and last timestamps
of all keys in the store) is split into intervals, whose length is defined by the
`time_interval_size` parameter in `config.yaml`. First, standard Redis and Redis
TimeSeries keys are synchronized between the two databases. Then each interval is
processed in turn, `TS.MRANGE()` being called to retrieve the values on one end
and `TS.MADD()` is leveraged to add them on the remote side.

## Resumption support

The synchronization engine supports full resumption if killed/stopped. This is
done by storing the current synchronization parameters (time interval width,
start, end, total count, current sync index, etc) into the Redis datastore and
re-reading them at startup to see if a sync was interrupted.

## Compaction support

Redis TimeSeries provides the ability to create sister time series that are
associated with a main time series and whose values are controlled by a
compaction rule. It is thus possible to create a compacted series whose values
are the average over the last 30 min of the values in the main time serie. This
is handled by the Redis datastore itself automatically if enabled.

The engine/binding provides the ability to leverage that and create the
compacted keys with a configurable rule. This allows to insert in the remote
datastore keys which are much lighter in memory to query (as they have fewer
values). For instance, a graphical interface can query those when quickly
skimming a dataset and then query the full resolution key for certain values.

# Building from source

The binding builds like any other binding. Since it makes use of
`afb-libpython`, make sure you have the proper Python environment setup for the
native part (C shared library) of `afb-libpython`. This is usually done by
building `afb-libpython` in a Python virtual environment and entering it when
starting the binding.
