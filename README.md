# departures-board [![License Badge](https://img.shields.io/badge/BY--NC--SA%204.0%20License-grey?style=flat&logo=creativecommons&logoColor=white)](https://creativecommons.org/licenses/by-nc-sa/4.0/)

This is an ESP32 based mini Departures Board replicating those at many UK railway stations (using data provided by National Rail's public API), London Underground Arrivals boards (using data provided by TfL) and UK wide bus stops (using data provided by bustimes.org). This implementation uses a 3.12" OLED display panel with SSD1322 display controller onboard. STL files are also provided for 3D printing the custom desktop case.

## Features
* All processing is done onboard by the ESP32 processor
* Smooth animation matching the real departures and arrivals boards
* Displays up to the next 9 departures with scheduled time, platform number, destination, calling stations and expected departure time
* Optionally only show services calling at a selected railway station
* Optionally display an alternate railway station between specific hours of the day
* Network Rail service messages
* Train information (operator, class, number of coaches etc.)
* Displays up to the next 9 arrivals with time to station (London Underground mode)
* TfL station and network service messages (London Underground mode)
* **New Bus mode** - Displays up to the next 9 departures with service number, destination, vehicle registration and schedule/expected time (requires **firmware v1.8 or above**)
* Fully-featured Web UI - choose any station on the UK network / London Tube & DLR network / UK Bus Stops
* Automatic firmware updates (optional)
* Displays the weather at the selected location (optional)
* STL files provided for custom 3D printed case

This short video demonstrates the Departures Board in action...
[![Departures Board Demo Video](https://github.com/user-attachments/assets/409b9a82-33a9-4351-ac87-f7e44ac56795)](https://youtu.be/N3pHk6yqwvo)

## Quick Start

### What you'll need

1. An ESP32 D1 Mini board (or clone) - either USB-C or Micro-USB version with CH9102 recommended. Cost around £2 from [AliExpress](https://www.aliexpress.com/item/1005005972627549.html).
2. A 3.12" 256x64 OLED Display Panel with SSD1322 display controller onboard. Cost around £8 from [AliExpress](https://www.aliexpress.com/item/1005008558326731.html).
3. A 3D printed case using the [STL](https://github.com/gadec-uk/departures-board/tree/main/stl) files provided. If you don't have a 3D printer, there are several services you can use to get one printed.
4. A National Rail Darwin Lite API token (these are free of charge - request one [here](https://realtime.nationalrail.co.uk/OpenLDBWSRegistration)).
5. Optionally, an OpenWeather Map API token to display weather conditions at the selected station (these are also free, sign-up for a free developer account [here](https://home.openweathermap.org/users/sign_up)).
6. Optionally, a TfL Open Data API token to display the London Underground/DLR Arrivals Board. These are free, sign-up for a free developer account [here](https://api-portal.tfl.gov.uk/signup)
7. Some intermediate soldering skills.

<img src="https://github.com/user-attachments/assets/5ae96896-62cc-4880-a3a8-79ac505e7605" align="center">

### Preparing the OLED display for 4-Wire SPI Mode

<img src="https://github.com/user-attachments/assets/cd176b57-ced6-486b-9a0d-9eee150dc813" align="right">
As supplied, the display is usually shipped with 8-bit 80XX mode enabled. This needs to be changed to 4-Wire SPI mode by removing one link and adding another (the image shows where to make these changes on the rear of the circuit board).

### Wiring Guide

Solder the 4 SPI connections, plus power and ground. The wires **MUST** be soldered to the **BACK** of the ESP32 Mini board (the side without the components) to enable it to sit in place in the case. You can solder directly to the pins on the OLED screen or for the best fit (if you are a more experienced solderer) de-solder and remove the header pins and solder directly to the board. You cannot use Dupont connectors, they will not fit the custom case design.

| OLED Pin | ESP32 Mini Pin |
|:---------|:-------------:|
| 1 VSS | GND |
| 2 VCC_IN | 3.3v |
| 4 D0/CLK | IO18 |
| 5 D1/DIN | IO23 |
| 14 D/C# | IO5 |
| 16 CS# | IO26 |

<img src="https://github.com/user-attachments/assets/a0ac50ab-f8d4-4528-ac38-7fb4c9ac0137" align="center">

### Installing the firmware

The project uses the Arduino framework and the ESP32 v3.2.0 core. If you want to build from source, you'll need [PlatformIO](https://platformio.org).

Alternatively, you can download pre-compiled firmware images from the [releases](https://github.com/gadec-uk/departures-board/releases). These can be installed over the USB serial connection using [esptool](https://github.com/espressif/esptool). If you have python installed, install with *pip install esptool*. For convenience, a pre-compiled executable version for Windows is included [here](https://github.com/gadec-uk/departures-board/tree/main/esptool).

If the board is not recognised you are probably using a version with the CP2104 USB-to-Serial chip. Drivers for the CP2104 are [here](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers?tab=downloads)

Attach the ESP32 Mini board via it's USB port and use the following command to flash the firmware:

```
esptool.py --chip esp32 --baud 460800 write_flash -z \
  0x1000 bootloader.bin \
  0xe000 boot_app0.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin
```

The tool should automatically find the correct serial port. If it fails to, you can manually specify the correct port by adding *--port COMx* (replace *COMx* with your actual port, e.g. COM3, /dev/ttyUSB0, etc.).

If using the pre-compiled esptool.exe version on Windows, save the esptool.exe and the four firmware (.bin) files to the same directory. Open a command prompt (Windows Key + R, type cmd and press enter) and change to the directory you saved the files into. Now type the following command on one line and press enter:
```
.\esptool --chip esp32 --baud 460800 write_flash -z 0x1000 bootloader.bin 0xe000 boot_app0.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

Subsequent updates can be carried out automatically over-the-air or you can manually update from the Web GUI.

### First time configuration

WiFiManager is used to setup the initial WiFi connection on first boot. The ESP32 will broadcast a temporary WiFi network named "Departures Board", connect to the network and follow the on-screen instuctions. You can also watch a video walkthrough of the entire process below.
[![Departures Board Setup Video](https://github.com/user-attachments/assets/176f0489-d846-42de-913f-eb838d9ab941)](https://youtu.be/bMyI56zwHyc)

Once the ESP32 has established an Internet connection, the next step is to enter your API keys (you must provide at least one of the following: a National Rail token or a Transport for London API key). Finally, select a station location. Start typing the location name and valid choices will be displayed as you type.

### Web GUI

At start-up, the ESP32's IP address is displayed. To change the station or to configure other miscellaneous settings, open the web page at that address. The settings available are:
- **Board Mode** - switch between National Rail Departures, London Underground Arrivals or UK Bus Stops modes (**firmware v1.8 or above**)
- **Station** - start typing a few characters of a station name and select from the drop-down station picker displayed (National Rail mode).
- **Only show services calling at** - filter services based on *calling at* location (National Rail mode - if you want to see the next trains *to* a particular station). Note this option cannot be combined with the *Alternate station* feature.
- **Alternate station** - automatically switch to displaying an alternate station between the hours set here (National Rail mode).
- **Underground Station** - start typing a few characters of an Underground or DLR station name and select from the drop-down station picker displayed (London Underground mode).
- **Bus Stop ATCO code** - Type the ATCO number of the bus stop you want to monitor (see [below](#bus-stop-atco-codes) for details).
- **Brightness** - adjusts the brightness of the OLED screen.
- **Show the date on screen** - displays the date in the upper-right corner (useful if you're also using this as a desk clock!)
- **Include Bus services** - optionally include bus replacement services (National Rail mode - shown with a small bus icon in place of platform number).
- **Include current weather at station location** - this option requires a valid OpenWeather Map API key (National Rail/Bus mode).
- **Increase API refresh rate** - Reduces the interval between data refreshes (National Rail mode). Uses more data and is not usually required.
- **Suppress calling at / information messages** - removes all horizontally scrolling text (much lower functionality but less distracting).
- **Enable automatic firmware updates at startup** - automatically checks for AND installs the latest firmware from this repository.
- **Enable overnight sleep mode (screensaver)** - if you're running the board 24/7, you can help prevent screen burn-in by enabling this option overnight.

A drop-down menu (top-right) adds the following options:
- **Check for Updates** - manually checks for and installs any updates to the firmware.
- **Edit API Keys** - view/edit your National Rail, OpenWeather Map and Transport for London API keys.
- **Clear WiFi Settings** - deletes the stored WiFi credentials and restarts in WiFiManager mode (useful to change WiFi network).
- **Restart System** - restarts the ESP32.

#### Other Web GUI Endpoints

A few other urls have been implemented, primarily for debugging/developer use:
- **/factoryreset** - deletes all configuration information, api keys and WiFi credentials. The entire setup process will need to be repeated.
- **/update** - for manual firmware updates. Download the latest binary from the [releases](https://github.com/gadec-uk/departures-board/releases). Only the **firmware.bin** file should be uploaded via */update*. The other .bin files are not used for upgrades. This method is *not* recommended for normal use.
- **/info** - displays some basic information about the current running state.
- **/formatffs** - formats the filing system, erasing the configuration & Web GUI (but not the WiFi credentials).
- **/dir** - displays a (basic) directory listing of the file system with the ability to view/delete files.
- **/upload** - upload a file to the file system.

### Bus Stop ATCO codes
Every UK bus stop has a unique ATCO code number. To find the ATCO code of the stop you want to monitor, go to [bustimes.org/search](https://bustimes.org/search) and type a location in the search box. Select the location from the list of places shown and then select the particular stop you want from the list. The ATCO code is shown on the stop information page. After entering the code in the Departures Board setup screen, tap the **Verify** button and the location will be shown confirming your selection. You must use the **Verify** button *before* you can save changes. Up to ten of the most recently verified ATCO codes are saved and can be selected from a dropdown list for quick access.

<img src="https://github.com/user-attachments/assets/8a41ec6d-5f15-4102-b3d5-c09260986319" align="center">

### Donating

<a href="https://buymeacoffee.com/gadec.uk"><img src="https://github.com/user-attachments/assets/e5960046-051a-45af-8730-e23d4725ab53" align="left" width="160" style="margin-right: 15px;" /></a>
This software is completely free for non-commercial use without obligation. If you would like to support me and encourage ongoing updates, you can [buy me a coffee!](https://buymeacoffee.com/gadec.uk)

### Licence
This work is licensed under **Creative Commons Attribution-NonCommercial-ShareAlike 4.0**. To view a copy of this licence, visit [https://creativecommons.org/licenses/by-nc-sa/4.0/](https://creativecommons.org/licenses/by-nc-sa/4.0/). Note: the terms of the licence prohibit commericial use of this work, this includes *any* reselling of the work in kit or assembled form for commercial gain.
