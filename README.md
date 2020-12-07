# Redis replication Binding

XXX: fill me in.

# TODOs

## Design

* Check if /start and /stop is OK or if we want autostart
* Settle for final name: either cloud-replication-binding or data-replication-binding
  
## Implementation

* Define dynamic configuration
  * Frequency for cloud upload
* Disconnection and reconnection to/from cloud
* Verbs
  * design /info
  * check if /help is still needed
* Connection
  * Connect to Thierry's binding (via require)
  * Add Thierry's ts_minsert() API

## FIXMES

* Investigate and remove XXX from codebase
* Restart does not work after stop

## Integration

* Port configuration
  * How to specific cloud side host + port since those are done at binder startup time?
* Container for host side
  * Insert OBS2 packages
  * Add Grafana
* Packaging via OBS2

## Testing

  * Test /help + /info w/ Gwen's app
  * Validate Grafana UI
  * Check disconnection works
  * Check records are there in the cloud DB after replication
  * Check start/stop + restart after stop