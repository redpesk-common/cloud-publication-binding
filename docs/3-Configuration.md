# redpesk® cloud publication binding configuration

You can find below information about the binding configuration and how to change
it.

```json
{
    "$schema": "http://iot.bzh/download/public/schema/json/ctl-schema.json",
    "metadata": {
      "uid": "cloud-publication-svc",
      "version": "1.0",
      "api": "cloud-pub",
      "info": "Redpesk cloud publication service",
      "require":["redis-cloud", "redis"]
    },
    "cloud-publication": [{
      "publish_frequency_ms": "100",
      "sensors" : [
        {"class" : "WIRED_WIND_WS310"},
        {"class" : "my_second_sensor"}]
    }
    ]
}
```

## 1. Metadata

```json
"metadata": {
  "uid": "cloud-publication-svc",
  "version": "1.0",
  "api": "cloud-pub",
  "info": "Redpesk cloud publication service",
  "require":["redis-cloud", "redis"]
}
```
The metadata is the first block of the JSON configuration. It gathers basic
statements regarding the binding.

In addition to the version and API name exposed by the binding, the `require`
entry ensures that the APIs the binding depends are correctly present when it
starts. Here, the binding uses two instances of the `redis-tsdb` binding, each
running on one of the edge (`redis`) and cloud (`redis-cloud`) sides.

## 2. Cloud publication specifics
```json
"cloud-publication": [{
  "publish_frequency_ms": "100",
  "sensors" : [
	{"class" : "WIRED_WIND_WS310"},
	{"class" : "my_second_sensor"}]
}
```
In this section, both the publication frequency and which sensors to upload data
for are defined. 

In the example above, sensor data is uploaded every 100ms for sensors named
`WIRED_WIND_WS310` and `my_second_sensor`. Please check the signal composer
binding documentation for how to determine actual sensor names to use here
depending on your hardware.
