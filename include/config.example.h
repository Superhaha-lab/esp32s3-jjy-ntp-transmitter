#pragma once

// =====================================================
// ESP32-S3 JJY NTP Transmitter configuration example
// 請複製本檔為 include/config.h，然後填入 Wi-Fi。
// =====================================================

// ---------- Wi-Fi ----------
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

// ---------- Time / NTP ----------
// 台灣時間 UTC+8。POSIX TZ 寫法方向相反，所以 UTC+8 寫成 CST-8。
#define TIME_ZONE_POSIX "CST-8"

// 日本時間 UTC+9，如要讓鐘顯示日本時間，可改成：
// #define TIME_ZONE_POSIX "JST-9"

#define NTP_SERVER_1 "time.stdinet.net.tw"
#define NTP_SERVER_2 "pool.ntp.org"
#define NTP_SERVER_3 "time.google.com"

// ---------- GPIO / PWM ----------
#define CARRIER_PIN 4
#define CARRIER_FREQUENCY_HZ 60000
#define CARRIER_DUTY_PERCENT 50

// LEDC 設定。Arduino-ESP32 core 2.x 會用 channel；core 3.x 會自動改用 pin API。
#define LEDC_CHANNEL 0
#define LEDC_RESOLUTION_BITS 8

// 板載 LED 腳位。如果你的 ESP32-S3 板沒有 LED，維持 -1 即可。
#define LED_PIN -1
#define LED_ACTIVE_LOW 0

// ---------- JJY output polarity ----------
// 0：標準模式，active window 期間輸出 60kHz 載波。
// 1：反相模式，active window 期間關閉載波。
// 如果 SEIKO RE572S 收不到，請先嘗試 0，再嘗試 1。
#define JJY_OUTPUT_INVERTED 0

// ---------- Test modes ----------
// 正常使用時全部維持 0。
#define DEBUG_FAKE_TIME 0
#define CARRIER_TEST_MODE 0
#define PULSE_TEST_MODE 0

// 假時間測試模式用
#define FAKE_TIME_YEAR 2026
#define FAKE_TIME_MONTH 7
#define FAKE_TIME_DAY 8
#define FAKE_TIME_HOUR 12
#define FAKE_TIME_MINUTE 0
#define FAKE_TIME_SECOND 0

// ---------- Serial ----------
#define SERIAL_BAUD 115200
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define NTP_SYNC_TIMEOUT_MS 30000
