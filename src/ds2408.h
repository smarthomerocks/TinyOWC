//https://github.com/queezythegreat/arduino-ds2408/tree/master/DS2408
//https://datasheets.maximintegrated.com/en/ds/DS2408.pdf
#ifndef DS2408_h
#define DS2408_h

#include <DS2480B.h>
#include "onewire.h"

#define PIO_LOGIC_STATE_REGISTER 0x88
#define OUTPUT_LATCH_STATE_REGISTER 0x89
#define RESUME 0xA5
#define READ_PIO_REGISTERS 0xF0

int16_t setState(DS2480B &ds, onewireNode &node, uint8_t state) {
  if (node.id[0] != DS2408) {
    ESP_LOGW(TAG, "Device in not a DS2408!");

    return -1;
  }

  int16_t result = -1;

  if (ds.reset()) {      // onewire initialization sequence, to be followed by other commands
    //ds.select(addr);     // issues onewire "MATCH ROM" address which selects a SPECIFIC (only one) 1-Wire device

    ESP_LOGD(TAG, "Set DS2408 state to: %s.", String(state, BIN));
    // Configure RSTZ as STRB output.
    ds.write(SKIP_ROM);   // TODO: Select should be used istead of SKIP_ROM, but I don't get it working.
    ds.write(0xCC);  // Issue Write Conditional Search Register command
    ds.write(0x8D);  // TA1, target address = 8Dh
    ds.write(0x00);  // TA2, target address = 008Dh
    ds.write(0x04);  // Write byte to Control/Status Register

    // Verify configuration setting
    ds.reset();
    ds.write(SKIP_ROM);// Resume should be used istead of SKIP_ROM, but I don't get it working.
    ds.write(0xF0);  // Issue Read PIO Registers command
    ds.write(0x8D);  // TA1, target address = 8Dh
    ds.write(0x00);  // TA2, target address = 008Dh
    auto val = ds.read(); // Read Control/Status Register and verify
    ESP_LOGD(TAG, "DS2408 verify configuration setting: %s.", String(val, HEX));

    ds.reset();
    ds.write(SKIP_ROM); // Resume should be used istead of SKIP_ROM, but I don't get it working.
    ds.write(0x5A);   // Issue Channel-access Write command

    ds.write(state);  // Write byte to PIO
    ds.write(~state); // Write inverted byte to PIO
    val = ds.read();  // Read for verification (AAh = success)

    if (val == 0xAA) {  // AAh = success
      val = ds.read();  // DS2408 samples PIO pin status
      ESP_LOGD(TAG, "DS2408 current state: %s.", String(val, BIN));
      result = val;
    } else {
      node.errors++;
    }

    ds.reset();
  } else {
    ESP_LOGW(TAG, "Reset DS2408 failed.");
    node.errors++;
  }

  return result;
}

/**
 * Get current state of pins.
 * @return lower half (8-bit) of integer repressent the eight inputs, -1 is returned if we failed to read device.
 */
int16_t getState(DS2480B &ds, onewireNode &node) {
  if (node.id[0] != DS2408) {
    ESP_LOGW(TAG, "Device in not a DS2408!");

    return -1;
  }

  int16_t result = -1;

  if (ds.reset()) {      // onewire initialization sequence, to be followed by other commands
    //ds.select(addr);     // issues onewire "MATCH ROM" address which selects a SPECIFIC (only one) 1-Wire device
    ds.write(SKIP_ROM);   // TODO: Select should be used istead of SKIP_ROM, but I don't get it working.

    uint8_t buf[13];  // Put everything in the buffer so we can compute CRC easily.
    buf[0] = READ_PIO_REGISTERS;    // Read PIO Registers
    buf[1] = PIO_LOGIC_STATE_REGISTER;    // LSB address
    buf[2] = 0x00;    // MSB address
    ds.write_bytes(buf, 3);
    ds.read_bytes(buf + 3, 10);     // 3 cmd bytes, 6 data bytes, 2 0xFF, 2 CRC16
    ds.reset();

    if (!ds.check_crc16(buf, 11, &buf[11])) {
      ESP_LOGI(TAG, "CRC(%s) failure in getState() for DS2408.", String(buf[11], HEX));
      node.errors++;
    } else {
      result = buf[3];
    }
  } else {
    ESP_LOGW(TAG, "Reset DS2408 failed.");
    node.errors++;
  }

  return result;
}

#endif