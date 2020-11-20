Format: 1.0
Source: agl-service-modbus
Binary: agl-service-modbus-bin, agl-service-modbus-dev, agl-service-modbus-plugin-kingpigeon, agl-service-modbus-plugin-raymarine-anemometer
Architecture: any
Version: 1.0-0
Maintainer: ronan.lemartret <ronan.lemartret@iot.bzh>
Standards-Version: 3.8.2
Homepage: https://github.com/iotbzh/modbus-agl-binding
Build-Depends: debhelper (>= 5),
 pkg-config,
 dpkg-dev,
 cmake,
 agl-cmake-apps-module-bin,
 agl-app-framework-binder-bin,
 agl-app-framework-binder-dev,
 lua5.3,
 liblua5.3-dev,
 libjson-c-dev,
 libsystemd-dev,
 agl-libafb-helpers-dev,
 agl-libappcontroller-dev,
 libmodbus-dev (>= 3.1.6),
DEBTRANSFORM-RELEASE: 1
Files:
 agl-service-modbus-1.0.tar.gz
