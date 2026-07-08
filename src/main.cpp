#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

#if __has_include("config.h")
#include "config.h"
#else
#error "Missing include/config.h. Copy include/config.example.h to include/config.h and fill Wi-Fi settings."
#endif

#include "jjy_encoder.h"

#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 20000
#endif

#ifndef NTP_SYNC_TIMEOUT_MS
#define NTP_SYNC_TIMEOUT_MS 30000
#endif

#ifndef UI_WEB_PORT
#define UI_WEB_PORT 80
#endif

#ifndef UI_AP_ALWAYS_ON
#define UI_AP_ALWAYS_ON 1
#endif

#ifndef UI_AP_SSID
#define UI_AP_SSID "JJY-SETUP"
#endif

#ifndef UI_AP_PASSWORD
#define UI_AP_PASSWORD "12345678"
#endif

#ifndef DEFAULT_OUTPUT_ENABLED
#define DEFAULT_OUTPUT_ENABLED 1
#endif

struct RuntimeSettings {
  String staSsid;
  String staPassword;
  String timeZone;
  uint32_t carrierFrequencyHz;
  bool outputInverted;
  bool outputEnabled;
};

static RuntimeSettings g_settings;
static WebServer g_server(UI_WEB_PORT);
static bool g_serverStarted = false;
static bool g_carrierOn = false;
static bool g_timeSynced = false;
static int g_lastLoggedSecond = -1;
static bool g_printedCarrierTest = false;
static uint32_t g_lastWifiAttemptMs = 0;
static uint32_t g_lastNtpAttemptMs = 0;

static bool isPlaceholderCredential(const String& value) {
  return value.length() == 0 || value == "your-wifi-ssid" || value == "your-wifi-password";
}

static String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

static String boolText(bool value) {
  return value ? "ON" : "OFF";
}

static uint32_t carrierDutyValue() {
  const uint32_t maxDuty = (1UL << LEDC_RESOLUTION_BITS) - 1UL;
  return (maxDuty * CARRIER_DUTY_PERCENT) / 100UL;
}

static void setLed(bool on) {
#if LED_PIN >= 0
  digitalWrite(LED_PIN, LED_ACTIVE_LOW ? !on : on);
#else
  (void)on;
#endif
}

static void carrierWrite(bool on) {
  const uint32_t duty = on ? carrierDutyValue() : 0;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(CARRIER_PIN, duty);
#else
  ledcWrite(LEDC_CHANNEL, duty);
#endif

  g_carrierOn = on;
}

static void carrierOn() {
  carrierWrite(true);
}

static void carrierOff() {
  carrierWrite(false);
}

static bool configureCarrierPwm() {
  pinMode(CARRIER_PIN, OUTPUT);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  static bool attached = false;
  if (attached) {
    ledcDetach(CARRIER_PIN);
  }

  bool ok = ledcAttach(CARRIER_PIN, g_settings.carrierFrequencyHz, LEDC_RESOLUTION_BITS);
  if (!ok) {
    Serial.println("[PWM] ledcAttach failed");
    return false;
  }
  attached = true;
  Serial.printf("[PWM] requested=%lu Hz, pin=%d, resolution=%d bits\n",
                (unsigned long)g_settings.carrierFrequencyHz,
                CARRIER_PIN,
                LEDC_RESOLUTION_BITS);
#else
  double actualFreq = ledcSetup(LEDC_CHANNEL, g_settings.carrierFrequencyHz, LEDC_RESOLUTION_BITS);
  ledcAttachPin(CARRIER_PIN, LEDC_CHANNEL);
  Serial.printf("[PWM] requested=%lu Hz, actual=%.2f Hz, channel=%d, resolution=%d bits\n",
                (unsigned long)g_settings.carrierFrequencyHz,
                actualFreq,
                LEDC_CHANNEL,
                LEDC_RESOLUTION_BITS);
#endif

  carrierOff();
  return true;
}

static void applyTimeZone() {
  setenv("TZ", g_settings.timeZone.c_str(), 1);
  tzset();
}

static void loadSettings() {
  Preferences prefs;
  prefs.begin("jjy", true);

  const String defaultSsid = String(WIFI_SSID);
  const String defaultPassword = String(WIFI_PASSWORD);

  g_settings.staSsid = prefs.getString("ssid", isPlaceholderCredential(defaultSsid) ? "" : defaultSsid);
  g_settings.staPassword = prefs.getString("pass", isPlaceholderCredential(defaultPassword) ? "" : defaultPassword);
  g_settings.timeZone = prefs.getString("tz", TIME_ZONE_POSIX);
  g_settings.carrierFrequencyHz = prefs.getUInt("freq", CARRIER_FREQUENCY_HZ);
  g_settings.outputInverted = prefs.getBool("inv", JJY_OUTPUT_INVERTED != 0);
  g_settings.outputEnabled = prefs.getBool("en", DEFAULT_OUTPUT_ENABLED != 0);

  prefs.end();

  if (g_settings.carrierFrequencyHz != 40000 && g_settings.carrierFrequencyHz != 60000) {
    g_settings.carrierFrequencyHz = CARRIER_FREQUENCY_HZ;
  }

  if (g_settings.timeZone != "CST-8" && g_settings.timeZone != "JST-9") {
    g_settings.timeZone = TIME_ZONE_POSIX;
  }
}

static void saveSettings() {
  Preferences prefs;
  prefs.begin("jjy", false);
  prefs.putString("ssid", g_settings.staSsid);
  prefs.putString("pass", g_settings.staPassword);
  prefs.putString("tz", g_settings.timeZone);
  prefs.putUInt("freq", g_settings.carrierFrequencyHz);
  prefs.putBool("inv", g_settings.outputInverted);
  prefs.putBool("en", g_settings.outputEnabled);
  prefs.end();
}

static String formatLocalTime() {
  time_t now = time(nullptr);
  tm localTime{};
  localtime_r(&now, &localTime);

  if (localTime.tm_year + 1900 < 2024) {
    return "not synced";
  }

  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
  return String(buffer);
}

static void sendNoCache() {
  g_server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  g_server.sendHeader("Pragma", "no-cache");
}

static String buildStatusHtml() {
  const bool staConnected = WiFi.status() == WL_CONNECTED;
  const IPAddress staIp = WiFi.localIP();
  const IPAddress apIp = WiFi.softAPIP();

  String html;
  html.reserve(7000);
  html += F("<!doctype html><html lang='zh-Hant'><head><meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<meta http-equiv='refresh' content='5'>");
  html += F("<title>ESP32-S3 JJY 控制台</title>");
  html += F("<style>body{font-family:Arial,'Noto Sans TC',sans-serif;background:#0f172a;color:#e5e7eb;margin:0;padding:18px}main{max-width:760px;margin:auto}.card{background:#111827;border:1px solid #374151;border-radius:16px;padding:18px;margin:14px 0;box-shadow:0 10px 30px #0004}h1{font-size:26px}h2{font-size:18px;color:#93c5fd}.grid{display:grid;grid-template-columns:150px 1fr;gap:9px}.ok{color:#86efac}.bad{color:#fca5a5}.warn{color:#fde68a}input,select,button{font-size:16px;border-radius:10px;border:1px solid #475569;padding:10px;background:#020617;color:#e5e7eb}input[type=text],input[type=password],select{width:100%;box-sizing:border-box}button{background:#2563eb;border:0;cursor:pointer;margin:6px 6px 6px 0}.danger{background:#dc2626}.secondary{background:#475569}.row{margin:12px 0}.small{font-size:13px;color:#94a3b8}code{background:#020617;padding:2px 5px;border-radius:5px}</style></head><body><main>");
  html += F("<h1>ESP32-S3 JJY 控制台</h1>");

  html += F("<section class='card'><h2>目前狀態</h2><div class='grid'>");
  html += F("<div>輸出</div><div>");
  html += g_settings.outputEnabled ? F("<span class='ok'>啟用</span>") : F("<span class='bad'>停止</span>");
  html += F("</div><div>載波</div><div>");
  html += g_carrierOn ? F("ON") : F("OFF");
  html += F("</div><div>頻率</div><div>");
  html += String(g_settings.carrierFrequencyHz);
  html += F(" Hz</div><div>極性</div><div>");
  html += g_settings.outputInverted ? F("反相：active window 載波 ON") : F("標準：active window 載波 OFF");
  html += F("</div><div>時間</div><div>");
  html += htmlEscape(formatLocalTime());
  html += F("</div><div>NTP</div><div>");
  html += g_timeSynced ? F("<span class='ok'>synced</span>") : F("<span class='warn'>not synced</span>");
  html += F("</div><div>STA Wi-Fi</div><div>");
  html += staConnected ? F("<span class='ok'>connected</span> ") : F("<span class='bad'>disconnected</span> ");
  if (staConnected) {
    html += staIp.toString();
  }
  html += F("</div><div>AP 熱點</div><div>");
  html += htmlEscape(String(UI_AP_SSID));
  html += F(" / ");
  html += apIp.toString();
  html += F("</div><div>線圈腳位</div><div>GPIO");
  html += String(CARRIER_PIN);
  html += F(" → 220Ω/330Ω → 線圈 → GND</div></div></section>");

  html += F("<section class='card'><h2>設定</h2><form method='post' action='/save'>");
  html += F("<div class='row'><label>2.4GHz Wi-Fi SSID</label><input name='ssid' type='text' value='");
  html += htmlEscape(g_settings.staSsid);
  html += F("'></div>");
  html += F("<div class='row'><label>Wi-Fi 密碼</label><input name='password' type='password' placeholder='留空代表不修改目前儲存密碼'></div>");
  html += F("<div class='row'><label>JJY 頻率</label><select name='freq'>");
  html += String("<option value='60000'") + (g_settings.carrierFrequencyHz == 60000 ? " selected" : "") + ">60kHz 九州</option>";
  html += String("<option value='40000'") + (g_settings.carrierFrequencyHz == 40000 ? " selected" : "") + ">40kHz 福島</option>";
  html += F("</select></div>");
  html += F("<div class='row'><label>顯示時區</label><select name='tz'>");
  html += String("<option value='CST-8'") + (g_settings.timeZone == "CST-8" ? " selected" : "") + ">台灣時間 UTC+8</option>";
  html += String("<option value='JST-9'") + (g_settings.timeZone == "JST-9" ? " selected" : "") + ">日本時間 UTC+9</option>";
  html += F("</select></div>");
  html += F("<div class='row'><label>OOK 極性</label><select name='inv'>");
  html += String("<option value='0'") + (!g_settings.outputInverted ? " selected" : "") + ">標準：active window 載波 OFF</option>";
  html += String("<option value='1'") + (g_settings.outputInverted ? " selected" : "") + ">反相：active window 載波 ON</option>";
  html += F("</select></div>");
  html += F("<div class='row'><label><input name='en' type='checkbox' value='1'");
  html += g_settings.outputEnabled ? F(" checked") : F("");
  html += F("> 啟用 JJY 輸出</label></div>");
  html += F("<button type='submit'>儲存並套用</button></form>");
  html += F("<form method='post' action='/toggle' style='display:inline'><button class='secondary' type='submit'>切換輸出 ON/OFF</button></form>");
  html += F("<form method='post' action='/resync' style='display:inline'><button class='secondary' type='submit'>重新 NTP 同步</button></form>");
  html += F("<form method='post' action='/clear' style='display:inline' onsubmit='return confirm(\"確定清除 UI 儲存設定？\")'><button class='danger' type='submit'>清除儲存設定</button></form>");
  html += F("<p class='small'>手機可連 <code>");
  html += htmlEscape(String(UI_AP_SSID));
  html += F("</code>，密碼 <code>");
  html += htmlEscape(String(UI_AP_PASSWORD));
  html += F("</code>，再打開 <code>http://192.168.4.1</code>。</p></section>");

  html += F("<section class='card'><h2>接線提醒</h2><p>只讓 ESP32-S3 用 <code>GPIO4 → 220Ω/330Ω → 線圈 → GND</code> 靠近時鐘天線，不要直接接入 SEIKO 主板。</p></section>");
  html += F("</main></body></html>");
  return html;
}

static void redirectHome() {
  g_server.sendHeader("Location", "/");
  g_server.send(303, "text/plain", "See Other");
}

static void handleRoot() {
  sendNoCache();
  g_server.send(200, "text/html; charset=utf-8", buildStatusHtml());
}

static void handleStatusJson() {
  sendNoCache();
  String json = "{";
  json += "\"outputEnabled\":" + String(g_settings.outputEnabled ? "true" : "false") + ",";
  json += "\"carrierOn\":" + String(g_carrierOn ? "true" : "false") + ",";
  json += "\"timeSynced\":" + String(g_timeSynced ? "true" : "false") + ",";
  json += "\"frequency\":" + String(g_settings.carrierFrequencyHz) + ",";
  json += "\"time\":\"" + formatLocalTime() + "\",";
  json += "\"staConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"staIp\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"apIp\":\"" + WiFi.softAPIP().toString() + "\"";
  json += "}";
  g_server.send(200, "application/json", json);
}

static bool syncTimeBlocking(uint32_t timeoutMs) {
#if CARRIER_TEST_MODE || PULSE_TEST_MODE || DEBUG_FAKE_TIME
  Serial.println("[Time] NTP skipped because test/fake mode is enabled");
  g_timeSynced = true;
  return true;
#else
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NTP] skipped because STA Wi-Fi is not connected");
    g_timeSynced = false;
    return false;
  }

  applyTimeZone();
  Serial.println("[NTP] configuring SNTP");
  configTzTime(g_settings.timeZone.c_str(), NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (g_serverStarted) {
      g_server.handleClient();
    }

    time_t now = time(nullptr);
    tm localTime{};
    localtime_r(&now, &localTime);

    if (localTime.tm_year + 1900 >= 2024) {
      char buffer[32];
      strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
      Serial.printf("[NTP] synced: %s\n", buffer);
      g_timeSynced = true;
      return true;
    }

    Serial.print(".");
    setLed((millis() / 250) % 2);
    delay(500);
  }

  Serial.println();
  Serial.println("[NTP] sync failed");
  g_timeSynced = false;
  return false;
#endif
}

static void handleSave() {
  const String oldSsid = g_settings.staSsid;
  const String oldPassword = g_settings.staPassword;

  if (g_server.hasArg("ssid")) {
    g_settings.staSsid = g_server.arg("ssid");
    g_settings.staSsid.trim();
  }

  if (g_server.hasArg("password") && g_server.arg("password").length() > 0) {
    g_settings.staPassword = g_server.arg("password");
  }

  if (g_server.hasArg("freq")) {
    const uint32_t freq = (uint32_t)g_server.arg("freq").toInt();
    g_settings.carrierFrequencyHz = (freq == 40000) ? 40000 : 60000;
  }

  if (g_server.hasArg("tz")) {
    const String tz = g_server.arg("tz");
    g_settings.timeZone = (tz == "JST-9") ? "JST-9" : "CST-8";
  }

  g_settings.outputInverted = g_server.hasArg("inv") && g_server.arg("inv") == "1";
  g_settings.outputEnabled = g_server.hasArg("en");

  saveSettings();
  applyTimeZone();
  configureCarrierPwm();

  const bool wifiChanged = oldSsid != g_settings.staSsid || oldPassword != g_settings.staPassword;
  if (wifiChanged) {
    Serial.println("[WiFi] settings changed; reconnecting STA");
    WiFi.disconnect(false, false);
    g_timeSynced = false;
    g_lastWifiAttemptMs = 0;
  }

  redirectHome();
}

static void handleToggle() {
  g_settings.outputEnabled = !g_settings.outputEnabled;
  saveSettings();
  if (!g_settings.outputEnabled) {
    carrierOff();
  }
  redirectHome();
}

static void handleResync() {
  syncTimeBlocking(NTP_SYNC_TIMEOUT_MS);
  redirectHome();
}

static void handleClear() {
  Preferences prefs;
  prefs.begin("jjy", false);
  prefs.clear();
  prefs.end();
  loadSettings();
  applyTimeZone();
  configureCarrierPwm();
  redirectHome();
}

static void startWebUi() {
  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/status.json", HTTP_GET, handleStatusJson);
  g_server.on("/save", HTTP_POST, handleSave);
  g_server.on("/toggle", HTTP_POST, handleToggle);
  g_server.on("/resync", HTTP_POST, handleResync);
  g_server.on("/clear", HTTP_POST, handleClear);
  g_server.onNotFound(handleRoot);
  g_server.begin();
  g_serverStarted = true;
  Serial.printf("[UI] Web UI started on port %d\n", UI_WEB_PORT);
}

static void startAccessPoint() {
#if UI_AP_ALWAYS_ON
  WiFi.mode(WIFI_AP_STA);
  const bool ok = WiFi.softAP(UI_AP_SSID, UI_AP_PASSWORD);
  Serial.printf("[AP] %s SSID=%s IP=%s\n",
                ok ? "started" : "failed",
                UI_AP_SSID,
                WiFi.softAPIP().toString().c_str());
#else
  WiFi.mode(WIFI_STA);
#endif
}

static bool connectStaBlocking(uint32_t timeoutMs) {
  if (isPlaceholderCredential(g_settings.staSsid)) {
    Serial.println("[WiFi] STA skipped: no SSID configured. Use AP Web UI.");
    return false;
  }

  Serial.printf("[WiFi] connecting STA to SSID: %s\n", g_settings.staSsid.c_str());
  WiFi.begin(g_settings.staSsid.c_str(), g_settings.staPassword.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    if (g_serverStarted) {
      g_server.handleClient();
    }
    Serial.print(".");
    setLed((millis() / 500) % 2);
    delay(250);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] STA connection failed; AP Web UI remains available");
    return false;
  }

  Serial.println("[WiFi] STA connected");
  Serial.print("[WiFi] STA IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

static void maintainWiFiAndTime() {
#if CARRIER_TEST_MODE || PULSE_TEST_MODE || DEBUG_FAKE_TIME
  return;
#else
  const uint32_t nowMs = millis();

  if (WiFi.status() != WL_CONNECTED && !isPlaceholderCredential(g_settings.staSsid)) {
    if (nowMs - g_lastWifiAttemptMs > 30000 || g_lastWifiAttemptMs == 0) {
      g_lastWifiAttemptMs = nowMs;
      Serial.println("[WiFi] retrying STA connection");
      WiFi.disconnect(false, false);
      WiFi.begin(g_settings.staSsid.c_str(), g_settings.staPassword.c_str());
    }
    g_timeSynced = false;
    return;
  }

  if (WiFi.status() == WL_CONNECTED && !g_timeSynced) {
    if (nowMs - g_lastNtpAttemptMs > 30000 || g_lastNtpAttemptMs == 0) {
      g_lastNtpAttemptMs = nowMs;
      syncTimeBlocking(10000);
    }
  }
#endif
}

static void printProjectHeader() {
  Serial.println();
  Serial.println("==================================================");
  Serial.println(" ESP32-S3 JJY NTP Transmitter + Web UI");
  Serial.println(" Target: SEIKO RE572S / JJY radio clock test");
  Serial.println("==================================================");
  Serial.printf("Carrier pin: GPIO%d\n", CARRIER_PIN);
  Serial.printf("Carrier frequency: %lu Hz\n", (unsigned long)g_settings.carrierFrequencyHz);
  Serial.printf("Duty: %d %%\n", CARRIER_DUTY_PERCENT);
  Serial.printf("Timezone: %s\n", g_settings.timeZone.c_str());
  Serial.printf("Output enabled: %s\n", g_settings.outputEnabled ? "YES" : "NO");
  Serial.printf("Output polarity: %s\n", g_settings.outputInverted ? "INVERTED" : "NORMAL");
  Serial.println("NORMAL = active window carrier OFF, remaining window carrier ON.");
  Serial.println("Active windows: MARKER=200ms, ONE=500ms, ZERO=800ms");
  Serial.println("==================================================");
}

static time_t fakeStartEpoch() {
  tm fake{};
  fake.tm_year = FAKE_TIME_YEAR - 1900;
  fake.tm_mon = FAKE_TIME_MONTH - 1;
  fake.tm_mday = FAKE_TIME_DAY;
  fake.tm_hour = FAKE_TIME_HOUR;
  fake.tm_min = FAKE_TIME_MINUTE;
  fake.tm_sec = FAKE_TIME_SECOND;
  fake.tm_isdst = -1;
  return mktime(&fake);
}

static bool getCurrentLocalTimeWithMs(tm& localTime, uint16_t& millisecond) {
#if DEBUG_FAKE_TIME
  static time_t fakeStart = fakeStartEpoch();
  time_t now = fakeStart + (millis() / 1000);
  millisecond = millis() % 1000;
  localtime_r(&now, &localTime);
  return true;
#else
  timeval tv{};
  gettimeofday(&tv, nullptr);
  time_t now = tv.tv_sec;
  millisecond = tv.tv_usec / 1000;
  localtime_r(&now, &localTime);
  return g_timeSynced && (localTime.tm_year + 1900 >= 2024);
#endif
}

static void applyJjyOutput(bool activeWindow) {
  if (!g_settings.outputEnabled) {
    carrierOff();
    setLed(false);
    return;
  }

  // Standard JJY OOK: active window is the low-power interval.
  // With a GPIO coil, low-power is represented as carrier OFF.
  bool carrierShouldBeOn = !activeWindow;

  if (g_settings.outputInverted) {
    carrierShouldBeOn = activeWindow;
  }

  carrierWrite(carrierShouldBeOn);
  setLed(activeWindow);
}

static void printSecondLog(const tm& localTime, uint16_t ms, JjyBitType bit, bool activeWindow) {
  char timeBuffer[32];
  strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &localTime);

  Serial.printf("[JJY] %s.%03u sec=%02d bit=%s window=%ums active=%s output=%s carrier=%s freq=%luHz polarity=%s\n",
                timeBuffer,
                ms,
                localTime.tm_sec,
                jjyBitToString(bit),
                jjyActiveWindowMillis(bit),
                activeWindow ? "YES" : "NO",
                g_settings.outputEnabled ? "ENABLED" : "DISABLED",
                g_carrierOn ? "ON" : "OFF",
                (unsigned long)g_settings.carrierFrequencyHz,
                g_settings.outputInverted ? "INVERTED" : "NORMAL");
}

static void runCarrierTestMode() {
  carrierOn();
  setLed(true);

  if (!g_printedCarrierTest) {
    g_printedCarrierTest = true;
    Serial.println("[TEST] CARRIER_TEST_MODE enabled");
    Serial.printf("[TEST] continuous carrier on GPIO%d at %lu Hz\n",
                  CARRIER_PIN,
                  (unsigned long)g_settings.carrierFrequencyHz);
  }

  delay(1000);
}

static void runPulseTestMode() {
  const uint32_t sec = millis() / 1000;
  const uint16_t ms = millis() % 1000;

  JjyBitType bit;
  switch (sec % 3) {
    case 0:
      bit = JJY_MARKER;
      break;
    case 1:
      bit = JJY_ONE;
      break;
    default:
      bit = JJY_ZERO;
      break;
  }

  const bool activeWindow = ms < jjyActiveWindowMillis(bit);
  applyJjyOutput(activeWindow);

  if ((int)sec != g_lastLoggedSecond) {
    g_lastLoggedSecond = sec;
    Serial.printf("[PULSE_TEST] second=%lu bit=%s window=%ums carrier=%s\n",
                  (unsigned long)sec,
                  jjyBitToString(bit),
                  jjyActiveWindowMillis(bit),
                  g_carrierOn ? "ON" : "OFF");
  }

  delay(2);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

#if LED_PIN >= 0
  pinMode(LED_PIN, OUTPUT);
  setLed(false);
#endif

  loadSettings();
  applyTimeZone();
  printProjectHeader();

  if (!configureCarrierPwm()) {
    Serial.println("[BOOT] PWM setup failed. Halt.");
    while (true) {
      delay(1000);
    }
  }

  startAccessPoint();
  startWebUi();

  connectStaBlocking(WIFI_CONNECT_TIMEOUT_MS);
  syncTimeBlocking(NTP_SYNC_TIMEOUT_MS);

  Serial.println("[BOOT] ready");
  Serial.printf("[UI] AP URL: http://%s\n", WiFi.softAPIP().toString().c_str());
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[UI] STA URL: http://%s\n", WiFi.localIP().toString().c_str());
  }
}

void loop() {
  if (g_serverStarted) {
    g_server.handleClient();
  }

  maintainWiFiAndTime();

#if CARRIER_TEST_MODE
  runCarrierTestMode();
  return;
#endif

#if PULSE_TEST_MODE
  runPulseTestMode();
  return;
#endif

  tm localTime{};
  uint16_t ms = 0;

  const bool timeValid = getCurrentLocalTimeWithMs(localTime, ms);
  if (!timeValid) {
    carrierOff();
    setLed(false);
    delay(10);
    return;
  }

  JjyFrame frame = buildJjyFrame(localTime);
  JjyBitType bit = jjyBitAtSecond(frame, localTime.tm_sec);
  const bool activeWindow = ms < jjyActiveWindowMillis(bit);

  applyJjyOutput(activeWindow);

  if (localTime.tm_sec != g_lastLoggedSecond) {
    g_lastLoggedSecond = localTime.tm_sec;
    printSecondLog(localTime, ms, bit, activeWindow);
  }

  delay(2);
}
