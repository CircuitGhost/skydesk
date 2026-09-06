# Skydesk

A small desk display for the [Waveshare ESP32-C6 1.47" LCD](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47). It shows the time, sun and moon, local weather, and ISS distance. The RGB LED changes with daylight, rain, and geomagnetic storms.

## You will need

- That Waveshare board and a USB-C cable
- [VS Code](https://code.visualstudio.com/) with the **PlatformIO IDE** extension (search “PlatformIO” in Extensions)
- Home Wi-Fi that is **2.4 GHz**. 5 GHz will not work on this chip.

## 1. Open the project

Download or clone this repo, then open the folder in VS Code.

## 2. Add your Wi-Fi and city

In the `include` folder, duplicate `user_config.example.h` and name the copy `user_config.h`.

Open `user_config.h` and change only these lines:

```c
#define WIFI_SSID       "your-wifi-ssid"
#define WIFI_PASS       "your-wifi-password"

#define LOCATION_NAME   "Your City"
#define LATITUDE        40.7128f
#define LONGITUDE       -74.0060f
```

- SSID and password are your 2.4 GHz network.
- `LOCATION_NAME` is just the label on screen.
- For latitude and longitude, search the web for *your city latitude longitude* and paste the two numbers. Keep the `f` at the end of each number.

You do not set a timezone. Skydesk figures that out from the coordinates.

## 3. Put it on the board

Plug the board into USB. In VS Code, open the PlatformIO sidebar and click **Upload** (environment: `waveshare-esp32-c6-lcd`).

If it cannot find the board: hold the **BOOT** button, tap **RESET**, let go of **BOOT**, then Upload again.

## When it works

The screen says Connecting, then Wi-Fi OK, then a clock. Press **BOOT** to pause or skip cards.

If the backlight comes on but the screen stays black, the flash did not take — use the BOOT/RESET steps above and Upload again.

## Cards

1. Clock with sunrise, sunset, moonrise, moonset
2. Lunar calendar
3. Sun and golden hour
4. Visible planets (Venus, Mars, Jupiter, Saturn)
5. Stargazing score
6. GNSS skyplot (drawn for fun — this board has no GPS chip)
7. Local weather
8. ISS distance and NOAA Kp index
