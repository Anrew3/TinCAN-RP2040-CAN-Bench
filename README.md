# TinCAN-CAN-Simulator

### What does "TinCAN" do?
  This project allows you to boot modules from modern vehicles such as the SYNC 3 and SYNC 4 modules on a "bench". This is done by simulating the canbus network of the vehicle using an inexpensive($20-30) RP2040 Canbus Board or ESP32 Board

### What's the point?
  At the time there is no existing project to power/boot SYNC 4 models and/or newer(2020+) modules. There also isn't a compact user friendly option to do so.

![Screenshot](docs/images/TinCANP4.png)

## How do I build this?
  - Gather the specified hardware, 3d print items, choose backplate/power config.
  - Read the [Assembly Guide](https://www.github.com) for a detailed instructions!
  - Use the Firmare Flashing Tool found below to flash the rp2040 firmware.

## Hardware List (Required)
  - Adafruit RP2040 Canbus Board [Adafruit_RP2040CAN](https://www.adafruit.com/product/5724) , [Mouser_RP2040CAN](https://mou.sr/3xuF0uN)
  - Adafruit MPM3610 21V -> 5V [Adafruit_MPM3610](https://www.adafruit.com/product/4739) , [Mouser_MPM3610](https://mou.sr/3Vck6YR)
  - Molex 6P Connector [Mouser_Molex6P](https://mou.sr/3JtCprp)
  - Barrel Jack [Amazon_BarrelJack](https://www.amazon.com)
  - CASE... 3D Print or CNC Aluminum [3D Print](https://github.com/Anrew3/TinCAN-RP2040-CAN-Simulator-Bench/hardware/enclosure/Carry_Tray_RP2040.3mf) , [CNC Aluminum](https://www.jlcpcb.com)
  - M2 & M4 Screws (4x M4x.7mmx15mm, 5x M2x.7mmx5mm)

## Hardware List (Optional)
  - JST Headers [Amazon_JSTHeaders](https://www.amazon.com)
  - ESP32 WIFI Addon [Adafruit_ESP32](https://www.adafruit.com/product/5395)

## Wiring (TINCAN)
![Screenshot](docs/images/TinCAN_Wiring.jpg)

## TinCAN Controller & Flasher

<div align="center">
  <a href="https://anrew3.github.io/TinCAN-CANBUS-Simulator/firmware/Controller/can-controller.html"
     target="_blank"
     rel="noopener noreferrer">
    <img src="https://img.shields.io/badge/🔧%20TinCAN%20Controller-Open%20App-blue?style=for-the-badge&logo=browser&logoColor=white"
         alt="Open TinCAN Controller">
  </a>
</div>

## WIRING HARNESS ADDONS
  - [SYNC3 (Coming Soon)](https://www.google.com)
  - [SYNC4 (Coming Soon)](https://www.google.com)
  - [IPC (Coming Soon)](https://www.google.com)
