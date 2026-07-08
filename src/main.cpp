#include <Arduino.h>
#include <WiFi.h>
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

static bool g_carrierOn = false;
static int g_lastLoggedSecond = -1;
static bool g_printedCarrierTest = false;

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

static bool setupCarrier() {
  pinMode(CARRIER_PIN, OUTPUT);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  bool ok = ledcAttach(CARRIER_PIN, CARRIER_FREQUENCY_HZ, LEDC_RESOLUTION_BITS);
  if (!ok) {
    Serial.println("[PWM] ledcAttach failed");
    return false;
  }
  Serial.printf("[PWM] requested=%lu Hz, pin=%d, resolution=%d bits\n",
                (unsigned long)CARRIER_FREQUENCY_HZ,
                CARRIER_PIN,
                LEDC_RESOLUTION_BITS);
#else
  double actualFreq = ledcSetup(LEDC_CHANNEL, CARRIER_FREQUENCY_HZ, LEDC_RESOLUTION_BITS);
  ledcAttachPin(CARRIER_PIN, LEDC_CHANNEL);
  Serial.printf("[PWM] requested=%lu Hz, actual=%.2f Hz, channel=%d, resolution=%d bits\n",
                (unsigned long)CARRIER_FREQUENCY_HZ,
                actualFreq,
                LEDC_CHANNEL,
                LEDC_RESOLUTION_BITS);
#endif

  carrierOff();
  return true;
}

static void printProjectHeader() {
  Serial.println();
  Serial.println("==================================================");
  Serial.println(" ESP32-S3 JJY NTP Transmitter");
  Serial.println(" Target: SEIKO RE572S / JJY radio clock test");
  Serial.println("==================================================");
  Serial.printf("Carrier pin: GPIO%d\n", CARRIER_PIN);
  Serial.printf("Carrier frequency: %lu Hz\n", (unsigned long)CARRIER_FREQUENCY_HZ);
  Serial.printf("Duty: %d %%\n", CARRIER_DUTY_PERCENT);
  Serial.printf("Timezone: %s\n", TIME_ZONE_POSIX);
  Serial.printf("Output polarity: %s\n", JJY_OUTPUT_INVERTED ? "INVERTED" : "NORMAL");
  Serial.println("Normal polarity = carrier ON during active JJY window.");
  Serial.println("Active windows: MARKER=200ms, ONE=500ms, ZERO=800ms");
  Serial.println("==================================================");
}

static bool connectWiFi() {
#if CARRIER_TEST_MODE || PULSE_TEST_MODE || DEBUG_FAKE_TIME
  Serial.println("[WiFi] skipped because test/fake mode is enabled");
  return true;
#else
  Serial.printf("[WiFi] connecting to SSID: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    Serial.print(".");
    setLed((millis() / 500) % 2);
    delay(250);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] connection failed");
    return false;
  }

  Serial.println("[WiFi] connected");
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());
  return true;
#endif
}

static bool syncTime() {
  setenv("TZ", TIME_ZONE_POSIX, 1);
  tzset();

#if CARRIER_TEST_MODE || PULSE_TEST_MODE || DEBUG_FAKE_TIME
  Serial.println("[Time] NTP skipped because test/fake mode is enabled");
  return true;
#else
  Serial.println("[NTP] configuring SNTP");
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

  const uint32_t start = millis();
  while (millis() - start < NTP_SYNC_TIMEOUT_MS) {
    time_t now = time(nullptr);
    tm localTime{};
    localtime_r(&now, &localTime);

    if (localTime.tm_year + 1900 >= 2024) {
      char buffer[32];
      strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
      Serial.printf("[NTP] synced: %s\n", buffer);

      for (int i = 0; i < 4; ++i) {
        setLed(true);
        delay(80);
        setLed(false);
        delay(80);
      }

      return true;
    }

    Serial.print(".");
    setLed((millis() / 250) % 2);
    delay(500);
  }

  Serial.println();
  Serial.println("[NTP] sync failed");
  return false;
#endif
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
  return (localTime.tm_year + 1900 >= 2024);
#endif
}

static void applyJjyOutput(bool activeWindow) {
  bool carrierShouldBeOn = activeWindow;

#if JJY_OUTPUT_INVERTED
  carrierShouldBeOn = !carrierShouldBeOn;
#endif

  carrierWrite(carrierShouldBeOn);
  setLed(activeWindow);
}

static void printSecondLog(const tm& localTime, uint16_t ms, JjyBitType bit, bool activeWindow) {
  char timeBuffer[32];
  strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &localTime);

  Serial.printf("[JJY] %s.%03u sec=%02d bit=%s window=%ums active=%s carrier=%s freq=%luHz polarity=%s\n",
                timeBuffer,
                ms,
                localTime.tm_sec,
                jjyBitToString(bit),
                jjyActiveWindowMillis(bit),
                activeWindow ? "YES" : "NO",
                g_carrierOn ? "ON" : "OFF",
                (unsigned long)CARRIER_FREQUENCY_HZ,
                JJY_OUTPUT_INVERTED ? "INVERTED" : "NORMAL");
}

static void runCarrierTestMode() {
  carrierOn();
  setLed(true);

  if (!g_printedCarrierTest) {
    g_printedCarrierTest = true;
    Serial.println("[TEST] CARRIER_TEST_MODE enabled");
    Serial.printf("[TEST] continuous carrier on GPIO%d at %lu Hz\n",
                  CARRIER_PIN,
                  (unsigned long)CARRIER_FREQUENCY_HZ);
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

  printProjectHeader();

  if (!setupCarrier()) {
    Serial.println("[BOOT] PWM setup failed. Halt.");
    while (true) {
      delay(1000);
    }
  }

  if (!connectWiFi()) {
    Serial.println("[BOOT] Wi-Fi failed. Halt.");
    while (true) {
      setLed((millis() / 300) % 2);
      delay(50);
    }
  }

  if (!syncTime()) {
    Serial.println("[BOOT] Time sync failed. Halt.");
    while (true) {
      setLed((millis() / 150) % 2);
      delay(50);
    }
  }

  Serial.println("[BOOT] ready");
}

void loop() {
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
    delay(100);
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
