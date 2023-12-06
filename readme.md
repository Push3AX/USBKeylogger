# USBKeylogger

Hardware-Based Keylogger

------

[**English**](https://github.com/Push3AX/USBKeylogger/blob/main/readme.md) | [中文](https://github.com/Push3AX/USBKeylogger/blob/main/readme_cn.md)

Hardware-Based Keylogger.

- Installed between the USB port and the keyboard, captures all keystrokes.
- Hardware-based, invisible to antivirus.
- Two hardware specifications for various implantation scenarios.
- Wi-Fi features for remote access.
- Built-in 3MB storage.

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/1.jpg" alt="1" style="zoom: 50%;" />



## Hardware Specifications

USBKeylogger is available in two hardware specifications:

1. Compact Model: Smaller in size, can be installed inside the keyboard (Example [here](https://github.com/ffffffff0x/1earn/blob/master/1earn/Security/IOT/硬件安全/HID/HID-KeyboardLogger.md)).

   <img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/V1.png" alt="V1"/> 

2. USB Hub Model: Built into a USB Hub (only the bottom port has keyboard logging functionality).

   <img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/V2.png" alt="V2"/> 



## Usage

Plug the USB Keyboard into the female USB port of the USBKeylogger, then plug the USBKeylogger into the computer.

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/1.jpg" alt="1" style="zoom: 50%;" />

Connect to the Wi-Fi hotspot of USBKeylogger, navigate to http://192.168.5.1/.

(Default SSID: USBKeylogger, default password: 123453678, can be changed in the Settings page. USBKeylogger can also be configured to connect to an existing Wi-Fi, see the Station Config)

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/web.jpg" alt="web"/>



## Manufacturing Guide

### Hardware Design

The hardware design of USBKeylogger is open-sourced on OSHWHUB:

- Compact Model: https://oshwhub.com/ant-project/USBKeylogger
- USB Hub Model: https://oshwhub.com/pusheax/usbkeylogger_v2_bak2

### Bill of Materials

#### Compact Model:

| Component ID   | Description     | Package Type        | Quantity |
| -------------- | --------------- | ------------------- | -------- |
| C7,C8          | 1uF Capacitor   | C0603               | 2        |
| C9,C10,C11,C12 | 100nF Capacitor | C0603               | 4        |
| MK2            | ESP-07S         | WIRELM-SMD_ESP-07S  | 1        |
| U4             | AMS1117-3.3     | SOT-223-4           | 1        |
| U5,U6          | CH9350L         | LQFP-48             | 2        |
| USB3           | To Keyboard     | USB-A-TH_USB-A-F-90 | 1        |
| USB4           | To Host         | USB-A-TH_AM90       | 1        |

#### USB Hub Model:

| Component ID                    | Description              | Package Type       | Quantity |
| ------------------------------- | ------------------------ | ------------------ | -------- |
| C1,C3,C5,C9,C10,C11,C13,C15     | 10uF Capacitors          | C0603              | 8        |
| C2,C4,C6,C8,C12,C14,C16,C18,C20 | 100nF Capacitors         | C0603              | 9        |
| C7,C17,C19                      | 10uF Tantalum Capacitors | CAP-SMD_L3.2-W1.6  | 3        |
| F1                              | ASMD1206-200 Fuse        | F1206              | 1        |
| MK1                             | ESP-07S                  | WIRELM-SMD_ESP-07S | 1        |
| U1                              | SL2.1A                   | SOP-16             | 11       |
| U2                              | AMS1117-3.3              | SOT-223-4          | 1        |
| U3,U4                           | CH9350L                  | LQFP-48            | 2        |
| USB1,USB2,USB3,USB4             | 916-351A1024Y10200       | USB-A-TH_USB-M-8   | 4        |
| X1                              | 12MHz                    | CRYSTAL-SMD        | 1        |
| N/A                             | USB Hub Case             | N/A                | 1        |

### Manufacturing Steps

Here are a brief steps to make your own USBKeylogger:

1. Download the Gerber files for the [Compact Model](https://github.com/Push3AX/USBKeylogger/releases/download/v1.1/Gerber_USBKeylogger_V2.zip) or the [USB Hub Model](https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/Hardware/V2/Gerber_USBKeylogger_v2.zip) of USBKeylogger and send them to a PCB manufacturer for production. There are no special manufacturing requirements for the Compact Model, but for the USB Hub Model, the board thickness of 1.2mm is recommended.
2. Download the firmware for the ESP-07S module [here](https://github.com/Push3AX/USBKeylogger/releases/download/v1.1/USBKeylogger.ino_v1.1.zip). Open it with Arduino and flash it onto the ESP-07S module using a programmer.
3. Refer to the BOM section and solder the components. When installing the Compact Version inside the keyboard, the USB connector can be omitted and instead soldered directly to the internal USB wires of the keyboard.
4. For the USB Hub Model, you also need to purchase a case, the specifications of which should meet the following requirements:

![case](https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/case.jpg)
