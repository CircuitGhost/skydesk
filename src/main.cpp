#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <Arduino_GFX_Library.h>
#include "config.h"
#include "astronomy.h"
#include "network_data.h"

// Official Waveshare ESP32-C6-LCD-1.47 bus: HWSPI + ST7789 172x320 with col offset 34
Arduino_DataBus *bus = new Arduino_HWSPI(
    LCD_DC /* DC */, LCD_CS /* CS */, LCD_SCLK /* SCK */, LCD_MOSI /* MOSI */, GFX_NOT_DEFINED /* MISO */
);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus, LCD_RST /* RST */, 0 /* rotation */, true /* IPS */,
    SCREEN_WIDTH /* width 172 */, SCREEN_HEIGHT /* height 320 */,
    34 /* col_offset1 */, 0 /* row_offset1 */, 34 /* col_offset2 */, 0 /* row_offset2 */
);

Arduino_Canvas *canvas = new Arduino_Canvas(
    SCREEN_WIDTH, SCREEN_HEIGHT, gfx, 0, 0, 0 /* rotation */
);

// Draw to the canvas when RAM allows, otherwise straight to the panel
Arduino_GFX *screen = gfx;

// --- State Variables ---
int currentCard = 0;
bool isPaused = false;
unsigned long cardTimerStart = 0;
unsigned long lastFetchTime = 0;

WeatherData currentWeather;
SpaceWeatherData currentSpaceWeather;

// --- Colors (RGB565) ---
#define COLOR_BG          0x0821 // Dark Cosmic Blue
#define COLOR_CARD_BG     0x10A4 // Deep Slate Navy
#define COLOR_TEXT        0xFFFF // Pure White
#define COLOR_GOLD        0xFEA0 // Warm Gold
#define COLOR_CYAN        0x07FF // Bright Cyan
#define COLOR_GREEN       0x07E0 // Bright Green
#define COLOR_ORANGE      0xFD20 // Solar Orange
#define COLOR_PURPLE      0xA81F // Aurora Purple
#define COLOR_GRAY        0x7BEF // Muted Slate Gray

// Function Declarations
void drawHeader(const char* title);
void drawProgressBar(float progress);
void renderClockCard();
void renderMoonCard(time_t nowTime);
void renderSunCard(time_t nowTime);
void renderPlanetCard(time_t nowTime);
void renderStargazingCard(time_t nowTime);
void renderRadarCard(time_t nowTime);
void renderWeatherCard();
void renderSpaceWeatherCard();
void updateRGBColor();
void checkBootButton();
void formatTime12(int mins, char *buf, size_t n);
void formatCountdown(int mins, char *buf, size_t n);

void drawSplash(const char *line1, const char *line2) {
    screen->fillScreen(COLOR_BG);
    screen->setTextColor(COLOR_CYAN);
    screen->setTextSize(2);
    screen->setCursor(12, 120);
    screen->print(line1);
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(1);
    screen->setCursor(12, 160);
    screen->print(line2);
    if (screen == canvas) {
        screen->flush();
    }
}

void setRgb(uint8_t r, uint8_t g, uint8_t b) {
    rgbLedWrite(RGB_LED_PIN, r, g, b);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("Starting Waveshare ESP32-C6 Desk Companion...");
    Serial.printf("Free heap: %u\n", ESP.getFreeHeap());

    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH); // Keep TF-card off the shared SPI bus

    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);

    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
    setRgb(0, 80, 180);

    if (!gfx->begin(40000000)) {
        Serial.println("GFX Display init failed!");
    }
    gfx->fillScreen(COLOR_BG);

    if (canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        screen = canvas;
        Serial.println("Canvas framebuffer OK");
    } else {
        screen = gfx;
        Serial.println("Canvas alloc failed; drawing direct to LCD");
    }
    Serial.printf("Free heap after display: %u\n", ESP.getFreeHeap());

    drawSplash("Connecting", "Wi-Fi...");

    AppNetworkManager::initWiFi();
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Wi-Fi OK: ");
        Serial.println(WiFi.localIP());
        drawSplash("Wi-Fi OK", WiFi.localIP().toString().c_str());
        setRgb(0, 180, 80);
    } else {
        Serial.println("Wi-Fi not connected yet; continuing");
        drawSplash("No Wi-Fi", "Retrying in background");
        setRgb(180, 40, 0);
    }
    delay(800);

    cardTimerStart = millis();
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void loop() {
    time_t nowTime = time(NULL);
    if (nowTime < 100000) {
        nowTime = 1715000000; // Fallback epoch if NTP syncing
    }

    AppNetworkManager::checkWiFiStatus();
    if (millis() > 1500 && (millis() - lastFetchTime > 300000 || lastFetchTime == 0)) {
        if (WiFi.status() == WL_CONNECTED) {
            lastFetchTime = millis();
            currentWeather = AppNetworkManager::fetchWeather();
            currentSpaceWeather = AppNetworkManager::fetchSpaceWeather();
            if (currentWeather.hasUtcOffset) {
                configTime(currentWeather.utcOffsetSec, 0, "pool.ntp.org", "time.nist.gov");
                nowTime = time(NULL);
            }
        }
    }

    if (nowTime > 1700000000) {
        static bool loggedAstro = false;
        if (!loggedAstro) {
            loggedAstro = true;
            SunInfo s = AstronomyEngine::getSunInfo(nowTime, LATITUDE, LONGITUDE);
            MoonTimes m = AstronomyEngine::getMoonTimes(nowTime, LATITUDE, LONGITUDE);
            Serial.printf("Local sun rise/set %02d:%02d / %02d:%02d  moon rise/set %d / %d\n",
                          s.sunriseMin / 60, s.sunriseMin % 60,
                          s.sunsetMin / 60, s.sunsetMin % 60,
                          m.riseMin, m.setMin);
        }
    }

    // BOOT Button polling
    checkBootButton();

    // Auto-rotation card timer
    unsigned long elapsed = millis() - cardTimerStart;
    if (!isPaused && elapsed >= CARD_DURATION_MS) {
        currentCard = (currentCard + 1) % NUM_CARDS;
        cardTimerStart = millis();
        elapsed = 0;
    }
    float progress = (float)elapsed / (float)CARD_DURATION_MS;
    if (isPaused) progress = 1.0f;

    screen->fillScreen(COLOR_BG);

    // Render Current Active Card
    switch (currentCard) {
        case 0: renderClockCard(); break;
        case 1: renderMoonCard(nowTime); break;
        case 2: renderSunCard(nowTime); break;
        case 3: renderPlanetCard(nowTime); break;
        case 4: renderStargazingCard(nowTime); break;
        case 5: renderRadarCard(nowTime); break;
        case 6: renderWeatherCard(); break;
        case 7: renderSpaceWeatherCard(); break;
    }

    // Render Bottom Progress Bar
    drawProgressBar(progress);

    if (screen == canvas) {
        screen->flush();
    }

    // Update RGB LED Glow
    updateRGBColor();

    delay(30); // ~30fps frame pacing
}

void drawHeader(const char* title) {
    screen->fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_CARD_BG);
    screen->drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_CYAN);
    screen->setTextColor(COLOR_CYAN);
    screen->setTextSize(1);
    screen->setCursor(8, 10);
    screen->print(title);

    // Wi-Fi icon indicator
    if (WiFi.status() == WL_CONNECTED) {
        screen->fillCircle(SCREEN_WIDTH - 12, 14, 3, COLOR_GREEN);
    } else {
        screen->fillCircle(SCREEN_WIDTH - 12, 14, 3, COLOR_GRAY);
    }
}

void formatTime12(int mins, char *buf, size_t n) {
    if (mins < 0) {
        snprintf(buf, n, "--");
        return;
    }
    mins = ((mins % 1440) + 1440) % 1440;
    int h24 = mins / 60;
    int m = mins % 60;
    int h12 = h24 % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, n, "%d:%02d %s", h12, m, h24 < 12 ? "AM" : "PM");
}

void formatCountdown(int mins, char *buf, size_t n) {
    if (mins < 0) mins = 0;
    int h = mins / 60;
    int m = mins % 60;
    if (h <= 0) {
        snprintf(buf, n, "%d min", m);
    } else {
        snprintf(buf, n, "%dh %02dm", h, m);
    }
}

void drawProgressBar(float progress) {
    int barWidth = (int)(progress * SCREEN_WIDTH);
    screen->fillRect(0, SCREEN_HEIGHT - 3, SCREEN_WIDTH, 3, COLOR_CARD_BG);
    screen->fillRect(0, SCREEN_HEIGHT - 3, barWidth, 3, isPaused ? COLOR_GOLD : COLOR_CYAN);
}

// --- CARD 1: LOCAL CLOCK ---
void renderClockCard() {
    drawHeader("LOCAL TIME");

    time_t now = time(NULL);
    bool synced = now > 1700000000;
    struct tm localTm = {};
    if (synced) {
        localtime_r(&now, &localTm);
    }

    static const char *kDow[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    int h24 = synced ? localTm.tm_hour : 0;
    int h12 = h24 % 12;
    if (h12 == 0) h12 = 12;

    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(4);
    if (!synced) {
        screen->setCursor(16, 44);
        screen->print("--:--");
    } else {
        char clockBuf[8];
        snprintf(clockBuf, sizeof(clockBuf), "%d:%02d", h12, localTm.tm_min);
        int clockW = (int)strlen(clockBuf) * 24;
        screen->setCursor((SCREEN_WIDTH - clockW) / 2, 44);
        screen->print(clockBuf);
    }

    screen->setTextSize(2);
    screen->setTextColor(COLOR_GOLD);
    screen->setCursor(64, 84);
    if (synced) {
        screen->print(h24 < 12 ? "AM" : "PM");
    } else {
        screen->setTextSize(1);
        screen->setCursor(28, 84);
        screen->print("Waiting for NTP");
    }

    screen->setTextColor(COLOR_CYAN);
    screen->setTextSize(1);
    screen->setCursor(28, 108);
    if (synced) {
        screen->printf("%s %s %d", kDow[localTm.tm_wday], kMon[localTm.tm_mon], localTm.tm_mday);
    } else {
        screen->print(LOCATION_NAME);
    }

    SunInfo sun = AstronomyEngine::getSunInfo(synced ? now : 1715000000, LATITUDE, LONGITUDE);
    MoonTimes moonTimes = AstronomyEngine::getMoonTimes(synced ? now : 1715000000, LATITUDE, LONGITUDE);
    int nowMin = synced ? (localTm.tm_hour * 60 + localTm.tm_min) : 0;
    int targetMin = sun.isDaylight ? sun.sunsetMin : sun.sunriseMin;
    int remaining = targetMin - nowMin;
    if (remaining < 0) remaining += 1440;

    char eventBuf[16];
    formatCountdown(remaining, eventBuf, sizeof(eventBuf));
    const char *eventLabel = sun.isGoldenHour ? "GOLDEN HOUR NOW" :
                             (sun.isDaylight ? "SUNSET IN" : "SUNRISE IN");
    uint16_t badgeColor = sun.isGoldenHour ? COLOR_ORANGE : COLOR_CARD_BG;

    screen->fillRoundRect(10, 128, SCREEN_WIDTH - 20, 42, 6, badgeColor);
    screen->setTextColor(COLOR_TEXT);
    screen->setCursor(20, 136);
    screen->print(eventLabel);
    if (!sun.isGoldenHour) {
        screen->setTextColor(COLOR_GOLD);
        screen->setTextSize(2);
        screen->setCursor(20, 148);
        screen->print(eventBuf);
    }

    char riseBuf[16], setBuf[16], moonRiseBuf[16], moonSetBuf[16];
    formatTime12(sun.sunriseMin, riseBuf, sizeof(riseBuf));
    formatTime12(sun.sunsetMin, setBuf, sizeof(setBuf));
    formatTime12(moonTimes.riseMin, moonRiseBuf, sizeof(moonRiseBuf));
    formatTime12(moonTimes.setMin, moonSetBuf, sizeof(moonSetBuf));

    screen->fillRoundRect(10, 180, SCREEN_WIDTH - 20, 124, 8, COLOR_CARD_BG);
    screen->setTextSize(1);
    screen->setTextColor(COLOR_CYAN);
    screen->setCursor(20, 194);
    screen->printf("Sunrise   %s", riseBuf);
    screen->setCursor(20, 214);
    screen->printf("Sunset    %s", setBuf);
    screen->setTextColor(COLOR_GOLD);
    screen->setCursor(20, 240);
    screen->printf("Moonrise  %s", moonRiseBuf);
    screen->setCursor(20, 260);
    screen->printf("Moonset   %s", moonSetBuf);
    screen->setTextColor(COLOR_GRAY);
    screen->setCursor(20, 284);
    screen->print(LOCATION_NAME);
}

// --- CARD 2: MOON PHASE GRAPHICS ---
void renderMoonCard(time_t nowTime) {
    drawHeader("LUNAR CALENDAR");

    MoonInfo moon = AstronomyEngine::getMoonInfo(nowTime);

    // Draw 3D Procedural Moon Sphere
    int centerX = SCREEN_WIDTH / 2;
    int centerY = 110;
    int radius = 42;

    // Moon background disk (unlit side)
    screen->fillCircle(centerX, centerY, radius, 0x2104);

    // Procedural lit phase crescent/sphere rendering
    float phase = moon.illumination;
    int fillWidth = (int)(radius * 2 * phase);
    for (int y = -radius; y <= radius; y++) {
        int xBound = (int)sqrt(radius * radius - y * y);
        int xStart = centerX - xBound;
        int xEnd = centerX + xBound;

        // Phase shadow curve
        if (moon.ageDays <= 14.76f) { // Waxing
            int shadowX = centerX + (int)(xBound * (1.0f - 2.0f * phase));
            for (int x = shadowX; x <= xEnd; x++) {
                screen->drawPixel(x, centerY + y, COLOR_GOLD);
            }
        } else { // Waning
            int shadowX = centerX - (int)(xBound * (1.0f - 2.0f * (1.0f - phase)));
            for (int x = xStart; x <= shadowX; x++) {
                screen->drawPixel(x, centerY + y, COLOR_GOLD);
            }
        }
    }
    screen->drawCircle(centerX, centerY, radius, COLOR_TEXT);

    // Moon Stats Info Card
    screen->fillRoundRect(10, 175, SCREEN_WIDTH - 20, 130, 8, COLOR_CARD_BG);
    screen->setTextColor(COLOR_GOLD);
    screen->setTextSize(2);
    screen->setCursor(20, 190);
    screen->printf("%.0f%% LIT", moon.illumination * 100.0f);

    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(1);
    screen->setCursor(20, 220);
    screen->print(moon.phaseName);

    screen->setCursor(20, 245);
    screen->printf("Moon Age: %.1f days", moon.ageDays);

    screen->setTextColor(COLOR_CYAN);
    screen->setCursor(20, 270);
    MoonTimes moonTimes = AstronomyEngine::getMoonTimes(nowTime, LATITUDE, LONGITUDE);
    char moonRiseBuf[16];
    char moonSetBuf[16];
    formatTime12(moonTimes.riseMin, moonRiseBuf, sizeof(moonRiseBuf));
    formatTime12(moonTimes.setMin, moonSetBuf, sizeof(moonSetBuf));
    screen->printf("Moonrise: %s", moonRiseBuf);
    screen->setCursor(20, 285);
    screen->printf("Moonset:  %s", moonSetBuf);
}

// --- CARD 2: GOLDEN HOUR & SUN ARC GRAPHICS ---
void renderSunCard(time_t nowTime) {
    drawHeader("SUN & GOLDEN HOUR");

    SunInfo sun = AstronomyEngine::getSunInfo(nowTime, LATITUDE, LONGITUDE);

    // Draw Solar Horizon Arc Diagram
    int arcX = SCREEN_WIDTH / 2;
    int arcY = 130;
    int rx = 65;
    int ry = 45;

    // Horizon line
    screen->drawFastHLine(10, arcY, SCREEN_WIDTH - 20, COLOR_GRAY);

    // Parabolic Sun Path Arc
    for (int deg = 0; deg <= 180; deg += 3) {
        float rad = deg * M_PI / 180.0f;
        int x = arcX - (int)(rx * cosf(rad));
        int y = arcY - (int)(ry * sinf(rad));
        screen->drawPixel(x, y, (deg >= 30 && deg <= 150) ? COLOR_GOLD : COLOR_ORANGE);
    }

    // Draw Sun Icon on Arc
    float currentProgress = constrain((sun.currentElevation + 10.0f) / 90.0f, 0.0f, 1.0f);
    float sunRad = currentProgress * M_PI;
    int sunX = arcX - (int)(rx * cosf(sunRad));
    int sunY = arcY - (int)(ry * sinf(sunRad));

    screen->fillCircle(sunX, sunY, 6, COLOR_GOLD);
    screen->drawCircle(sunX, sunY, 8, COLOR_ORANGE);

    // Status Badge
    screen->fillRoundRect(10, 160, SCREEN_WIDTH - 20, 30, 6, sun.isGoldenHour ? COLOR_ORANGE : COLOR_CARD_BG);
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(1);
    screen->setCursor(20, 171);
    if (sun.isGoldenHour) {
        screen->print("GOLDEN HOUR ACTIVE!");
    } else if (sun.isDaylight) {
        screen->print("DAYLIGHT / SOLAR DAY");
    } else {
        screen->print("NIGHT / TWILIGHT");
    }

    // Solar Timings Grid
    screen->fillRoundRect(10, 200, SCREEN_WIDTH - 20, 105, 8, COLOR_CARD_BG);
    char riseBuf[16], setBuf[16], goldBuf[16];
    formatTime12(sun.sunriseMin, riseBuf, sizeof(riseBuf));
    formatTime12(sun.sunsetMin, setBuf, sizeof(setBuf));
    formatTime12(sun.goldenHourEveningStart, goldBuf, sizeof(goldBuf));

    screen->setTextColor(COLOR_CYAN);
    screen->setCursor(20, 212);
    screen->printf("Sunrise:  %s", riseBuf);

    screen->setCursor(20, 232);
    screen->printf("Sunset:   %s", setBuf);

    screen->setTextColor(COLOR_GOLD);
    screen->setCursor(20, 256);
    screen->printf("Golden Hr: %s", goldBuf);

    screen->setTextColor(COLOR_GRAY);
    screen->setCursor(20, 280);
    screen->printf("Elev: %+.1f deg", sun.currentElevation);
}

static const char *compassFromAz(float az) {
    static const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int idx = (int)((az + 22.5f) / 45.0f) % 8;
    if (idx < 0) idx += 8;
    return dirs[idx];
}

struct StarScore {
    int score;
    const char *label;
    const char *advice;
};

StarScore computeStarScore(time_t nowTime) {
    StarScore s = {0, "DAYLIGHT", "Wait until after sunset"};
    SunInfo sun = AstronomyEngine::getSunInfo(nowTime, LATITUDE, LONGITUDE);
    int code = currentWeather.weatherCode;
    bool precip = (code >= 51 && code <= 67) || (code >= 71 && code <= 82) || (code >= 95);
    bool fog = (code == 45 || code == 48);

    if (sun.isDaylight) {
        return s;
    }
    if (precip) {
        s.score = 1;
        s.label = "WASHED OUT";
        s.advice = "Rain or snow in the way";
        return s;
    }
    if (fog) {
        s.score = 1;
        s.label = "FOG";
        s.advice = "Wait for it to lift";
        return s;
    }

    MoonInfo moon = AstronomyEngine::getMoonInfo(nowTime);
    float penalty = (currentWeather.cloudCover / 25.0f) + (moon.illumination * 2.0f);
    int score = (int)lroundf(constrain(5.0f - penalty, 1.0f, 5.0f));
    s.score = score;
    if (score >= 5) { s.label = "EXCELLENT"; s.advice = "Dark and clear — look up"; }
    else if (score == 4) { s.label = "GOOD"; s.advice = "Worth a look outside"; }
    else if (score == 3) { s.label = "FAIR"; s.advice = "Bright moon or some cloud"; }
    else if (score == 2) { s.label = "POOR"; s.advice = "Tough night for stars"; }
    else { s.label = "VERY POOR"; s.advice = "Wait for a darker sky"; }
    return s;
}

// --- CARD 4: NAKED-EYE PLANETS ---
void renderPlanetCard(time_t nowTime) {
    drawHeader("VISIBLE PLANETS");

    PlanetInfo planets[4];
    int n = AstronomyEngine::getNakedEyePlanets(nowTime, LATITUDE, LONGITUDE, planets, 4);

    int upCount = 0;
    for (int i = 0; i < n; i++) {
        if (planets[i].isUp) upCount++;
        int y = 38 + i * 58;
        screen->fillRoundRect(10, y, SCREEN_WIDTH - 20, 52, 8, COLOR_CARD_BG);
        screen->setTextSize(1);
        screen->setTextColor(COLOR_CYAN);
        screen->setCursor(20, y + 8);
        screen->print(planets[i].name);

        screen->setTextColor(planets[i].isUp ? COLOR_GOLD : COLOR_GRAY);
        screen->setCursor(20, y + 24);
        if (planets[i].isUp) {
            screen->printf("UP  %+.0f deg  %s", planets[i].alt, compassFromAz(planets[i].az));
        } else {
            screen->printf("Down  %+.0f deg", planets[i].alt);
        }
    }

    screen->setTextColor(COLOR_GRAY);
    screen->setTextSize(1);
    screen->setCursor(14, 278);
    screen->printf("%d of 4 above the horizon", upCount);
}

// --- CARD 5: STARGAZING SCORE ---
void renderStargazingCard(time_t nowTime) {
    drawHeader("STARGAZING");

    StarScore score = computeStarScore(nowTime);
    MoonInfo moon = AstronomyEngine::getMoonInfo(nowTime);

    uint16_t scoreColor = COLOR_GRAY;
    if (score.score >= 4) scoreColor = COLOR_GREEN;
    else if (score.score == 3) scoreColor = COLOR_GOLD;
    else if (score.score >= 1) scoreColor = COLOR_ORANGE;

    screen->setTextColor(scoreColor);
    screen->setTextSize(5);
    if (score.score <= 0) {
        screen->setTextSize(3);
        screen->setCursor(28, 50);
        screen->print("DAY");
    } else {
        screen->setCursor(64, 48);
        screen->print(score.score);
    }

    screen->setTextSize(1);
    screen->setTextColor(COLOR_TEXT);
    screen->setCursor(20, 100);
    screen->print(score.label);
    screen->setTextColor(COLOR_CYAN);
    screen->setCursor(20, 118);
    screen->print(score.advice);

    screen->fillRoundRect(10, 145, SCREEN_WIDTH - 20, 148, 8, COLOR_CARD_BG);
    screen->setTextColor(COLOR_TEXT);
    screen->setCursor(20, 160);
    screen->printf("Clouds     %d%%", currentWeather.cloudCover);
    screen->fillRect(20, 176, 132, 8, 0x2104);
    screen->fillRect(20, 176, constrain((currentWeather.cloudCover * 132) / 100, 0, 132), 8, COLOR_CYAN);

    screen->setCursor(20, 198);
    screen->printf("Moon       %.0f%% lit", moon.illumination * 100.0f);
    screen->fillRect(20, 214, 132, 8, 0x2104);
    screen->fillRect(20, 214, constrain((int)(moon.illumination * 132), 0, 132), 8, COLOR_GOLD);

    screen->setCursor(20, 236);
    screen->printf("Kp index   %.1f", currentSpaceWeather.kpIndex);
    screen->fillRect(20, 252, 132, 8, 0x2104);
    screen->fillRect(20, 252, constrain((int)((currentSpaceWeather.kpIndex / 9.0f) * 132), 0, 132), 8,
                     currentSpaceWeather.kpIndex >= 5.0f ? COLOR_PURPLE : COLOR_GREEN);

    screen->setTextColor(COLOR_GRAY);
    screen->setCursor(20, 272);
    screen->print(LOCATION_NAME);
}

// --- CARD 6: GNSS SATELLITE RADAR GRAPHICS ---
void renderRadarCard(time_t nowTime) {
    drawHeader("GNSS SATELLITE RADAR");

    int centerX = SCREEN_WIDTH / 2;
    int centerY = 120;
    int r1 = 20, r2 = 45, r3 = 70;

    // Draw Polar Radar Scope Grid
    screen->drawCircle(centerX, centerY, r1, 0x0320);
    screen->drawCircle(centerX, centerY, r2, 0x0320);
    screen->drawCircle(centerX, centerY, r3, COLOR_CYAN);
    screen->drawFastHLine(centerX - r3, centerY, r3 * 2, 0x0320);
    screen->drawFastVLine(centerX, centerY - r3, r3 * 2, 0x0320);

    // Compass Headings
    screen->setTextColor(COLOR_CYAN);
    screen->setTextSize(1);
    screen->setCursor(centerX - 3, centerY - r3 - 10); screen->print("N");
    screen->setCursor(centerX + r3 + 4, centerY - 3);   screen->print("E");
    screen->setCursor(centerX - 3, centerY + r3 + 3);  screen->print("S");
    screen->setCursor(centerX - r3 - 10, centerY - 3);  screen->print("W");

    // Fetch & Plot Satellite Positions
    SatPos sats[16];
    int count = AstronomyEngine::getSatellitesInView(nowTime, sats, 16);

    for (int i = 0; i < count; i++) {
        float azRad = sats[i].az * M_PI / 180.0f;
        float r = r3 * (1.0f - (sats[i].el / 90.0f));
        int sx = centerX + (int)(r * sinf(azRad));
        int sy = centerY - (int)(r * cosf(azRad));

        uint16_t color = sats[i].isGps ? COLOR_GREEN : COLOR_ORANGE;
        screen->fillCircle(sx, sy, 3, color);
    }

    // Info Panel
    screen->fillRoundRect(10, 205, SCREEN_WIDTH - 20, 100, 8, COLOR_CARD_BG);
    screen->setTextColor(COLOR_GREEN);
    screen->setTextSize(2);
    screen->setCursor(20, 220);
    screen->printf("%d SATS", count);

    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(1);
    screen->setCursor(20, 250);
    screen->print("GPS & GLONASS Overhead");
    screen->setCursor(20, 270);
    screen->print("Simulated skyplot");
    screen->setTextColor(COLOR_GRAY);
    screen->setCursor(20, 285);
    screen->print("Constellation: Active");
}

// --- CARD 4: LOCAL WEATHER GRAPHICS ---
void renderWeatherCard() {
    drawHeader("LOCAL WEATHER");

    // Location & Main Temp
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(1);
    screen->setCursor(15, 40);
    screen->print(LOCATION_NAME);

    screen->setTextColor(COLOR_GOLD);
    screen->setTextSize(4);
    screen->setCursor(15, 60);
    screen->printf("%.0f", currentWeather.tempF);
    screen->setTextSize(2);
    screen->print("oF");

    screen->setTextColor(COLOR_CYAN);
    screen->setTextSize(1);
    screen->setCursor(15, 105);
    screen->print(currentWeather.conditionText);

    // Weather Metrics Grid
    screen->fillRoundRect(10, 130, SCREEN_WIDTH - 20, 175, 8, COLOR_CARD_BG);

    screen->setTextColor(COLOR_TEXT);
    screen->setCursor(20, 145);
    screen->printf("High / Low:  %.0f / %.0f oF", currentWeather.tempHighF, currentWeather.tempLowF);

    screen->setCursor(20, 175);
    screen->printf("Humidity:    %d%%", currentWeather.humidity);

    screen->setCursor(20, 205);
    screen->printf("Wind Speed:  %.1f mph", currentWeather.windSpeedMph);

    screen->setCursor(20, 235);
    screen->printf("UV Index:    %.1f", currentWeather.uvIndex);

    // UV Gauge Bar
    screen->fillRect(20, 255, 132, 8, 0x2104);
    int uvWidth = (int)((currentWeather.uvIndex / 10.0f) * 132);
    screen->fillRect(20, 255, constrain(uvWidth, 0, 132), 8, COLOR_ORANGE);

    screen->setTextColor(COLOR_GRAY);
    screen->setCursor(20, 285);
    screen->print("Source: Open-Meteo API");
}

// --- CARD 5: ISS & SPACE WEATHER ALERT GRAPHICS ---
void renderSpaceWeatherCard() {
    drawHeader("ISS & SPACE WEATHER");

    // Space Weather Kp Gauge
    screen->fillRoundRect(10, 38, SCREEN_WIDTH - 20, 120, 8, COLOR_CARD_BG);
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(1);
    screen->setCursor(20, 50);
    screen->print("NOAA Geomagnetic Kp");

    screen->setTextColor(currentSpaceWeather.kpIndex >= 5.0f ? COLOR_ORANGE : COLOR_GREEN);
    screen->setTextSize(3);
    screen->setCursor(20, 68);
    screen->printf("Kp %.1f", currentSpaceWeather.kpIndex);

    screen->setTextSize(1);
    screen->setCursor(20, 100);
    screen->print(currentSpaceWeather.stormLevel);

    // Kp Gauge Bar
    screen->fillRect(20, 125, 132, 10, 0x2104);
    int kpWidth = (int)((currentSpaceWeather.kpIndex / 9.0f) * 132);
    uint16_t barColor = currentSpaceWeather.kpIndex >= 5.0f ? COLOR_PURPLE : COLOR_GREEN;
    screen->fillRect(20, 125, constrain(kpWidth, 0, 132), 10, barColor);

    // ISS Tracker Panel
    screen->fillRoundRect(10, 170, SCREEN_WIDTH - 20, 135, 8, COLOR_CARD_BG);
    screen->setTextColor(COLOR_CYAN);
    screen->setTextSize(1);
    screen->setCursor(20, 185);
    screen->print("ISS FLYOVER TRACKER");

    screen->setTextColor(COLOR_TEXT);
    screen->setCursor(20, 210);
    screen->print("Status: Orbit Tracking");

    screen->setCursor(20, 235);
    screen->printf("Next Pass: ~%d min", currentSpaceWeather.issNextPassMin);

    screen->setCursor(20, 260);
    screen->printf("Distance:  %.0f km", currentSpaceWeather.issDistanceKm);

    screen->setTextColor(COLOR_GOLD);
    screen->setCursor(20, 285);
    screen->print("RGB LED: Live Status");
}

// --- PERIPHERAL HELPERS ---
void updateRGBColor() {
    if (currentSpaceWeather.kpIndex >= 5.0f) {
        int glow = (millis() / 8) % 512;
        int v = glow < 256 ? glow : 511 - glow;
        setRgb(v, 0, 180 - v / 3);
        return;
    }

    int code = currentWeather.weatherCode;
    bool storm = (code >= 95);
    bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
    bool snow = (code >= 71 && code <= 77);
    bool fog = (code == 45 || code == 48);

    if (storm) {
        int flash = ((millis() / 140) % 7) == 0 ? 255 : 50;
        setRgb(flash / 5, flash / 5, flash);
        return;
    }
    if (rain) {
        int pulse = 50 + (int)((millis() / 18) % 90);
        setRgb(0, pulse, 230);
        return;
    }
    if (snow) {
        setRgb(190, 210, 255);
        return;
    }
    if (fog) {
        setRgb(70, 85, 110);
        return;
    }

    time_t now = time(NULL);
    if (now < 1700000000) {
        setRgb(0, 80, 150);
        return;
    }

    SunInfo sun = AstronomyEngine::getSunInfo(now, LATITUDE, LONGITUDE);
    if (!sun.isDaylight) {
        StarScore seeing = computeStarScore(now);
        if (seeing.score >= 4) {
            setRgb(170, 200, 255);
            return;
        }
    }
    if (sun.isGoldenHour) {
        setRgb(255, 140, 25);
    } else if (sun.isDaylight) {
        if (code <= 1) {
            setRgb(255, 175, 40);
        } else {
            setRgb(130, 145, 165);
        }
    } else {
        setRgb(20, 30, 100);
    }
}

void checkBootButton() {
    static bool lastBtnState = HIGH;
    static unsigned long lastDebounce = 0;
    bool reading = digitalRead(BOOT_BTN_PIN);

    if (reading != lastBtnState && (millis() - lastDebounce > 150)) {
        lastDebounce = millis();
        if (reading == LOW) { // Button Pressed
            isPaused = !isPaused;
            if (!isPaused) {
                currentCard = (currentCard + 1) % NUM_CARDS;
                cardTimerStart = millis();
            }
        }
        lastBtnState = reading;
    }
}
