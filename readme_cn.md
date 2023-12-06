# USBKeylogger

硬件键盘记录器

------

[**中文**](https://github.com/Push3AX/USBKeylogger/blob/main/readme_cn.md) | [English](https://github.com/Push3AX/USBKeylogger/blob/main/readme.md)

硬件键盘记录器。

- 安装在USB接口和键盘之间，记录键盘输入
- 通过硬件实现，反病毒软件无法识别
- 两个硬件规格，适配不同植入场景
- 带Wi-Fi功能，可远程控制
- 内置3MB存储空间

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/1.jpg" alt="1" style="zoom: 50%;" />



## 硬件规格

USBKeylogger共有两种硬件规格。

1. 小型版本：体积较小，可安装在键盘内部（[此处](https://github.com/ffffffff0x/1earn/blob/master/1earn/Security/IOT/%E7%A1%AC%E4%BB%B6%E5%AE%89%E5%85%A8/HID/HID-KeyboardLogger.md)有案例）。

   <img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/V1.png" alt="V1"/> 

2. USB Hub版本：内置在USB Hub中（仅有最下方接口有键盘记录功能）。

   <img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/V2.png" alt="V2"/> 



## 使用方法

将USB键盘插入USBKeylogger，然后将USBKeylogger插入电脑。

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/1.jpg" alt="1" style="zoom: 50%;" />

连接到USBKeylogger的Wi-Fi热点，访问http://192.168.5.1/。

（默认SSID：USBKeylogger，默认密码：123453678，在Settings页面可以修改。也可配置USBKeylogger连接到现有的Wi-Fi，即Station Config）

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/web.jpg" alt="web"/>



## 制造指南

### 硬件设计

USBKeylogger的硬件设计开源在OSHWHub，请移步至：

- 小型版本：https://oshwhub.com/ant-project/USBKeylogger
- USB Hub版本：https://oshwhub.com/pusheax/usbkeylogger_v2_bak2



### BOM与成本

#### 小型版本（总价约45元）：

| 元器件号       | 名称        | 封装类型            | 数量 | 总价（元） |
| -------------- | ----------- | ------------------- | ---- | ---------- |
| C7,C8          | 1uF 电容    | C0603               | 2    | 0.1        |
| C9,C10,C11,C12 | 100nF 电容  | C0603               | 4    | 0.1        |
| MK2            | ESP-07S     | WIRELM-SMD_ESP-07S  | 1    | 16         |
| U4             | AMS1117-3.3 | SOT-223-4           | 1    | 0.5        |
| U5,U6          | CH9350L     | LQFP-48             | 2    | 28         |
| USB3           | To Keyboard | USB-A-TH_USB-A-F-90 | 1    | 0.4        |
| USB4           | To Host     | USB-A-TH_AM90       | 1    | 0.4        |

#### USB Hub版本（总价约55元）：

| 元器件号                        | 名称                | 封装类型           | 数量 | 总价（元） |
| ------------------------------- | ------------------- | ------------------ | ---- | ---------- |
| C1,C3,C5,C9,C10,C11,C13,C15     | 10uF 电容           | C0603              | 8    | 0.1        |
| C2,C4,C6,C8,C12,C14,C16,C18,C20 | 100nF 电容          | C0603              | 9    | 0.1        |
| C7,C17,C19                      | 10uF 钽电容         | CAP-SMD_L3.2-W1.6  | 3    | 1.5        |
| F1                              | ASMD1206-200 保险丝 | F1206              | 1    | 0.3        |
| MK1                             | ESP-07S             | WIRELM-SMD_ESP-07S | 1    | 16         |
| U1                              | SL2.1A              | SOP-16             | 11   | 1.5        |
| U2                              | AMS1117-3.3         | SOT-223-4          | 1    | 0.5        |
| U3,U4                           | CH9350L             | LQFP-48            | 2    | 28         |
| USB1,USB2,USB3,USB4             | 916-351A1024Y10200  | USB-A-TH_USB-M-8   | 4    | 1.5        |
| X1                              | 12MHz               | CRYSTAL-SMD        | 1    | 0.5        |
| N/A                             | USB Hub外壳         | N/A                | 1    | 5          |



### 制造步骤

如果你希望自己制作USBKeylogger，以下是简要步骤：

1. 下载USBKeylogger[小型版本](https://github.com/Push3AX/USBKeylogger/releases/download/v1.1/Gerber_USBKeylogger_V1.zip)或[USB Hub版本](https://github.com/Push3AX/USBKeylogger/releases/download/v1.1/Gerber_USBKeylogger_V2.zip)的Gerber文件，发送给PCB制造商生产。小型版本的制造工艺无特殊要求，USB Hub版本推荐板厚为1.2mm。
2. 在 [此处](https://github.com/Push3AX/USBKeylogger/releases/download/v1.1/USBKeylogger.ino_v1.1.zip)下载ESP-07S模块的固件。使用Arduino打开，使用编程器将其烧录到ESP-07S模块。
3. 参照BOM章节，焊接各个元器件。将小型版本安装在键盘外壳中时，可以不焊接USB接头，而是直接焊接到键盘内部的USB线上。
4. USB Hub版本还需购买外壳，规格应满足如下要求：

![case](https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/case.jpg)
