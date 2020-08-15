#ifndef DS18x20_h
#define DS18x20_h

#include <DS2480B.h>
#include "onewire.h"
// Code from https://community.particle.io/t/success-multiple-single-ds18b20-temp-sensors-on-a-single-onewire-bus/50318/17

// DS18x20 Function commands
#define CONVERT_T 0x44
#define READ_SCRATCHPAD 0xBE
#define WRITE_SCRATCHPAD 0x4E
#define COPY_SCRATCHPAD 0x48
#define RECALL 0xB8
// DS18x20 Scratchpad layout
#define TEMP_LSB        0
#define TEMP_MSB        1
#define HIGH_ALARM_TEMP 2
#define LOW_ALARM_TEMP  3
#define CONFIGURATION   4
#define INTERNAL_BYTE   5
#define COUNT_REMAIN    6
#define COUNT_PER_C     7
#define SCRATCHPAD_CRC  8

#define DS18B20_TEMP_HI_REG 0x55    //  set to a known value, checkerboard pattern (could be used to abort a "going to fail" crc check)
#define DS18B20_TEMP_LO_REG 0xAA    //  set to a known value, checkerboard pattern (ditto)

// DS18B20 resolution is determined by the byte written to it's configuration register
enum DS18B20_RESOLUTION : uint8_t {
  DS18B20_9BIT  = 0x1F,         //   9 bit   93.75 ms conversion time
  DS18B20_10BIT = 0x3F,         //  10 bit  187.50 ms conversion time
  DS18B20_11BIT = 0x5F,         //  11 bit  375.00 ms conversion time
  DS18B20_12BIT = 0x7F,         //  12 bit  750.00 ms conversion time
};

// if DS18B20 resolution is less than full 12-bit, the low bits of the data should be masked...
enum DS18B20_RES_MASK : uint8_t {
  DS18B20_9BIT_MASK  = 0xF8,        
  DS18B20_10BIT_MASK = 0xFC,      
  DS18B20_11BIT_MASK = 0xFE,        
  DS18B20_12BIT_MASK = 0xFF,       
};

// DS18B20 conversion time is ALSO determined by the byte written to it's configuration register
enum DS18B20_CONVERSION_TIME   : uint16_t {
  DS18B20_9BIT_TIME  = 94,          //   9 bit   93.75 ms conversion time w/pad
  DS18B20_10BIT_TIME = 188,         //  10 bit  187.50 ms conversion time w/pad
  DS18B20_11BIT_TIME = 375,         //  11 bit  375.00 ms conversion time w/pad
  DS18B20_12BIT_TIME = 750,         //  12 bit  750.00 ms conversion time w/pad
};

bool isConnected(DS2480B &ds, const uint8_t rom[8]) {
  if (ds.reset()) {     // onewire initialization sequence, to be followed by other commands
    ds.select(rom);     // issues onewire "MATCH ROM" address which selects a SPECIFIC device
    ds.write(READ_SCRATCHPAD); // onewire "READ SCRATCHPAD" command, to access selected DS18B20's scratchpad
  
    uint8_t data[9];
    bool allZeros = true;

    for (auto i = 0; i < 9; i++) {           // read whole scratchpad register
      data[i] = ds.read();
      // do more work at same time...
      if (data[i] != 0) {
        allZeros = false;
      }
    }

    if (allZeros) {
      return false;
    }

    auto crc = DS2480B::crc8(data, 8);
    if (crc != data[8]) {
      return false;
    }

    return true;

  } else {
    return false;
  }
}

// this function sets the resolution for ALL DS18B20s on an instantiated OneWire
void setResolution(DS2480B &ds, uint8_t resolution)  
{
  ds.reset();            // onewire intialization sequence, to be followed by other commands
  ds.write(SKIP_ROM);    // onewire "SKIP ROM" command, selects ALL DS18B20s on bus
  ds.write(WRITE_SCRATCHPAD); // onewire "WRITE SCRATCHPAD" command (requires write to 3 registers: 2 hi-lo regs, 1 config reg)
  ds.write(DS18B20_TEMP_HI_REG); // 1) write known value to temp hi register 
  ds.write(DS18B20_TEMP_LO_REG); // 2) write known value to temp lo register
  ds.write(resolution);  // 3) write the selected resolution to configuration registers of all DS18B20s on the bus
}

// this function intitalizes simultaneous temperature conversions for ALL DS18B20s on an instantiated OneWire
void startSimultaneousConversion(DS2480B &ds)    
{
  ds.reset();          // onewire initialization sequence, to be followed by other commands
  ds.write(SKIP_ROM);  // onewire "SKIP ROM" command, addresses ALL DS18B20s on bus
  ds.write(CONVERT_T); // onewire wire "CONVERT T" command, starts temperature conversion on ALL DS18B20s
}

void startConversion(DS2480B &ds, const uint8_t addr[8])    
{
  ds.reset();          // onewire initialization sequence, to be followed by other commands
  ds.select(addr);     // issues onewire "MATCH ROM" address which selects a SPECIFIC (only one) DS18B20 device
  ds.write(CONVERT_T); // onewire wire "CONVERT T" command, starts temperature conversion on ALL DS18B20s
}

// this function returns the RAW temperature conversion result of a SINGLE selected DS18B20 device (via it's address)
int16_t _readConversion(DS2480B &ds, const uint8_t addr[8]) {
  if (ds.reset()) {     // onewire initialization sequence, to be followed by other commands
    ds.select(addr);    // issues onewire "MATCH ROM" address which selects a SPECIFIC (only one) DS18B20 device
    ds.write(READ_SCRATCHPAD); // onewire "READ SCRATCHPAD" command, to access selected DS18B20's scratchpad
    
    byte data[9];

    for (auto i = 0; i < 9; i++) {           // we need 9 bytes
      data[i] = ds.read();
    //  Serial.print(data[i], HEX);
    //  Serial.print(" ");
    }

    //Serial.print(" CRC=");
    //Serial.print(DS2480B::crc8(data, 8), HEX);
    //Serial.print(":");
    //Serial.print(data[8], HEX);
    //Serial.println();
    auto crc = DS2480B::crc8(data, 8);
    if (crc != data[8]) {
      Serial.printf("Missmatched CRC=%X:%X.\n", crc, data[8]);
      return UNSET_TEMPERATURE;
    }
    
    // Convert the data to actual temperature
    // because the result is a 16 bit signed integer, it should
    // be stored to an "int16_t" type, which is always 16 bits
    // even when compiled on a 32 bit processor.
    int16_t raw = (data[1] << 8) | data[0];

    if (addr[0] == DS18S20) {  // old DS18S20 or DS1820
      raw = raw << 3; // 9 bit resolution default
      if (data[7] == 0x10) {
        // "count remain" gives full 12 bit resolution
        raw = (raw & 0xFFF0) + 12 - data[6];
      }
    } else {
      byte cfg = (data[4] & 0x60);
      // at lower res, the low bits are undefined, so let's zero them
      if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
      else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
      else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
      //// default is 12 bit resolution, 750 ms conversion time
    }

    return raw;
  } else {
    return UNSET_TEMPERATURE;
  }
}

// This function returns the RAW temperature conversion result of a SINGLE selected DS18B20 device (via it's address).
// Multiple retries are done in case of bus error or CRC missmatch.
// UNSET_TEMPERATURE is returned if no temperature could be read.
int16_t readConversion(DS2480B &ds, onewireNode &node) {
  uint8_t consecutiveReadTries = 0;
  int16_t temp = UNSET_TEMPERATURE;

  do {
    temp = _readConversion(ds, node.id);

    if (temp == UNSET_TEMPERATURE) {
      node.errors++;
    }
    consecutiveReadTries++;
  } while (temp == UNSET_TEMPERATURE && consecutiveReadTries <= MAX_CONSECUTIVE_RETRIES);

  return temp;
}

float rawToCelsius(int16_t raw) {
  return (float)raw / 16.0;
}

bool isTemperatureSensor(uint8_t familyId) {
  return familyId == DS1822 || familyId == DS18S20 || familyId == DS18B20;
}

#endif