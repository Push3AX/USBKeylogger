# USBKeylogger

硬件键盘记录器

------

[**中文**](https://github.com/Push3AX/USBKeylogger/blob/main/readme_cn.md) | [English](https://github.com/Push3AX/USBKeylogger/blob/main/readme.md)

硬件键盘记录器。

- 安装在 USB 接口和键盘之间，记录键盘输入
- 纯硬件实现，反病毒软件无法识别
- 两款硬件规格，适配不同植入场景
- 带 Wi-Fi 功能，可开启 AP 或接入现有 Wi-Fi 网络
- 记录内容自动回传，AES-128 加密，支持 ntfy.sh 和自定义 HTTP
- 支持自动遍历开放 Wi-Fi 尝试联网
- 内置 3MB Flash 存储，写满自动循环写入
- 多国语言支持（English / 中文 / Русский）

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/1.jpg" alt="1" width="480" />

------



## 硬件规格

USBKeylogger 共有两种硬件规格。

1. 小型版本：体积较小，可安装在键盘内部（[此处](https://github.com/ffffffff0x/1earn/blob/master/1earn/Security/IOT/%E7%A1%AC%E4%BB%B6%E5%AE%89%E5%85%A8/HID/HID-KeyboardLogger.md)有案例）。

   <img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/V1.png" alt="V1" width="520" />

2. USB Hub 版本：内置在 USB Hub 中（仅有最下方接口有键盘记录功能）。

   <img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/V2.png" alt="V2" width="520" />

------



## 使用方法

将 USB 键盘插入 USBKeylogger，然后将 USBKeylogger 插入电脑。

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/1.jpg" alt="1" width="480" />

连接到 USBKeylogger 的 Wi-Fi 热点，访问 http://192.168.5.1/。

- 默认 SSID：`USBKeylogger_XXXXXX`（MAC 地址后6位），默认密码：`12345678`
- 登陆后请务必修改热点密码和管理页面密码。

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/Homepage.png" alt="Homepage" width="360" />

### 查看键盘记录

在首页点击「查看键盘记录」可在线查看，点击「下载键盘记录」可下载为文本文件。

每次启动，日志中会记录一个boot标记。若已连接互联网，还会通过NTP记录启动时间。

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/keylog.PNG" alt="Keylog" width="520" />

### 配置自动推送

USBKeylogger连接到有互联网的 Wi-Fi 网络后，可将键盘记录自动推送到指定服务器。推送内容经过AES-128加密，服务器只能收到密文。下载密文后，在本地使用解密工具和密钥解密。

#### 第一步：连接 Wi-Fi

进入**设置 → 通用**，在「连接到 Wi-Fi 路由器」中填写路由器的 SSID 和密码，保存后重启。设备将同时开启 AP 热点和 STA 连接。

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/settings.png" alt="Settings" width="360" />

#### 第二步：配置推送

进入**设置 → 键盘记录推送**，选择推送模式，推荐ntfy.sh

[ntfy.sh](https://ntfy.sh) 是一个开源的推送服务，无需注册账号即可使用。

1. 在「ntfy 服务器」填写服务器地址，默认为 `http://ntfy.sh`（免费公共服务器）

2. 「ntfy Topic」中会生成一个随机字符串作为频道名（也可手动修改）

3. 「AES密钥」中会生成一个32位的加密密钥，请务必妥善保存此密钥。

4. 点击生成的订阅链接即可查看推送历史。也请务必保存此链接。

5. 配置完成后点击「测试发送」，验证推送通道是否正常。

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/pushSettings.png" alt="Push Settings" width="360" />

> **注意**：ntfy.sh 公共服务器的推送消息仅保存 **3小时**，建议[自建 ntfy 服务器](https://docs.ntfy.sh/install/)并将服务器地址改为自建地址。

#### 第三步：配置推送计划

推送计划控制何时发送键盘记录，开机推送和定时推送相互独立，可以同时启用：

1. 开机推送：设备每次启动并连接 Wi-Fi 后，将完整键盘记录推送。默认开启。
2. 定时推送：设备运行期间，按设定的间隔（10–71580 分钟）定期将完整键盘记录历史推送一次。适合长期不重启的设备。

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/timeSettings.png" alt="Time Settings" width="360" />

#### 第四步：解密数据

访问生成的ntfy.sh订阅链接，下载附件。附件中的内容均为加密密文，格式为 `AES128CBC:<Base64>`。

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/ntfy.sh.png" alt="ntfy.sh" width="360" />

使用项目中的 **`KeylogDecrpyt.html`** 在本地浏览器解密。

1. 用浏览器打开 `KeylogDecrpyt.html`
2. 将设置页面中的 AES 密钥粘贴到「AES 密钥」栏
3. 将收到的加密内容粘贴到「加密载荷」栏
4. 点击「解密」，明文键盘记录将显示在下方

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/decrypt.png" alt="Decrypt" width="360" />

### 自动遍历开放热点

当设备已配置推送但 STA 网络不可用（或没有配置 STA）时，可开启此功能作为备用推送通道。

**工作流程：**

1. 设备检测到 STA 推送失败
2. 自动扫描附近所有无密码的开放 Wi-Fi 热点
3. 逐一连接，尝试通过每个热点连接到推送服务器
4. 推送成功后保存本次成功的热点，下次优先尝试该热点

------



## 制造指南

**警告：部分 CH9350L 芯片存在 Bug，表现为按下某按键 2 秒，会上传超过 2 秒，甚至不停的该键值，直到按下另一个键打断。这是 CH9350L 本身的 Bug，请更换芯片批次。**

### 硬件设计

USBKeylogger 的硬件设计开源在 OSHWHub，请移步至：

- 小型版本：https://oshwhub.com/ant-project/USBKeylogger
- USB Hub 版本：https://oshwhub.com/pusheax/usbkeylogger_v2_bak2



### BOM 与成本

#### 小型版本（总价约 45 元）：

| 元器件号       | 名称        | 封装类型            | 数量 | 总价（元） |
| -------------- | ----------- | ------------------- | ---- | ---------- |
| C7,C8          | 1uF 电容    | C0603               | 2    | 0.1        |
| C9,C10,C11,C12 | 100nF 电容  | C0603               | 4    | 0.1        |
| MK2            | ESP-07S     | WIRELM-SMD_ESP-07S  | 1    | 16         |
| U4             | AMS1117-3.3 | SOT-223-4           | 1    | 0.5        |
| U5,U6          | CH9350L     | LQFP-48             | 2    | 28         |
| USB3           | To Keyboard | USB-A-TH_USB-A-F-90 | 1    | 0.4        |
| USB4           | To Host     | USB-A-TH_AM90       | 1    | 0.4        |

#### USB Hub 版本（总价约 55 元）：

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
| N/A                             | USB Hub 外壳        | N/A                | 1    | 5          |



### 制造步骤

如果你希望自己制作 USBKeylogger，以下是简要步骤：

1. 下载 USBKeylogger [小型版本](https://github.com/Push3AX/USBKeylogger/releases/download/v1.1/Gerber_USBKeylogger_V1.zip)或 [USB Hub 版本](https://github.com/Push3AX/USBKeylogger/releases/download/v1.1/Gerber_USBKeylogger_V2.zip)的 Gerber 文件，发送给 PCB 制造商生产。小型版本的制造工艺无特殊要求，USB Hub 版本推荐板厚为 1.2mm。
2. 下载 ESP-07S 模块固件：[USBKeylogger.ino_v1.3.1.zip](https://github.com/Push3AX/USBKeylogger/releases/download/v1.3.1/USBKeylogger.ino_v1.3.1.zip)。也可以使用 Arduino IDE 打开 `USBKeylogger.ino/USBKeylogger/USBKeylogger.ino` 固件工程，按下方 Arduino 设置编译，并通过编程器烧录到 ESP-07S 模块。
3. 参照 BOM 章节，焊接各个元器件。将小型版本安装在键盘外壳中时，可以不焊接 USB 接头，而是直接焊接到键盘内部的 USB 线上（[此处](https://github.com/ffffffff0x/1earn/blob/master/1earn/Security/IOT/%E7%A1%AC%E4%BB%B6%E5%AE%89%E5%85%A8/HID/HID-KeyboardLogger.md)有案例）。
4. USB Hub 版本还需购买外壳，规格应满足如下要求：

<img src="https://raw.githubusercontent.com/Push3AX/USBKeylogger/main/images/case.png" alt="case" width="480" />

### Arduino 编译设置

固件目标模块为 ESP-07S / ESP8266。Arduino IDE 中先安装 `esp8266` 开发板包，然后选择 **Generic ESP8266 Module**。

需要安装的库：

- ESPAsyncTCP
- ESPAsyncWebServer
- ESPAsyncDNSServer

推荐的开发板菜单设置：

| 菜单项 | 设置 |
| --- | --- |
| Board | Generic ESP8266 Module |
| Upload Speed | 115200 |
| CPU Frequency | 80 MHz |
| Crystal Frequency | 26 MHz |
| Flash Size | 4MB (FS:3MB OTA:~512KB) |
| Flash Mode | DOUT (compatible) |
| Flash Frequency | 40MHz |
| Reset Method | no dtr (aka ck) |
| Debug port | Disabled |
| Debug Level | None |
| lwIP Variant | v2 Lower Memory |
| VTables | Flash |
| C++ Exceptions | Disabled |
| Stack Protection | Disabled |
| Erase Flash | 通常选 Only Sketch；需要清空旧配置时选 All Flash |
| Espressif FW | nonos-sdk 2.2.1+100 (190703) |
| SSL Support | Basic SSL ciphers (lower ROM use) |
| MMU | 32KB cache + 32KB IRAM (balanced) |
| Non-32-Bit Access | Use pgm_read macros for IRAM/PROGMEM |
| Builtin Led | 2 |



### 二次开发

如果需要二次开发，前端的代码位于 `USBKeylogger.ino/common_fixed.js`，包含 CSS 样式、i18n 字典和页面逻辑。

修改前端后，运行构建工具将 JS 压缩写入：

```bash
cd USBKeylogger.ino
python rebuild_html_h.py
```

该脚本会将 `common_fixed.js` gzip 压缩后生成 C 数组，写入 `USBKeylogger/html.h` 的 `JS_Common_GZ` 段。
