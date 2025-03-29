# IllumiNet: Web-Enabled Smart Lighting Control Platform

## Overview

IllumiNet is a smart lighting control system built on the ESP32 platform that enables remote control of multiple lights through a web interface. The system leverages the WiFi capabilities of ESP32 to create a lightweight web server that exposes RESTful API endpoints for controlling individual or groups of lights.

## Features

- Control up to 5 lights independently
- Simple and intuitive API endpoints
- Real-time status monitoring
- Control all lights simultaneously with a single command
- Low latency response times
- Cross-platform compatibility (control from any device with web access)

## Hardware Requirements

- ESP32 development board
- 5 LED lights or relays for controlling mains-powered lights
- Jumper wires
- Power supply for ESP32
- Breadboard (for prototyping)

## Software Requirements

- Arduino IDE
- ESP32 board support package
- WiFi network with internet access

## Pin Configuration

| Light Name | GPIO Pin |
| ---------- | -------- |
| Light 1    | GPIO 2   |
| Light 2    | GPIO 4   |
| Light 3    | GPIO 5   |
| Light 4    | GPIO 12  |
| Light 5    | GPIO 14  |

## Installation Instructions

1. Clone this repository or download the source code
2. Open the `IllumiNet.ino` file in Arduino IDE
3. Install required libraries if prompted
4. Update the WiFi credentials in the code:
   ```cpp
   const char* ssid = "YourWiFiName";
   const char* password = "YourWiFiPassword";
   ```
5. Connect your ESP32 board to your computer
6. Select the correct board and port in Arduino IDE
7. Upload the sketch to your ESP32 board
8. Open the Serial Monitor (baud rate: 115200) to view the assigned IP address

## Circuit Diagram

Connect each LED/relay to the corresponding GPIO pin as listed in the Pin Configuration section. Remember to use appropriate resistors for LEDs and transistors/drivers for relays.

## API Reference

### Get API Information

- **URL**: `/`
- **Method**: `GET`
- **Response**: Text information about available endpoints

### Control Individual Light

- **Turn On Light**

  - **URL**: `/lamp/on?id=X`
  - **Method**: `GET`
  - **URL Params**: `id=[0-4]` (Light ID)
  - **Response**: JSON containing status and confirmation message

- **Turn Off Light**

  - **URL**: `/lamp/off?id=X`
  - **Method**: `GET`
  - **URL Params**: `id=[0-4]` (Light ID)
  - **Response**: JSON containing status and confirmation message

- **Get Light Status**
  - **URL**: `/lamp/status?id=X`
  - **Method**: `GET`
  - **URL Params**: `id=[0-4]` (Light ID)
  - **Response**: JSON containing light status information

### Control All Lights

- **Turn On All Lights**

  - **URL**: `/lamp/all?action=on`
  - **Method**: `GET`
  - **Response**: JSON confirmation message

- **Turn Off All Lights**

  - **URL**: `/lamp/all?action=off`
  - **Method**: `GET`
  - **Response**: JSON confirmation message

- **Get All Lights Status**
  - **URL**: `/lamp/all/status`
  - **Method**: `GET`
  - **Response**: JSON array containing status information for all lights

## Example Requests

### Turn on Light 1

```
http://[esp32-ip-address]/lamp/on?id=0
```

### Turn off Light 3

```
http://[esp32-ip-address]/lamp/off?id=2
```

### Get status of all lights

```
http://[esp32-ip-address]/lamp/all/status
```

## Web Interface Integration

To control the lights from a web interface, you can create a simple HTML page with JavaScript that makes AJAX calls to the API endpoints. The ESP32 server includes CORS support, allowing you to host your web interface on a different domain.

## Troubleshooting

- If you can't connect to the ESP32, check your WiFi credentials
- Ensure all connections are secure and properly wired
- Verify that the ESP32 is receiving power
- Check the Serial Monitor for error messages
- Make sure your controlling device is on the same network as the ESP32

## Future Enhancements

- Add authentication for secure access
- Implement MQTT support for integration with smart home systems
- Create scheduling functionality
- Add support for dimming (PWM control)
- Develop a dedicated mobile application

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- ESP32 Community
- Arduino IDE developers
- All contributors to this project
