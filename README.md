# ESP32-S3 JJY NTP Transmitter + Web UI

這是一個給 SEIKO RE572S 等日本電波時計使用的近距離 JJY 訊號測試發射器。

系統使用 ESP32-S3 連接 Wi-Fi，透過 NTP 取得時間，產生 JJY time code，並以 40kHz 或 60kHz 載波輸出。預設使用 60kHz，適合模擬日本九州 JJY 60kHz 訊號。

新版加入 **ESP32 內建 Web UI 控制台**：時鐘端只需要接 ESP32 的線圈，其餘設定可用手機或電腦瀏覽器調整。

## 重要警告

本專案只做近距離低功率測試。

不要做以下事情：

- 不要直接把 GPIO 接進時鐘主板。
- 不要拆除 SEIKO RE572S 原廠 ferrite antenna。
- 不要替換原廠電波接收模組。
- 不要接大功率放大器。
- 不要使用長天線。

建議接法：

```text
ESP32-S3 GPIO4 -> 220R/330R 電阻 -> 小型線圈 -> GND
```

## 硬體材料

- ESP32-S3 開發板
- USB 線
- 220Ω 或 330Ω 電阻
- 漆包線、單芯線或杜邦線
- SEIKO RE572S 電波鐘
- VS Code
- PlatformIO IDE extension
- 手機或電腦瀏覽器，用來開 Web UI

## 線圈建議

- 直徑：5 至 10 cm
- 圈數：約 15 至 30 圈
- 位置：靠近 SEIKO RE572S 原本接收天線區
- 測試時慢慢旋轉線圈方向
- 收訊期間不要移動線圈

## Web UI 控制台

ESP32-S3 會開一個 Wi-Fi 熱點：

```text
SSID: JJY-SETUP
Password: 12345678
網址: http://192.168.4.1
```

你可以用手機或電腦連上 `JJY-SETUP`，再打開 `http://192.168.4.1`。

Web UI 可以設定：

- 家裡 2.4GHz Wi-Fi SSID / 密碼
- JJY 頻率：60kHz / 40kHz
- 時區：台灣 UTC+8 / 日本 UTC+9
- OOK 極性：標準 / 反相
- JJY 輸出 ON / OFF
- 重新 NTP 同步
- 清除儲存設定

## 設定 Wi-Fi

方法 A：用程式檔預設 Wi-Fi

先複製設定檔：

```powershell
Copy-Item include\config.example.h include\config.h
```

打開：

```text
include/config.h
```

修改：

```cpp
#define WIFI_SSID "你的WiFi名稱"
#define WIFI_PASSWORD "你的WiFi密碼"
```

方法 B：用 Web UI 設定 Wi-Fi

若 `include/config.h` 還是預設值，ESP32 仍會啟動 AP 熱點。連上 `JJY-SETUP` 後，在 Web UI 填入家裡 2.4GHz Wi-Fi，再按「儲存並套用」。

## 時區

預設時區是台灣 UTC+8：

```cpp
#define TIME_ZONE_POSIX "CST-8"
```

如果要讓鐘顯示日本時間，可在 Web UI 改成日本 UTC+9。

## OOK 極性

新版預設為標準 JJY 低功率調變邏輯：

```text
active window 期間：carrier OFF
剩餘時間：carrier ON
```

如果 SEIKO RE572S 收不到，可在 Web UI 把極性切成「反相」再測。

## 編譯

在 VS Code 安裝 PlatformIO IDE。

然後按左側 PlatformIO 圖示，選：

```text
Project Tasks -> esp32-s3-devkitc-1 -> General -> Build
```

或 Terminal 執行：

```powershell
pio run
```

## 燒錄

插上 ESP32-S3，選擇 Upload：

```text
Project Tasks -> esp32-s3-devkitc-1 -> General -> Upload
```

或：

```powershell
pio run -t upload
```

## Serial Monitor

鮑率：

```text
115200
```

開啟：

```text
Project Tasks -> esp32-s3-devkitc-1 -> General -> Monitor
```

或：

```powershell
pio device monitor
```

## 對時流程

1. ESP32-S3 燒錄完成。
2. 開 Serial Monitor，確認有看到 Web UI 與 Wi-Fi 訊息。
3. 用手機或電腦連 `JJY-SETUP`，或直接打開 ESP32 的 STA IP。
4. 在 Web UI 確認 Wi-Fi connected、NTP synced。
5. 在 Web UI 確認輸出啟用、頻率 60kHz、極性標準。
6. GPIO4 接 220Ω/330Ω 電阻，再接線圈到 GND。
7. 線圈貼近 SEIKO RE572S 背面或內部接收天線附近。
8. 按下 RE572S 背後 A 鍵進入強制接收。
9. 等 5 至 15 分鐘。

## 如果 15 分鐘還沒成功

依序檢查：

1. Web UI 時間是否正確。
2. 是否真的連上家裡 2.4GHz Wi-Fi。
3. NTP 是否 synced。
4. GPIO 是否為 GPIO4。
5. 線圈是否有串 220Ω/330Ω 電阻。
6. 線圈是否靠近時鐘天線區。
7. 嘗試旋轉線圈方向。
8. 把線圈更貼近時鐘。
9. Web UI 將極性從標準改成反相再測。
10. Web UI 將頻率從 60kHz 改成 40kHz 再測。
11. 開啟 `CARRIER_TEST_MODE` 檢查是否有連續載波。
12. 開啟 `PULSE_TEST_MODE` 檢查 Marker / ONE / ZERO 節奏。

## 測試模式

在 `include/config.h`：

```cpp
#define CARRIER_TEST_MODE 1
```

可讓 GPIO4 持續輸出 60kHz。

```cpp
#define PULSE_TEST_MODE 1
```

可讓輸出每秒循環 Marker / ONE / ZERO。

```cpp
#define DEBUG_FAKE_TIME 1
```

可不用 NTP，使用假時間測試 JJY 編碼。

正常使用時，三個都要是 0。
