# TINY-OWC

## Introduction

Firmware for ESP32 microcontrollers for monitoring and controlling external sensors, primarily the Maxim 1-Wire family.

## Build and upload software

To simplify building this software, with all its dependencies, we use the [Platform.io](https://platformio.org/) open source ecosystem. Make sure you have Platform.io installed on your computer before you proceed.

### Compile and upload the firmware

Connect a micro-USB cable between your computer and the ESP32 microcontroller, run the following commands in the root folder of this project to compile and upload the software to the ESP32:

```
  platformio run -t buildfs
  platformio run -t uploadfs
  platformio run -t upload
```

If your computer is stuck waiting on the following line:

```
  Serial port /dev/ttyUSB0
```

and eventually timing out, then you need to press the "flash"-button on the ESP32 for 2-3 seconds when waiting on those lines to initialize the flashing-process!

## License & Author

- Author: Henrik Östman

```
MIT License

Copyright (c) 2020 Henrik Östman

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE
```
