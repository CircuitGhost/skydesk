# Skydesk

Firmware for a Waveshare ESP32-C6 1.47" LCD desk companion. It rotates cards for local time, moon, sun/golden hour, GNSS skyplot, weather, and ISS/space weather, and drives the onboard RGB LED from daylight and conditions.

## Hardware

[Waveshare ESP32-C6-LCD-1.47](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47) — 172×320 ST7789, Wi-Fi 6, RGB LED, BOOT button.

## Setup

1. Copy `include/secrets.example.h` to `include/secrets.h` and set your Wi-Fi SSID and password.
2. Edit `include/config.h` if you want a different location than Youngstown, OH.
3. Build and flash with [PlatformIO](https://platformio.org/):

```bash
pio run -e waveshare-esp32-c6-lcd -t upload
```

BOOT pauses auto-rotate or skips to the next card.

## Cards

1. Local clock with sunrise, sunset, moonrise, and moonset
2. Lunar calendar
3. Sun and golden hour
4. GNSS satellite radar
5. Local weather (Open-Meteo)
6. ISS and NOAA Kp index
