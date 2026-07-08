# Serial troubleshooting

If PlatformIO Monitor only shows ESP-ROM boot messages but no application logs, the board is probably using a USB-to-UART chip such as CH343 while the firmware was built for native USB CDC Serial.

For CH343 boards, set these flags in `platformio.ini`:

```ini
build_flags =
  -DARDUINO_USB_MODE=0
  -DARDUINO_USB_CDC_ON_BOOT=0
  -Wall
  -Wextra
```

Then rebuild and upload again.
