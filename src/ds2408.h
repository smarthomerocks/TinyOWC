//https://github.com/queezythegreat/arduino-ds2408/tree/master/DS2408
//https://datasheets.maximintegrated.com/en/ds/DS2408.pdf
#ifndef DS2408_h
#define DS2408_h

#include <DS2480B.h>
#include "onewire.h"
#include "tinyowc.h"

#define PIO_LOGIC_STATE_REGISTER 0x88
#define OUTPUT_LATCH_STATE_REGISTER 0x89
#define RESUME 0xA5
#define READ_PIO_REGISTERS 0xF0

int16_t setState(DS2480B &ds, onewireNode &node, uint8_t state) {
  if (node.id[0] != DS2408) {
    ESP_LOGW(TAG, "Device is not a DS2408!");
    return -1;
  }

  if (ds.reset()) {      // onewire initialization sequence, to be followed by other commands
    ESP_LOGD(TAG, "Set DS2408 state to: %s.", String(state, BIN));
    
    uint8_t retries = MAX_CONSECUTIVE_RETRIES;
    //ds.select(node.id); // issues onewire "MATCH ROM" address which selects a SPECIFIC (only one) 1-Wire device
    ds.write(SKIP_ROM); // HACK, this select all devices on the bus, but we should select only this single device. Though I get CRC-errors using above line.

    do {
      ds.write(0x5A);   // Issue Channel-access Write command
      ds.write(state);  // Write byte to PIO
      ds.write(~state); // Write inverted byte to PIO
      auto status = ds.read();  // Read for verification (AAh = success)
      auto newState = ds.read();  // DS2408 samples PIO pin status

      if (status == 0xAA) {  // AAh = success
        ESP_LOGD(TAG, "DS2408 current state: %s.", String(newState, BIN));
        node.success++;
        return newState;
      } else {
        node.errors++;
        ESP_LOGW(TAG, "DS2408 setState failed, trying again...");

        if (ds.reset()) {
          ds.write(RESUME); // reselect last selected device.
        } else {
          ESP_LOGW(TAG, "Reset DS2408 failed after non-success setState.");
          node.errors++;
          return -1;
        }
      }
    } while (--retries);

    return -1;
  } else {
    ESP_LOGW(TAG, "Reset DS2408 failed.");
    node.errors++;
    return -1;
  }
}

/**
 * Get current state of pins.
 * @return lower half (8-bit) of integer repressent the eight inputs, -1 is returned if we failed to read device.
 */
int16_t getState(DS2480B &ds, onewireNode &node) {
  if (node.id[0] != DS2408) {
    ESP_LOGW(TAG, "Device is not a DS2408!");
    return -1;
  }

  if (ds.reset()) {      // onewire initialization sequence, to be followed by other commands
    //ds.select(node.id);  // issues onewire "MATCH ROM" address which selects a SPECIFIC (only one) 1-Wire device
    ds.write(SKIP_ROM); // HACK, this select all devices on the bus, but we should select only this single device. Though I get CRC-errors using above line.

    uint8_t retries = MAX_CONSECUTIVE_RETRIES;

    do {
      uint8_t buf[13];  // Put everything in the buffer so we can compute CRC easily.
      buf[0] = READ_PIO_REGISTERS;    // Read PIO Registers
      buf[1] = PIO_LOGIC_STATE_REGISTER;    // LSB address
      buf[2] = 0x00;    // MSB address
      ds.write_bytes(buf, 3);
      ds.read_bytes(buf + 3, 10);     // 3 cmd bytes, 6 data bytes, 2 0xFF, 2 CRC16

      if (!ds.check_crc16(buf, 11, &buf[11])) {
        node.errors++;
        ESP_LOGW(TAG, "CRC(%s) failure in getState() for DS2408, trying again...", String(buf[11], HEX));

        if (ds.reset()) {
          ds.write(RESUME); // reselect last selected device.
        } else {
          ESP_LOGW(TAG, "Reset DS2408 failed after non-success getState.");
          node.errors++;
          return -1;
        }
      } else {
        node.success++;
        return buf[3];
      }
    } while (--retries);
  } else {
    ESP_LOGW(TAG, "Reset DS2408 failed.");
    node.errors++;
  }

  return -1;
}

/**
 * Exit test-mode.
 * "The DS2408 is sensitive to the power-on slew rate and can inadvertently power up with a test mode
 * feature enabled. When this occurs, the P0 port does not respond to the Channel Access Write command."
 * @return 0=failed, 1=success
 */
bool existTestMode(DS2480B &ds, onewireNode &node) {
  // RST PD 96h <64-bit DS2408 ROM Code> 3Ch RST PD
  if (ds.reset()) {  // onewire initialization sequence, to be followed by other commands
    ds.write(0x96);
    for (uint8_t i = 0; i < 8; i++) {
      ds.write(node.id[i]);
    }
    ds.write(0x3C);

    if (ds.reset()) {
      return 1;
    }
  }

  return 0;
}

void ds2408_reset(DS2480B &ds, onewireNode &node) {
  ESP_LOGD(TAG, "Reset DS2408, id: %s.", node.idStr.c_str());
  
  if (existTestMode(ds, node)) {
    // Configure RSTZ as STRB output.
    //ds.select(node.id); // reselect last selected device.
    ds.write(SKIP_ROM); // HACK, this select all devices on the bus, but we should select only this single device. Though I get CRC-errors using above line.
    ds.write(0xCC);  // Issue Write Conditional Search Register command
    ds.write(0x8D);  // TA1, target address = 8Dh
    ds.write(0x00);  // TA2, target address = 008Dh
    ds.write(0x04);  // Write byte to Control/Status Register, RSTZ as STRB output

    // Verify configuration setting
    if (!ds.reset()) {
      ESP_LOGW(TAG, "Reset DS2408 failed after non-success configure RSTZ as STRB.");
      node.errors++;
      return;
    }

    ds.write(RESUME); // reselect last selected device.
    ds.write(0xF0);  // Issue Read PIO Registers command
    ds.write(0x8D);  // TA1, target address = 8Dh
    ds.write(0x00);  // TA2, target address = 008Dh
    auto status = ds.read(); // Read Control/Status Register and verify
    ESP_LOGD(TAG, "DS2408 verify configuration setting: %s.", String(status, HEX));

    // Set all relays off.
    setState(ds, node, B11111111);
  } else {
    ESP_LOGW(TAG, "Reset DS2408 failed for id: %s.", node.idStr.c_str());
  }
}
#endif