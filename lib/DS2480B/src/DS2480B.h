#ifndef DS2480B_h
#define DS2480B_h

#include <HardwareSerial.h>

// ROM commands
#define DATA_MODE 0xE1
#define COMMAND_MODE 0xE3
#define RESET 0xC1
#define SEARCH_ROM 0xF0
#define ALARM_SEARCH 0xEC
#define PULSE_TERMINATE 0xF1
#define SKIP_ROM 0xCC
#define MATCH_ROM 0x55
#define READ_POWER_SUPPLY 0xB4

// Supported 1-Wire devices
// https://github.com/owfs/owfs-doc/wiki/1Wire-Device-List
#define DS2405 0x5    // 1-channel switch
#define DS2406 0x12   // 2-channel switch
#define DS2413 0x3A   // 2-channel switch
#define DS2408 0x29   // 8-channel switch
#define DS18S20 0x10  // temperature sensor
#define DS1822 0x22   // temperature sensor
#define DS18B20 0x28  // temperature sensor
#define DS2423 0x1D   // dual counter
#define DS2450 0x20   // quad A/D converters

class DS2480B {
 private:
  HardwareSerial &port;
  bool isCmdMode;

  // global search state
  unsigned char ROM_NO[8];
  uint8_t LastDiscrepancy;
  uint8_t LastFamilyDiscrepancy;
  bool LastDeviceFlag;

  bool waitForReply();

 public:
  DS2480B(HardwareSerial &port);

  // Initialize DS2480B.
  void begin();

  // Perform a 1-Wire reset cycle. Returns 1 if a device responds
  // with a presence pulse.  Returns 0 if there is no device or the
  // bus is shorted or otherwise held low for more than 250uS
  uint8_t reset();

  void beginTransaction();
  void endTransaction();

  void commandMode();
  void dataMode();

  // Issue a 1-Wire rom select command, you do the reset first.
  void select(const uint8_t rom[8]);

  // Issue a 1-Wire rom skip command, to address all on bus.
  void skip();

  // Write a byte.
  void write(uint8_t v);

  void writeCmd(uint8_t v);

  void write_bytes(const uint8_t *buf, uint16_t count);

  // Read a byte.
  uint8_t read();

  void read_bytes(uint8_t *buf, uint16_t count);

  // Write a bit.
  uint8_t write_bit(uint8_t v);

  // Read a bit.
  uint8_t read_bit();

  // Check if device is parasite powered (no VCC)
  bool isParasitePowered(const uint8_t rom[8]);

  // Clear the search state so that if will start from the beginning again.
  void reset_search();

  // Setup the search to find the device type 'family_code' on the next call
  // to search(*newAddr) if it is present.
  void target_search(uint8_t family_code);

  // Look for the next device. Returns 1 if a new address has been
  // returned. A zero might mean that the bus is shorted, there are
  // no devices, or you have already retrieved all of them.  It
  // might be a good idea to check the CRC to make sure you didn't
  // get garbage.  The order is deterministic. You will always get
  // the same devices in the same order.
  bool search(uint8_t *newAddr);

  // Compute a Dallas Semiconductor 8 bit CRC, these are used in the
  // ROM and scratchpad registers.
  static uint8_t crc8(const uint8_t *addr, uint8_t len);

  // Compute the 1-Wire CRC16 and compare it against the received CRC.
  // Example usage (reading a DS2408):
  //    // Put everything in a buffer so we can compute the CRC easily.
  //    uint8_t buf[13];
  //    buf[0] = 0xF0;    // Read PIO Registers
  //    buf[1] = 0x88;    // LSB address
  //    buf[2] = 0x00;    // MSB address
  //    WriteBytes(net, buf, 3);    // Write 3 cmd bytes
  //    ReadBytes(net, buf+3, 10);  // Read 6 data bytes, 2 0xFF, 2 CRC16
  //    if (!CheckCRC16(buf, 11, &buf[11])) {
  //        // Handle error.
  //    }
  //
  // @param input - Array of bytes to checksum.
  // @param len - How many bytes to use.
  // @param inverted_crc - The two CRC16 bytes in the received data.
  //                       This should just point into the received data,
  //                       *not* at a 16-bit integer.
  // @param crc - The crc starting value (optional)
  // @return True, iff the CRC matches.
  static bool check_crc16(const uint8_t *input, uint16_t len,
                          const uint8_t *inverted_crc, uint16_t crc = 0);

  // Compute a Dallas Semiconductor 16 bit CRC.  This is required to check
  // the integrity of data received from many 1-Wire devices.  Note that the
  // CRC computed here is *not* what you'll get from the 1-Wire network,
  // for two reasons:
  //   1) The CRC is transmitted bitwise inverted.
  //   2) Depending on the endian-ness of your processor, the binary
  //      representation of the two-byte return value may have a different
  //      byte order than the two bytes you get from 1-Wire.
  // @param input - Array of bytes to checksum.
  // @param len - How many bytes to use.
  // @param crc - The crc starting value (optional)
  // @return The CRC16, as defined by Dallas Semiconductor.
  static uint16_t crc16(const uint8_t *input, uint16_t len, uint16_t crc = 0);
};

#endif