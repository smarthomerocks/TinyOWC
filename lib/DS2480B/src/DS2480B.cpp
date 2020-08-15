#include "DS2480B.h"
//
// For DS2480B programming, see: https://pdfserv.maximintegrated.com/en/an/an192.pdf
//
DS2480B::DS2480B(HardwareSerial &port) : port(port) { 
  reset_search(); 
}

void DS2480B::begin() {
  // "Master Reset" the DS2480 (just like its been powercycled) by sending a NULL character at a data rate of 4800bps.
  // Sending a couple of 0x00 at baud 4800 makes it above the "Master Reset Time"(tMR) of 104us and thus forcing a reset.
  // We reset the DS2480 to get back to a known state after the ESP32 has been powercycled.
  port.updateBaudRate(4800);
  port.write(0x00);
  port.write(0x00);
  port.updateBaudRate(9600);

  isCmdMode = true;
  delay(1);
  // A 1-Wire Reset MUST be sent to calibrate the on-chip timing generator of the DS2480
  port.write(RESET);
}

// Perform the onewire reset function.  We will wait up to 250uS for
// the bus to come high, if it doesn't then it is broken or shorted
// and we return a 0;
//
// Returns 1 if a device asserted a presence pulse, 0 otherwise.
//
uint8_t DS2480B::reset() {
  commandMode();

  port.write(RESET);
  // proper return is 0xCD otherwise something was wrong
  while (!port.available());
  uint8_t r = port.read();
  if (r == 0xCD) return 1;
  return 0;
}

void DS2480B::dataMode() {
  if (isCmdMode) {
    port.write(DATA_MODE);
    isCmdMode = false;
  }
}

void DS2480B::commandMode() {
  if (!isCmdMode) {
    port.write(COMMAND_MODE);
    isCmdMode = true;
  }
}

void DS2480B::beginTransaction() { dataMode(); }

void DS2480B::endTransaction() { commandMode(); }

bool DS2480B::waitForReply() {
  for (uint16_t i = 0; i < 30000; i++) {
    if (port.available()) return true;
  }
  return false;
}

//
// Write a bit - actually returns the bit read back in case you care.
//
uint8_t DS2480B::write_bit(uint8_t v) {
  commandMode();
  if (v == 1)
    port.write(0x91);  // write a single "on" bit to onewire
  else
    port.write(0x81);  // write a single "off" bit to onewire
  if (!waitForReply()) return 0;

  uint8_t val = port.read();

  return val & 1;
}

//
// Read a bit - short hand for writing a 1 and seeing what we get back.
//
uint8_t DS2480B::read_bit() {
  uint8_t r = write_bit(1);
  return r;
}

//
// Write a byte.
void DS2480B::write(uint8_t v) {
  dataMode();

  port.write(v);
  // need to double up transmission if the sent byte was one of the command
  // bytes
  if (v == DATA_MODE || v == COMMAND_MODE || v == PULSE_TERMINATE)
    port.write(v);
  if (!waitForReply()) return;
  port.read();  // throw away reply
}

void DS2480B::writeCmd(uint8_t v) {
  commandMode();

  port.write(v);

  if (!waitForReply()) return;
  port.read();  // throw away reply
}

void DS2480B::write_bytes(const uint8_t *buf, uint16_t count) {
  for (uint16_t i = 0; i < count; i++) write(buf[i]);
}

//
// Read a byte
//
uint8_t DS2480B::read() {
  dataMode();

  port.write(0xFF);
  if (!waitForReply()) return 0;
  uint8_t r = port.read();
  return r;
}

void DS2480B::read_bytes(uint8_t *buf, uint16_t count) {
  for (uint16_t i = 0; i < count; i++) buf[i] = read();
}

//
// Do a ROM select
//
void DS2480B::select(const uint8_t rom[8]) {
  dataMode();
  write(MATCH_ROM);

  for (uint8_t i = 0; i < 8; i++) write(rom[i]);
}

//
// Do a ROM skip
//
void DS2480B::skip() {
  dataMode();
  write(SKIP_ROM);
}

// returns true if parasite mode is used (Data + GND) by device
// returns false if normal mode is used (VCC + Data + GND) by device
bool DS2480B::isParasitePowered(const uint8_t rom[8]) {
  // https://github.com/milesburton/Arduino-Temperature-Control-Library/blob/master/DallasTemperature.cpp#L231 "readPowerSupply()"
	bool parasiteMode = false;

	reset();
  select(rom);

	write(READ_POWER_SUPPLY);

	if (read_bit() == 0) {
		parasiteMode = true;
  }

  reset();

	return parasiteMode;
}

//
// You need to use this function to start a search again from the beginning.
// You do not need to do it for the first search, though you could.
//
void DS2480B::reset_search() {
  // reset the search state
  LastDiscrepancy = 0;
  LastDeviceFlag = false;
  LastFamilyDiscrepancy = 0;
  for (int i = 7;; i--) {
    ROM_NO[i] = 0;
    if (i == 0) break;
  }
}

// Setup the search to find the device type 'family_code' on the next call
// to search(*newAddr) if it is present.
//
void DS2480B::target_search(uint8_t family_code) {
  // set the search state to find SearchFamily type devices
  ROM_NO[0] = family_code;
  for (uint8_t i = 1; i < 8; i++) ROM_NO[i] = 0;
  LastDiscrepancy = 64;
  LastFamilyDiscrepancy = 0;
  LastDeviceFlag = false;
}

//
// Perform a search. If this function returns a '1' then it has
// enumerated the next device and you may retrieve the ROM from the
// DS2480B::address variable. If there are no devices, no further
// devices, or something horrible happens in the middle of the
// enumeration then a 0 is returned.  If a new device is found then
// its address is copied to newAddr.  Use DS2480B::reset_search() to
// start over.
//
// --- Replaced by the one from the Dallas Semiconductor web site ---
//--------------------------------------------------------------------------
// Perform the 1-Wire Search Algorithm on the 1-Wire bus using the existing
// search state.
// Return true  : device found, ROM number in ROM_NO buffer
//        false : device not found, end of search
//
// TODO: see alternative way with search accelerator: https://os.mbed.com/users/giorgos2405/code/DS2480B//file/07f0cd3758fa/DS2480B.cpp/
bool DS2480B::search(uint8_t *newAddr) {
  uint8_t id_bit_number;
  uint8_t last_zero, rom_byte_number;
  uint8_t id_bit, cmp_id_bit;
  bool search_result;

  unsigned char rom_byte_mask, search_direction;

  // initialize for search
  id_bit_number = 1;
  last_zero = 0;
  rom_byte_number = 0;
  rom_byte_mask = 1;
  search_result = false;

  // if the last call was not the last one
  if (!LastDeviceFlag) {
    // 1-Wire reset
    if (!reset()) {
      // reset the search
      LastDiscrepancy = 0;
      LastDeviceFlag = false;
      LastFamilyDiscrepancy = 0;
      return false;
    }

    // issue the search command
    write(SEARCH_ROM);

    // loop to do the search
    do {
      // read a bit and its complement
      id_bit = read_bit();
      cmp_id_bit = read_bit();

      // check for no devices on 1-wire
      if ((id_bit == 1) && (cmp_id_bit == 1))
        break;
      else {
        // all devices coupled have 0 or 1
        if (id_bit != cmp_id_bit)
          search_direction = id_bit;  // bit write value for search
        else {
          // if this discrepancy if before the Last Discrepancy
          // on a previous next then pick the same as last time
          if (id_bit_number < LastDiscrepancy)
            search_direction = ((ROM_NO[rom_byte_number] & rom_byte_mask) > 0);
          else
            // if equal to last pick 1, if not then pick 0
            search_direction = (id_bit_number == LastDiscrepancy);

          // if 0 was picked then record its position in LastZero
          if (search_direction == 0) {
            last_zero = id_bit_number;

            // check for Last discrepancy in family
            if (last_zero < 9) LastFamilyDiscrepancy = last_zero;
          }
        }

        // set or clear the bit in the ROM byte rom_byte_number
        // with mask rom_byte_mask
        if (search_direction == 1)
          ROM_NO[rom_byte_number] |= rom_byte_mask;
        else
          ROM_NO[rom_byte_number] &= ~rom_byte_mask;

        // serial number search direction write bit
        write_bit(search_direction);

        // increment the byte counter id_bit_number
        // and shift the mask rom_byte_mask
        id_bit_number++;
        rom_byte_mask <<= 1;

        // if the mask is 0 then go to new SerialNum byte rom_byte_number and
        // reset mask
        if (rom_byte_mask == 0) {
          rom_byte_number++;
          rom_byte_mask = 1;
        }
      }
    } while (rom_byte_number < 8);  // loop until through all ROM bytes 0-7

    // if the search was successful then
    if (!(id_bit_number < 65)) {
      // search successful so set LastDiscrepancy,LastDeviceFlag,search_result
      LastDiscrepancy = last_zero;

      // check for last device
      if (LastDiscrepancy == 0) LastDeviceFlag = true;

      search_result = true;
    }
  }

  // if no device found then reset counters so next 'search' will be like a
  // first
  if (!search_result || !ROM_NO[0]) {
    LastDiscrepancy = 0;
    LastDeviceFlag = false;
    LastFamilyDiscrepancy = 0;
    search_result = false;
  }
  for (int i = 0; i < 8; i++) newAddr[i] = ROM_NO[i];
  return search_result;
}

// The 1-Wire CRC scheme is described in Maxim Application Note 27:
// "Understanding and Using Cyclic Redundancy Checks with Maxim iButton
// Products"
//

// This table comes from Dallas sample code where it is freely reusable,
// though Copyright (C) 2000 Dallas Semiconductor Corporation
static const uint8_t PROGMEM dscrc_table[] = {
    0,   94,  188, 226, 97,  63,  221, 131, 194, 156, 126, 32,  163, 253, 31,
    65,  157, 195, 33,  127, 252, 162, 64,  30,  95,  1,   227, 189, 62,  96,
    130, 220, 35,  125, 159, 193, 66,  28,  254, 160, 225, 191, 93,  3,   128,
    222, 60,  98,  190, 224, 2,   92,  223, 129, 99,  61,  124, 34,  192, 158,
    29,  67,  161, 255, 70,  24,  250, 164, 39,  121, 155, 197, 132, 218, 56,
    102, 229, 187, 89,  7,   219, 133, 103, 57,  186, 228, 6,   88,  25,  71,
    165, 251, 120, 38,  196, 154, 101, 59,  217, 135, 4,   90,  184, 230, 167,
    249, 27,  69,  198, 152, 122, 36,  248, 166, 68,  26,  153, 199, 37,  123,
    58,  100, 134, 216, 91,  5,   231, 185, 140, 210, 48,  110, 237, 179, 81,
    15,  78,  16,  242, 172, 47,  113, 147, 205, 17,  79,  173, 243, 112, 46,
    204, 146, 211, 141, 111, 49,  178, 236, 14,  80,  175, 241, 19,  77,  206,
    144, 114, 44,  109, 51,  209, 143, 12,  82,  176, 238, 50,  108, 142, 208,
    83,  13,  239, 177, 240, 174, 76,  18,  145, 207, 45,  115, 202, 148, 118,
    40,  171, 245, 23,  73,  8,   86,  180, 234, 105, 55,  213, 139, 87,  9,
    235, 181, 54,  104, 138, 212, 149, 203, 41,  119, 244, 170, 72,  22,  233,
    183, 85,  11,  136, 214, 52,  106, 43,  117, 151, 201, 74,  20,  246, 168,
    116, 42,  200, 150, 21,  75,  169, 247, 182, 232, 10,  84,  215, 137, 107,
    53};

//
// Compute a Dallas Semiconductor 8 bit CRC. These show up in the ROM
// and the registers.  (note: this might better be done without to
// table, it would probably be smaller and certainly fast enough
// compared to all those delayMicrosecond() calls.  But I got
// confused, so I use this table from the examples.)
//
uint8_t DS2480B::crc8(const uint8_t *addr, uint8_t len) {
  uint8_t crc = 0;

  while (len--) {
    crc = pgm_read_byte(dscrc_table + (crc ^ *addr++));
  }
  return crc;
}

bool DS2480B::check_crc16(const uint8_t *input, uint16_t len,
                          const uint8_t *inverted_crc, uint16_t crc) {
  crc = ~crc16(input, len, crc);
  return (crc & 0xFF) == inverted_crc[0] && (crc >> 8) == inverted_crc[1];
}

uint16_t DS2480B::crc16(const uint8_t *input, uint16_t len, uint16_t crc) {
  static const uint8_t oddparity[16] = {0, 1, 1, 0, 1, 0, 0, 1,
                                        1, 0, 0, 1, 0, 1, 1, 0};

  for (uint16_t i = 0; i < len; i++) {
    // Even though we're just copying a byte from the input,
    // we'll be doing 16-bit computation with it.
    uint16_t cdata = input[i];
    cdata = (cdata ^ crc) & 0xff;
    crc >>= 8;

    if (oddparity[cdata & 0x0F] ^ oddparity[cdata >> 4]) crc ^= 0xC001;

    cdata <<= 6;
    crc ^= cdata;
    cdata <<= 1;
    crc ^= cdata;
  }
  return crc;
}
