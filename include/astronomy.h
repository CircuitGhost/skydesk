#ifndef ASTRONOMY_H
#define ASTRONOMY_H

#include <Arduino.h>
#include <time.h>
#include <math.h>

struct MoonInfo {
    float illumination; // 0.0 to 1.0 (0% to 100%)
    float ageDays;      // 0.0 to 29.53 days
    String phaseName;   // e.g. "Waxing Gibbous"
    int phaseAngle;     // 0 to 360 degrees
};

struct SunInfo {
    int sunriseMin;       // Minutes from local midnight
    int sunsetMin;
    int solarNoonMin;
    int goldenHourMorningStart;
    int goldenHourMorningEnd;
    int goldenHourEveningStart;
    int goldenHourEveningEnd;
    float currentElevation; // Solar elevation angle (-90 to +90 deg)
    bool isGoldenHour;
    bool isDaylight;
};

struct MoonTimes {
    int riseMin; // Minutes from local midnight, or -1 if none today
    int setMin;
};

struct PlanetInfo {
    const char *name;
    float alt; // degrees, negative = below horizon
    float az;  // 0 = north, clockwise
    bool isUp;
};

struct SatPos {
    int prn;
    float az;  // Azimuth 0..360
    float el;  // Elevation 0..90
    bool isGps; // true = GPS, false = GLONASS
};

class AstronomyEngine {
public:
    static MoonInfo getMoonInfo(time_t epochTime) {
        MoonInfo info;
        const double refNewMoon = 1704974220.0;
        const double synodicMonth = 29.53058867;

        double diffDays = ((double)epochTime - refNewMoon) / 86400.0;
        double currentCycle = diffDays / synodicMonth - floor(diffDays / synodicMonth);

        info.ageDays = currentCycle * synodicMonth;
        info.phaseAngle = (int)(currentCycle * 360.0) % 360;
        info.illumination = (1.0f - cosf((float)currentCycle * 2.0f * M_PI)) * 0.5f;

        if (info.ageDays < 1.0 || info.ageDays > 28.53) {
            info.phaseName = "New Moon";
        } else if (info.ageDays < 6.5) {
            info.phaseName = "Waxing Crescent";
        } else if (info.ageDays < 8.0) {
            info.phaseName = "First Quarter";
        } else if (info.ageDays < 13.8) {
            info.phaseName = "Waxing Gibbous";
        } else if (info.ageDays < 15.7) {
            info.phaseName = "Full Moon";
        } else if (info.ageDays < 21.5) {
            info.phaseName = "Waning Gibbous";
        } else if (info.ageDays < 23.0) {
            info.phaseName = "Last Quarter";
        } else {
            info.phaseName = "Waning Crescent";
        }

        return info;
    }

    static SunInfo getSunInfo(time_t epochTime, float lat, float lon) {
        SunInfo info;
        struct tm utcTm = {};
        struct tm localTm = {};
        gmtime_r(&epochTime, &utcTm);
        localtime_r(&epochTime, &localTm);

        int dayOfYear = utcTm.tm_yday + 1;
        float decl = 23.45f * sinf((284.0f + dayOfYear) * 360.0f / 365.0f * M_PI / 180.0f) * M_PI / 180.0f;
        float latRad = lat * M_PI / 180.0f;

        float B = (360.0f / 365.0f) * (dayOfYear - 81) * M_PI / 180.0f;
        float eot = 9.87f * sinf(2.0f * B) - 7.53f * cosf(B) - 1.5f * sinf(B);

        // Solar noon in minutes from UTC midnight (west longitudes are negative)
        float utcNoon = 720.0f - (lon * 4.0f) - eot;

        float cosH = (sinf(-0.833f * M_PI / 180.0f) - sinf(latRad) * sinf(decl)) / (cosf(latRad) * cosf(decl));
        cosH = constrain(cosH, -1.0f, 1.0f);
        float H_deg = acosf(cosH) * 180.0f / M_PI;

        float cosH_gh = (sinf(6.0f * M_PI / 180.0f) - sinf(latRad) * sinf(decl)) / (cosf(latRad) * cosf(decl));
        cosH_gh = constrain(cosH_gh, -1.0f, 1.0f);
        float H_gh_deg = acosf(cosH_gh) * 180.0f / M_PI;

        info.solarNoonMin = utcEventToLocalMin(epochTime, utcNoon);
        info.sunriseMin = utcEventToLocalMin(epochTime, utcNoon - H_deg * 4.0f);
        info.sunsetMin = utcEventToLocalMin(epochTime, utcNoon + H_deg * 4.0f);
        info.goldenHourMorningStart = utcEventToLocalMin(epochTime, utcNoon - H_deg * 4.0f);
        info.goldenHourMorningEnd = utcEventToLocalMin(epochTime, utcNoon - H_gh_deg * 4.0f);
        info.goldenHourEveningStart = utcEventToLocalMin(epochTime, utcNoon + H_gh_deg * 4.0f);
        info.goldenHourEveningEnd = utcEventToLocalMin(epochTime, utcNoon + H_deg * 4.0f);

        float utcNowMin = utcTm.tm_hour * 60.0f + utcTm.tm_min + utcTm.tm_sec / 60.0f;
        float currentH_rad = ((utcNowMin - utcNoon) / 4.0f) * M_PI / 180.0f;
        float sinElev = sinf(latRad) * sinf(decl) + cosf(latRad) * cosf(decl) * cosf(currentH_rad);
        info.currentElevation = asinf(constrain(sinElev, -1.0f, 1.0f)) * 180.0f / M_PI;

        int nowMin = localTm.tm_hour * 60 + localTm.tm_min;
        info.isDaylight = (nowMin >= info.sunriseMin && nowMin <= info.sunsetMin);
        info.isGoldenHour = ((nowMin >= info.goldenHourMorningStart && nowMin <= info.goldenHourMorningEnd) ||
                             (nowMin >= info.goldenHourEveningStart && nowMin <= info.goldenHourEveningEnd));
        return info;
    }

    static MoonTimes getMoonTimes(time_t epochTime, float lat, float lon) {
        struct tm localTm = {};
        localtime_r(&epochTime, &localTm);
        int dayKey = localTm.tm_year * 366 + localTm.tm_yday;

        static int cachedKey = -1;
        static MoonTimes cached = { -1, -1 };
        static float cachedLat = 0;
        static float cachedLon = 0;
        if (dayKey == cachedKey && cachedLat == lat && cachedLon == lon) {
            return cached;
        }

        localTm.tm_hour = 0;
        localTm.tm_min = 0;
        localTm.tm_sec = 0;
        localTm.tm_isdst = -1;
        time_t midnight = mktime(&localTm);

        const float horizon = -0.8f;
        MoonTimes times = { -1, -1 };
        float prevEl = moonElevationDeg(midnight - 180, lat, lon);
        for (int m = 0; m <= 1440; m += 3) {
            float el = moonElevationDeg(midnight + m * 60, lat, lon);
            if (times.riseMin < 0 && prevEl < horizon && el >= horizon) {
                times.riseMin = m;
            }
            if (times.setMin < 0 && prevEl >= horizon && el < horizon) {
                times.setMin = m;
            }
            prevEl = el;
        }

        cachedKey = dayKey;
        cached = times;
        cachedLat = lat;
        cachedLon = lon;
        return times;
    }

    static int getSatellitesInView(time_t epochTime, SatPos satsOut[], int maxSats) {
        const int totalConstellation = 14;
        static const struct { int prn; float baseAz; float baseEl; float orbitPeriodHrs; bool isGps; } constData[] = {
            { 3,   45.0f, 62.0f, 11.96f, true },
            { 8,  120.0f, 35.0f, 11.96f, true },
            { 14, 210.0f, 78.0f, 11.96f, true },
            { 17, 310.0f, 42.0f, 11.96f, true },
            { 22,  85.0f, 18.0f, 11.96f, true },
            { 27, 160.0f, 55.0f, 11.96f, true },
            { 31, 280.0f, 25.0f, 11.96f, true },
            { 701,  15.0f, 50.0f, 11.26f, false },
            { 704,  95.0f, 72.0f, 11.26f, false },
            { 712, 175.0f, 38.0f, 11.26f, false },
            { 719, 240.0f, 65.0f, 11.26f, false },
            { 724, 335.0f, 48.0f, 11.26f, false },
            { 19, 135.0f, 82.0f, 11.96f, true },
            { 708,  55.0f, 30.0f, 11.26f, false }
        };

        int count = 0;
        double hoursPastEpoch = (double)(epochTime % 86400) / 3600.0;

        for (int i = 0; i < totalConstellation && count < maxSats; i++) {
            float az = fmodf(constData[i].baseAz + (float)(hoursPastEpoch * 360.0 / constData[i].orbitPeriodHrs), 360.0f);
            float el = constData[i].baseEl + 15.0f * sinf((float)(hoursPastEpoch * 2.0 * M_PI / 4.0 + i));
            el = constrain(el, 10.0f, 88.0f);

            satsOut[count].prn = constData[i].prn;
            satsOut[count].az = az;
            satsOut[count].el = el;
            satsOut[count].isGps = constData[i].isGps;
            count++;
        }
        return count;
    }

    static int getNakedEyePlanets(time_t epochTime, float lat, float lon, PlanetInfo out[], int maxN) {
        const int nPlanets = 4;
        static const char *names[nPlanets] = { "Venus", "Mars", "Jupiter", "Saturn" };
        int count = 0;
        for (int i = 0; i < nPlanets && count < maxN; i++) {
            double ra, dec;
            planetRaDec(epochTime, i, &ra, &dec);
            float alt, az;
            raDecToAltAz(epochTime, ra, dec, lat, lon, &alt, &az);
            out[count].name = names[i];
            out[count].alt = alt;
            out[count].az = az;
            out[count].isUp = alt > 0.0f;
            count++;
        }
        return count;
    }

private:
    static int utcEventToLocalMin(time_t epochTime, float utcMinFromMidnight) {
        struct tm utcTm = {};
        gmtime_r(&epochTime, &utcTm);
        time_t utcMidnight = epochTime - (utcTm.tm_hour * 3600 + utcTm.tm_min * 60 + utcTm.tm_sec);
        time_t event = utcMidnight + (time_t)(utcMinFromMidnight * 60.0f);
        struct tm localTm = {};
        localtime_r(&event, &localTm);
        return localTm.tm_hour * 60 + localTm.tm_min;
    }

    static double wrap360(double deg) {
        deg = fmod(deg, 360.0);
        if (deg < 0) deg += 360.0;
        return deg;
    }

    static void moonRaDec(time_t t, double *ra, double *dec) {
        const double J2000 = 946728000.0;
        double d = ((double)t - J2000) / 86400.0;

        double Lp = wrap360(218.3164477 + 13.17639648 * d);
        double D  = wrap360(297.8501921 + 12.19074912 * d);
        double M  = wrap360(357.5291092 + 0.98560028 * d);
        double Mp = wrap360(134.9633964 + 13.06499295 * d);
        double F  = wrap360(93.2720950 + 13.22935038 * d);

        double lon = Lp
            + 6.289 * sin(Mp * M_PI / 180.0)
            + 1.274 * sin((2.0 * D - Mp) * M_PI / 180.0)
            + 0.658 * sin((2.0 * D) * M_PI / 180.0)
            + 0.214 * sin((2.0 * Mp) * M_PI / 180.0)
            - 0.186 * sin(M * M_PI / 180.0);
        double lat = 5.128 * sin(F * M_PI / 180.0)
            + 0.280 * sin((Mp + F) * M_PI / 180.0)
            + 0.277 * sin((Mp - F) * M_PI / 180.0);
        double eps = 23.439291 - 0.0000004 * d;

        double lonR = lon * M_PI / 180.0;
        double latR = lat * M_PI / 180.0;
        double epsR = eps * M_PI / 180.0;
        *ra = atan2(sin(lonR) * cos(epsR) - tan(latR) * sin(epsR), cos(lonR));
        *dec = asin(sin(latR) * cos(epsR) + cos(latR) * sin(epsR) * sin(lonR));
    }

    static float moonElevationDeg(time_t t, float lat, float lon) {
        double ra, dec;
        moonRaDec(t, &ra, &dec);
        float alt, az;
        raDecToAltAz(t, ra, dec, lat, lon, &alt, &az);
        (void)az;
        return alt;
    }

    static void raDecToAltAz(time_t t, double ra, double dec, float lat, float lon, float *alt, float *az) {
        const double J2000 = 946728000.0;
        double d = ((double)t - J2000) / 86400.0;
        double gmst = fmod(18.697374558 + 24.06570982441908 * d, 24.0);
        if (gmst < 0) gmst += 24.0;
        double lstHours = fmod(gmst + lon / 15.0, 24.0);
        if (lstHours < 0) lstHours += 24.0;
        double ha = lstHours * 15.0 * M_PI / 180.0 - ra;
        double latR = lat * M_PI / 180.0;
        double sinEl = sin(latR) * sin(dec) + cos(latR) * cos(dec) * cos(ha);
        *alt = (float)(asin(constrain(sinEl, -1.0, 1.0)) * 180.0 / M_PI);
        double azR = atan2(sin(ha), cos(ha) * sin(latR) - tan(dec) * cos(latR));
        double azDeg = azR * 180.0 / M_PI + 180.0;
        if (azDeg < 0) azDeg += 360.0;
        if (azDeg >= 360.0) azDeg -= 360.0;
        *az = (float)azDeg;
    }

    // Schlyter-style compact orbits; index 0 Venus .. 3 Saturn
    static void planetRaDec(time_t t, int index, double *ra, double *dec) {
        double d = ((double)t - 946728000.0) / 86400.0 + 1.5;
        double sunX, sunY, sunZ;
        bodyHelio(d, -1, &sunX, &sunY, &sunZ);

        double xh, yh, zh;
        bodyHelio(d, index, &xh, &yh, &zh);
        // Schlyter: bodyHelio(-1) is the geocentric Sun (Earth→Sun),
        // so geocentric planet = heliocentric planet + geocentric Sun.
        double xg = xh + sunX;
        double yg = yh + sunY;
        double zg = zh + sunZ;

        double lon = atan2(yg, xg);
        double hyp = sqrt(xg * xg + yg * yg);
        double latp = atan2(zg, hyp);
        double eps = (23.4393 - 3.563E-7 * d) * M_PI / 180.0;
        *ra = atan2(sin(lon) * cos(eps) - tan(latp) * sin(eps), cos(lon));
        *dec = asin(sin(latp) * cos(eps) + cos(latp) * sin(eps) * sin(lon));
    }

    static void bodyHelio(double d, int index, double *xh, double *yh, double *zh) {
        // N, i, w, a, e, M  (degrees / AU / deg per day) — Schlyter 2000
        double N, i, w, a, e, M;
        if (index < 0) { // Sun / Earth orbit
            N = 0.0;
            i = 0.0;
            w = 282.9404 + 4.70935E-5 * d;
            a = 1.0;
            e = 0.016709 - 1.151E-9 * d;
            M = 356.0470 + 0.9856002585 * d;
        } else if (index == 0) { // Venus
            N = 76.6799 + 2.46590E-5 * d;
            i = 3.3946 + 2.75E-8 * d;
            w = 54.8910 + 1.38374E-5 * d;
            a = 0.723330;
            e = 0.006773 - 1.302E-9 * d;
            M = 48.0052 + 1.6021302244 * d;
        } else if (index == 1) { // Mars
            N = 49.5574 + 2.11081E-5 * d;
            i = 1.8497 - 1.78E-8 * d;
            w = 286.5016 + 2.92961E-5 * d;
            a = 1.523688;
            e = 0.093405 + 2.516E-9 * d;
            M = 18.6021 + 0.5240207766 * d;
        } else if (index == 2) { // Jupiter
            N = 100.4542 + 2.76854E-5 * d;
            i = 1.3030 - 1.557E-7 * d;
            w = 273.8777 + 1.64505E-5 * d;
            a = 5.20256;
            e = 0.048498 + 4.469E-9 * d;
            M = 19.8950 + 0.0830853001 * d;
        } else { // Saturn
            N = 113.6634 + 2.38980E-5 * d;
            i = 2.4886 - 1.081E-7 * d;
            w = 339.3939 + 2.97661E-5 * d;
            a = 9.55475;
            e = 0.055546 - 9.499E-9 * d;
            M = 316.9670 + 0.0334442282 * d;
        }

        N = wrap360(N) * M_PI / 180.0;
        i = i * M_PI / 180.0;
        w = wrap360(w) * M_PI / 180.0;
        M = wrap360(M) * M_PI / 180.0;

        double E = M;
        for (int k = 0; k < 6; k++) {
            E = M + e * sin(E);
        }
        double xv = a * (cos(E) - e);
        double yv = a * sqrt(1.0 - e * e) * sin(E);
        double v = atan2(yv, xv);
        double r = sqrt(xv * xv + yv * yv);
        double vw = v + w;
        *xh = r * (cos(N) * cos(vw) - sin(N) * sin(vw) * cos(i));
        *yh = r * (sin(N) * cos(vw) + cos(N) * sin(vw) * cos(i));
        *zh = r * (sin(vw) * sin(i));
    }
};

#endif // ASTRONOMY_H
