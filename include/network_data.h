#ifndef NETWORK_DATA_H
#define NETWORK_DATA_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"

struct WeatherData {
    float tempF;
    float tempHighF;
    float tempLowF;
    int humidity;
    float windSpeedMph;
    int weatherCode;
    float uvIndex;
    String conditionText;
    bool isValid;
    int utcOffsetSec;
    bool hasUtcOffset;
    int cloudCover; // 0-100
};

struct SpaceWeatherData {
    float kpIndex;
    String stormLevel;
    float issDistanceKm;
    int issNextPassMin;
    bool isValid;
};

class AppNetworkManager {
public:
    static void initWiFi() {
        Serial.print("Connecting to Wi-Fi: ");
        Serial.println(WIFI_SSID);
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    }

    static bool checkWiFiStatus() {
        if (WiFi.status() == WL_CONNECTED) {
            return true;
        }
        static unsigned long lastAttempt = 0;
        if (millis() - lastAttempt > 10000) {
            lastAttempt = millis();
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASS);
        }
        return false;
    }

    static WeatherData fetchWeather() {
        WeatherData data = { 72.0f, 78.0f, 62.0f, 55, 8.5f, 1, 4.2f, "Partly Cloudy", false, 0, false, 40 };
        String payload;
        String url = String("https://api.open-meteo.com/v1/forecast?latitude=") +
                     String(LATITUDE, 4) + "&longitude=" + String(LONGITUDE, 4) +
                     "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,cloud_cover" +
                     "&daily=temperature_2m_max,temperature_2m_min,uv_index_max" +
                     "&temperature_unit=fahrenheit&wind_speed_unit=mph&timezone=auto" +
                     "&forecast_days=1";
        if (!httpGet(url.c_str(), payload)) {
            return data;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Serial.printf("Weather JSON error: %s\n", err.c_str());
            return data;
        }

        data.tempF = doc["current"]["temperature_2m"] | 72.0f;
        data.humidity = doc["current"]["relative_humidity_2m"] | 55;
        data.windSpeedMph = doc["current"]["wind_speed_10m"] | 8.5f;
        data.weatherCode = doc["current"]["weather_code"] | 1;
        data.tempHighF = doc["daily"]["temperature_2m_max"][0] | 78.0f;
        data.tempLowF = doc["daily"]["temperature_2m_min"][0] | 62.0f;
        data.uvIndex = doc["daily"]["uv_index_max"][0] | 4.2f;
        data.cloudCover = doc["current"]["cloud_cover"] | 40;
        data.conditionText = getWeatherConditionText(data.weatherCode);
        data.utcOffsetSec = doc["utc_offset_seconds"] | 0;
        data.hasUtcOffset = true;
        data.isValid = true;
        Serial.printf("Weather OK: %.0f F %s (UTC%+d)\n", data.tempF, data.conditionText.c_str(),
                      data.utcOffsetSec / 3600);
        return data;
    }

    static SpaceWeatherData fetchSpaceWeather() {
        SpaceWeatherData data = { 2.3f, "Quiet", 1240.0f, 42, false };
        fetchKpIndex(data);
        fetchIssDistance(data);
        return data;
    }

private:
    static bool httpGet(const char *url, String &payload) {
        if (WiFi.status() != WL_CONNECTED) {
            return false;
        }

        NetworkClientSecure client;
        client.setInsecure();
        client.setTimeout(8);

        HTTPClient http;
        http.setTimeout(8000);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.useHTTP10(true);
        http.setUserAgent("DeskCompanion/1.0");
        if (!http.begin(client, url)) {
            Serial.printf("HTTP begin failed: %s\n", url);
            return false;
        }
        http.addHeader("Accept", "application/json");
        http.addHeader("Accept-Encoding", "identity");

        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            Serial.printf("HTTP %d for %s\n", httpCode, url);
            http.end();
            return false;
        }
        payload = http.getString();
        http.end();
        if (payload.length() < 2) {
            Serial.println("HTTP empty body");
            return false;
        }
        return true;
    }

    static float haversineKm(float lat1, float lon1, float lat2, float lon2) {
        const float R = 6371.0f;
        float dLat = (lat2 - lat1) * M_PI / 180.0f;
        float dLon = (lon2 - lon1) * M_PI / 180.0f;
        float a = sinf(dLat / 2) * sinf(dLat / 2) +
                  cosf(lat1 * M_PI / 180.0f) * cosf(lat2 * M_PI / 180.0f) *
                  sinf(dLon / 2) * sinf(dLon / 2);
        return R * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
    }

    static void fetchKpIndex(SpaceWeatherData &data) {
        String payload;
        if (!httpGet("https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json", payload)) {
            return;
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err || !doc.is<JsonArray>() || doc.size() < 2) {
            Serial.printf("Kp JSON error: %s\n", err.c_str());
            return;
        }
        JsonArray row = doc[doc.size() - 1];
        if (row.size() > 1) {
            data.kpIndex = row[1].as<float>();
            if (data.kpIndex < 3.0f) data.stormLevel = "Quiet";
            else if (data.kpIndex < 4.0f) data.stormLevel = "Unsettled";
            else if (data.kpIndex < 5.0f) data.stormLevel = "Active";
            else if (data.kpIndex < 6.0f) data.stormLevel = "G1 Minor Storm";
            else data.stormLevel = "G2+ Solar Storm!";
            data.isValid = true;
            Serial.printf("Kp OK: %.1f %s\n", data.kpIndex, data.stormLevel.c_str());
        }
    }

    static void fetchIssDistance(SpaceWeatherData &data) {
        if (WiFi.status() != WL_CONNECTED) return;
        HTTPClient http;
        http.setTimeout(8000);
        http.useHTTP10(true);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        if (!http.begin("http://api.open-notify.org/iss-now.json")) return;
        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            Serial.printf("ISS HTTP %d\n", httpCode);
            http.end();
            return;
        }
        String payload = http.getString();
        http.end();

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Serial.printf("ISS JSON error: %s\n", err.c_str());
            return;
        }
        float issLat = doc["iss_position"]["latitude"].as<float>();
        float issLon = doc["iss_position"]["longitude"].as<float>();
        data.issDistanceKm = haversineKm(LATITUDE, LONGITUDE, issLat, issLon);
        data.issNextPassMin = (int)constrain(data.issDistanceKm / 460.0f, 1.0f, 90.0f);
        data.isValid = true;
        Serial.printf("ISS distance: %.0f km\n", data.issDistanceKm);
    }

    static String getWeatherConditionText(int code) {
        switch (code) {
            case 0: return "Clear Sky";
            case 1: return "Mainly Clear";
            case 2: return "Partly Cloudy";
            case 3: return "Overcast";
            case 45: case 48: return "Foggy";
            case 51: case 53: case 55: return "Drizzle";
            case 61: case 63: case 65: return "Rainy";
            case 71: case 73: case 75: return "Snowy";
            case 80: case 81: case 82: return "Rain Showers";
            case 95: case 96: case 99: return "Thunderstorm";
            default: return "Partly Cloudy";
        }
    }
};

#endif // NETWORK_DATA_H
