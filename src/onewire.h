#ifndef Onewire_h
#define Onewire_h

#include <Arduino.h>
#include <DS2480B.h>
#include <vector>

// just a value indicating the variable has no value.
#define UNSET_TEMPERATURE -1024

// number of read tries before giving up
#define MAX_CONSECUTIVE_RETRIES 3

struct onewireNode {
  uint8_t id[8];     // e.g. 28,EE,A8,9B,19,16,2,62
  uint8_t familyId;  // e.g. 28
  String idStr;      // e.g. "28.EEA89B19160262"
  String name;       // optional description, e.g. "bedroom", length is limited.
  float lowLimit = UNSET_TEMPERATURE;        // only applicable on temperature sensors.
  float highLimit = UNSET_TEMPERATURE;       // only applicable on temperature sensors.
  float temperature = UNSET_TEMPERATURE;     // only applicable on temperature sensors.
  float lastTemperature = UNSET_TEMPERATURE; // only applicable on temperature sensors.
  unsigned long lastOperation = 0;  // last time a operation (read/write) was made on the device (e.g. temperature was updated or pin was set)
  uint16_t failedReadingsInRow = 0; // only applicable on temperature sensors.
  uint32_t errors = 0;  // read/write errors for device (if many then check device and cables)
  uint32_t success = 0; // read/write success operations for device
  unsigned long millisWhenLastPush = 0; // keep track of how long since we reported status to MQTT-broker or InfluxDB
  uint8_t actuatorId[8] = {}; // e.g. 29,29,E1,3,0,0,0,9C, only applicable on temperature sensors.
  int8_t actuatorPin = -1;    // only applicable on temperature sensors.
  bool actuatorPinState[8] = {false, false, false, false, false, false, false, false}; // only applicable on DS2405, DS2406, DS2413 and DS2408 nodes.
  uint32_t counters[2] = {0, 0};  // only applicable on DS2423 nodes. Only external counters (A & B) exposed.
  char stateOverride = 'A'; // only applicable on temperature sensors with a actuatorPin set. '1' -> actuatorPin is always set to 1, '0' -> actuatorPin is always set to 0, 'A' (as in automatic) -> actuatorPin is set to 1 when temperature is below "lowLimit" and 0 then temperature is higher than highLimit.
};

std::vector<onewireNode> oneWireNodes;
std::vector<onewireNode> scannedOneWireNodes;

String familyIdToNameTranslation(uint8_t familyId) {
      switch (familyId) {
        case DS2405:
          return "DS2405";
        case DS2406:
          return "DS2406";
        case DS2413:
          return "DS2413";
        case DS2408:
          return "DS2408";
        case DS18S20:
          return "DS18S20";
        case DS18B20:
          return "DS18B20";
        case DS1822:
          return "DS1822";
        case DS2423:
          return "DS2423";
        case DS2450:
          return "DS2450";
        default:
          return "";
      }
}

/*
* Convert 1-wire address to string
* 28,EE,A8,9B,19,16,2,62 -> "28.EEA89B19160262"
*/
String idToString(const uint8_t addr[8]) {
  char dataString[50] = {0};
  snprintf(dataString, sizeof(dataString), "%02X.%02X%02X%02X%02X%02X%02X%02X",addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);

  return String(dataString);
}

/*
* Convert string to 1-wire byte array address
* "29.29E1030000009C" -> 29,29,E1,3,0,0,0,9C
*/
byte *stringToId(String str, byte *addr) {
  // safetycheck, 1-Wire addresses are always 17 chars long.
  if (str.length() == 17) {
    addr[0] = strtoul(str.substring(0, 2).c_str(), NULL, 16);
    addr[1] = strtoul(str.substring(3, 5).c_str(), NULL, 16);
    addr[2] = strtoul(str.substring(5, 7).c_str(), NULL, 16);
    addr[3] = strtoul(str.substring(7, 9).c_str(), NULL, 16);
    addr[4] = strtoul(str.substring(9, 11).c_str(), NULL, 16);
    addr[5] = strtoul(str.substring(11, 13).c_str(), NULL, 16);
    addr[6] = strtoul(str.substring(13, 15).c_str(), NULL, 16);
    addr[7] = strtoul(str.substring(15, 17).c_str(), NULL, 16);
  }

  return addr;
}

/*
* Populate the node-struct with information from the byte array (8 bytes long)
*/
void populateNode(onewireNode& node, const uint8_t addr[8]) {
  // the first ROM byte indicates which chip-family
  node.familyId = addr[0];
  node.idStr = idToString(addr);
  for (uint8_t i = 0; i < 8; i++) {
    node.id[i] = addr[i];
  }
}

onewireNode* getOneWireNode(const uint8_t addr[8]) {
    auto it = std::find_if (oneWireNodes.begin(), oneWireNodes.end(), [&addr](const onewireNode& n) {
      return 
        n.id[0] == addr[0] &&
        n.id[1] == addr[1] &&
        n.id[2] == addr[2] &&
        n.id[3] == addr[3] &&
        n.id[4] == addr[4] &&
        n.id[5] == addr[5] &&
        n.id[6] == addr[6] &&
        n.id[7] == addr[7];
    });

    if (it != oneWireNodes.end()) {
      return it.base();
    } else {
      return nullptr;
    }
}

#endif