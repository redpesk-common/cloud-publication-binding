# redpesk® cloud publication binding configuration

You can find below the configuration obtained after compiling the cloud-publication binding and a brief descritpion of several concept introduced.

```json
{
    "$schema": "http://iot.bzh/download/public/schema/json/ctl-schema.json",
    "metadata": {
      "uid": "cloud-publication-svc",
      "version": "1.0",
      "api": "cloud-pub",
      "info": "Redpesk cloud publication service",
      "require":["redis-from-cloud", "redis"]
    },
    "cloud-publication": [{
      "publish_frequency_ms": "100",
      "sensors" : [
        {"class" : "sensor2"},
        {"class" : "my_third_sensor"}]
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
  "require":["redis-from-cloud", "redis"]
},
```
The metadata is the first block of the json configuration. It gathers basic statements regarding the binding.
In addition to the version and API name exposed by the binding, the `require` entry ensures that the APIs the binding depends are correctly present when it starts. Here, the binding uses two instances of the `redis-tsdb` binding, each running on one of the edge and cloud sides.

## 2. Cloud publication specifics
```json
"cloud-publication": [{
  "publish_frequency_ms": "100",
  "sensors" : [
	{"class" : "sensor2"},
	{"class" : "my_third_sensor"}]
}
```
In this section, both the publication frequency and which sensors to upload data for are defined.
