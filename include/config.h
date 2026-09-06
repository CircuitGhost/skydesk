#ifndef CONFIG_H
#define CONFIG_H

#if __has_include("user_config.h")
#include "user_config.h"
#else
#error Copy include/user_config.example.h to include/user_config.h and set Wi-Fi plus your city lat/lon
#endif

#ifndef WIFI_SSID
#error WIFI_SSID is missing. Copy include/user_config.example.h to include/user_config.h
#endif

// --- Waveshare ESP32-C6 1.47" LCD Pin Mapping ---
#define LCD_MOSI        6
#define LCD_SCLK        7
#define LCD_CS          14
#define LCD_DC          15
#define LCD_RST         21
#define LCD_BL          22
#define SD_CS_PIN       4   // Shared SPI with LCD; must stay HIGH

// --- Peripherals Pin Mapping ---
#define RGB_LED_PIN     8
#define BOOT_BTN_PIN    9

// --- Display Parameters ---
#define SCREEN_WIDTH    172
#define SCREEN_HEIGHT   320

// --- UI Settings ---
#define CARD_DURATION_MS 10800 // ~10.8s per card (35% slower than 8s)
#define NUM_CARDS        6

#endif // CONFIG_H
