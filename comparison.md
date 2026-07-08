# 與 w-dee/JJY_EMITTER 的對照

參考專案：

```text
https://github.com/w-dee/JJY_EMITTER
```

## 相同點

本專案與 w-dee/JJY_EMITTER 一樣採用：

- ESP32 系列 MCU
- Wi-Fi
- NTP 校時
- JJY time code
- 40kHz / 60kHz 載波概念
- LEDC PWM
- OOK 調變
- Marker = 200ms
- Binary 1 = 500ms
- Binary 0 = 800ms

## 不同點

本專案針對 SEIKO RE572S 與 ESP32-S3 需求整理：

- 使用 PlatformIO 專案結構
- 目標板為 ESP32-S3 DevKitC-1
- 預設輸出腳位 GPIO4
- 預設時區為台灣 UTC+8
- 新增 `include/config.h` 設定方式
- 新增 NORMAL / INVERTED 極性設定
- 新增假時間、連續載波、脈衝循環測試模式
- README 使用繁體中文
- 接線方式以近場小線圈為主

## 注意

本專案不直接接入 SEIKO RE572S 主板，也不替換原廠接收模組。  
只使用外部低功率線圈靠近原本天線區。
