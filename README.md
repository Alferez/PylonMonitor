# Pylontech Battery Monitor

The Pylontech Battery Monitor is connected via a serial connection to the "Console" port of the master battery. It automatically detects the number of installed batteries and reads their values. The battery information is stored and displayed on a local webpage and is also published to an MQTT broker. A setup page is used for MQTT configuration.

Based on https://github.com/dj0abr/pylonmonitor

## Supported Hardware:
Raspberry Pi: The recommended option for lowest power consumption is the Raspberry Pi Zero W,
but any other SBC is also compatible.

## Supported Serial Ports:
You can use the internal Raspberry Pi's Serial Port and an external Serial port (PL2303 / Cisco Serial Cable with adapter).

## Connection:
Connect the Console connector of the master battery to the primary serial interface.

## Install required libraries:
Run the script named `prepare` to install necessary libraries.

## Build the software:
make clean<br>
make<br>
This process will create the executable file named `pylonmonitor`.

## Install Auto-run Service:
Run the script named `install` to install the auto-run service.

## Run the Software Manually:
If you need to run it manually:
 * First, you need to stop the auto-run service: `sudo systemctl stop pylonmonitor.service`
 * Now you can run the software with root privileges by executing `sudo ./pylonmonitor`.

## Number of Pylontech Batteries:
The software automatically detects the number of connected Pylontech batteries.

## Using the pylontech battery monitor:
1. Via Web interface: Access the Raspberry Pi's IP address in a web browser.<br>
2. Click on "SETUP" at the top of the web interface to configure network settings and MQTT.<br>
3. Use tools like MQTT Explorer to verify that everything is working as expected.

## Web Interface:

![Screenshot of Pylontech Battery Monitor](pics/screenshot.png)

## Setting Page:
![Screenshot of Setting Page](pics/settings.png)

## Home Assistant Integration:

Compatible with Home Assistant Pylontech Battery Card ( https://github.com/jtubb/Pylontech-Battery-Card )

To integrate, you need to copy `homeassistant.yaml` to your Home Assistant configuration and import the file.
If you have more than 1 battery, you need to copy and replace "pack 1" with "pack X".

![Screenshot of Home Assistant](pics/homeassistant.jpg)

