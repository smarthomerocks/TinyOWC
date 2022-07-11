#include <InfluxDbClient.h>
#include "onewire.h"
#include "tinyowc.h"

//#define INFLUXDB_CLIENT_DEBUG_ENABLE

InfluxDBClient influxDb;
bool isInfluxDbEnabled = false;

/**
 * Creates InfluxDB instance
 */
void initInfluxDB(const char *serverUrl, const char *dbname, const char *user, const char *passwd) {
  isInfluxDbEnabled = false;

  if (strlen(serverUrl) > 0 && strlen(dbname) > 0) {
    influxDb.setConnectionParamsV1(serverUrl, dbname, user, passwd);
    // We don't set timestamp on packets, because that would require a reliable timesource as NTP (which don't work well enough on ESP32).
    // Instead we have a short flush delay and let the server set the timestamp on receival. 
    influxDb.setWriteOptions(WritePrecision::NoTime, 8, 16, 5, true);

    if (influxDb.validateConnection()) {
      ESP_LOGI(TAG, "Connected to InfluxDB: %s", influxDb.getServerUrl().c_str());
      isInfluxDbEnabled = true;
    } else {
      ESP_LOGW(TAG, "InfluxDB connection failed: %s", influxDb.getLastErrorMessage().c_str());
    }
  }
}

void writeInfluxPoint(onewireNode& node) {
  if (isInfluxDbEnabled) {
    ESP_LOGD(TAG, "Writing node %s to InfluxDB queue.", node.idStr.c_str());

    if (isTemperatureSensor(node.familyId)) {
      Point sensor("temperature");
      sensor.addTag("device_id", node.idStr);

      if (node.name != NULL && node.name.length() > 0)
        sensor.addTag("device_name", node.name);

      sensor.addField("value", node.temperature);
      influxDb.writePoint(sensor);

    } else if (node.familyId == DS2408) {
      Point actuator("actuator");
      actuator.addTag("device_id", node.idStr);

      if (node.name != NULL && node.name.length() > 0)
        actuator.addTag("device_name", node.name);

      actuator.addField("pin0", node.actuatorPinState[0]);
      actuator.addField("pin1", node.actuatorPinState[1]);
      actuator.addField("pin2", node.actuatorPinState[2]);
      actuator.addField("pin3", node.actuatorPinState[3]);
      actuator.addField("pin4", node.actuatorPinState[4]);
      actuator.addField("pin5", node.actuatorPinState[5]);
      actuator.addField("pin6", node.actuatorPinState[6]);
      actuator.addField("pin7", node.actuatorPinState[7]);

      influxDb.writePoint(actuator);

    } else if (node.familyId == DS2406 || node.familyId == DS2413) {
      Point actuator("actuator");
      actuator.addTag("device_id", node.idStr);

      if (node.name != NULL && node.name.length() > 0)
        actuator.addTag("device_name", node.name);

      actuator.addField("pin0", node.actuatorPinState[0]);
      actuator.addField("pin1", node.actuatorPinState[1]);

      influxDb.writePoint(actuator);

    } else if (node.familyId == DS2405) {
      Point actuator("actuator");
      actuator.addTag("device_id", node.idStr);

      if (node.name != NULL && node.name.length() > 0)
        actuator.addTag("device_name", node.name);

      actuator.addField("pin0", node.actuatorPinState[0]);
      influxDb.writePoint(actuator);

    } else if (node.familyId == DS2423) {
      Point counter("counters");
      counter.addTag("device_id", node.idStr);

      if (node.name != NULL && node.name.length() > 0)
        counter.addTag("device_name", node.name);

      counter.addField("counter0", node.counters[0]);
      counter.addField("counter1", node.counters[1]);

      influxDb.writePoint(counter);
    }
  }
}

void writeWiFiSignalStrength(String appName) {
  if (isInfluxDbEnabled) {
    Point wifiPoint("wifi");
    wifiPoint.addTag("tinyowc", appName);
    wifiPoint.addField("rssi", WiFi.RSSI());
    influxDb.writePoint(wifiPoint);
  }
}

void flushInflux() {
  if (isInfluxDbEnabled && !influxDb.isBufferEmpty()) {
      // Write all remaining points to db
      influxDb.flushBuffer();
  }
}
