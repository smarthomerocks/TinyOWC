#ifndef DS2423_h
#define DS2423_h

#include <DS2480B.h>
#include "onewire.h"

#define DS2423_PAGE_ONE 0xC0  // counter A
#define DS2423_PAGE_TWO 0xE0  // counter B
#define DS2423_READ_MEMORY_PLUS_COUNTERS_COMMAND 0xA5

/**
 * Return counter value from DS2423-device.
 * @returns lower 32-bit is counter value. -1 if reading failed
 */
int64_t getCounter(DS2480B &ds, onewireNode &node, uint8_t counterNr) {
  if (node.id[0] != DS2423) {
    ESP_LOGW(TAG, "Device in not a DS2423!");
    return -1;
  }

  if (counterNr > 1) {
    ESP_LOGW(TAG, "Only counters 0(A) and 1(B) is supported!");
    return -1;
  }

  if (ds.reset()) {      // onewire initialization sequence, to be followed by other commands
    ds.select(node.id);  // issues onewire "MATCH ROM" address which selects a SPECIFIC (only one) 1-Wire device

    uint8_t buf[45];

    buf[0] = DS2423_READ_MEMORY_PLUS_COUNTERS_COMMAND;
    buf[1] = counterNr == 0 ? DS2423_PAGE_ONE : DS2423_PAGE_TWO;
    buf[2] = 0x01;
    ds.write_bytes(buf, 3);

    for (int j = 3; j < 45; j++) {
      buf[j] = ds.read();
    }

    ds.reset();

    uint32_t count = (uint32_t)buf[38];
    for (int j = 37; j >= 35; j--) {
      count = (count << 8) + (uint32_t)buf[j];
    }
    uint16_t crc = ds.crc16(buf, 43);
    uint8_t *crcBytes = (uint8_t *)&crc;
    uint8_t crcLo = ~buf[43];
    uint8_t crcHi = ~buf[44];
    boolean error = (crcLo != crcBytes[0]) || (crcHi != crcBytes[1]);
///----- TODO: REMOVE
    for (int j = 0; j < 44; j++) {
      Serial.print(buf[j]);
      Serial.print(" ");
    }
    Serial.println();
    Serial.println();
    ESP_LOGI(TAG, "count: %d", count);
///----- TODO: REMOVE ^^^
    if (error) {
      ESP_LOGI(TAG, "CRC(%s) failure in getCounter() for DS2423.", String(buf[11], HEX));
      node.errors++;
      return -1;
    } else {
      return count;
    }
  } else {
    ESP_LOGW(TAG, "Reset DS2423 failed.");
    node.errors++;
    return -1;
  }
}

#endif