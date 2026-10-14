#include "Orbit.h"

#include "Sgp4Bridge.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace orbit {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;
constexpr double WGS84_A = 6378.137;
constexpr double WGS84_F = 1.0 / 298.257223563;
constexpr double UNIX_EPOCH_JD = 2440587.5;
constexpr int64_t RISE_SET_STEP_SECONDS = 20;
constexpr int64_t MAX_PASS_SECONDS = 12 * 3600;

double wrapDegrees180(double deg) {
    deg = std::fmod(deg + 180.0, 360.0);
    if (deg < 0.0) deg += 360.0;
    return deg - 180.0;
}

double wrapDegrees360(double deg) {
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0) deg += 360.0;
    return deg;
}

bool parseField(const char* line, int start, int length, double& out) {
    char buffer[24];
    if (length >= (int)sizeof(buffer)) return false;
    memcpy(buffer, line + start, length);
    buffer[length] = '\0';
    char* end = nullptr;
    out = strtod(buffer, &end);
    while (end && *end == ' ') end++;
    return end != buffer && end && *end == '\0';
}

/** Parses the TLE notation " 12345-3" which means 0.12345e-3. */
bool parseImpliedExponent(const char* line, int start, double& out) {
    char mantissa[8] = "0.";
    char sign = line[start];
    for (int i = 0; i < 5; i++) {
        char c = line[start + 1 + i];
        mantissa[2 + i] = (c == ' ') ? '0' : c;
    }
    mantissa[7] = '\0';
    double value = strtod(mantissa, nullptr);
    char expSign = line[start + 6];
    char expDigit = line[start + 7];
    if (expDigit < '0' || expDigit > '9') return false;
    int exponent = expDigit - '0';
    if (expSign == '-') exponent = -exponent;
    value *= std::pow(10.0, exponent);
    out = (sign == '-') ? -value : value;
    return true;
}

double julianDayOfJanuaryFirst(int year) {
    return 367.0 * year - std::floor(7.0 * year * 0.25) + 30.0 + 1.0 + 1721013.5;
}

double lengthOf(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 eciToEcef(const Vec3& eci, double gmst) {
    double c = std::cos(gmst);
    double s = std::sin(gmst);
    return Vec3 { c * eci.x + s * eci.y, -s * eci.x + c * eci.y, eci.z };
}

Geodetic ecefToGeodetic(const Vec3& ecef) {
    const double e2 = WGS84_F * (2.0 - WGS84_F);
    double p = std::sqrt(ecef.x * ecef.x + ecef.y * ecef.y);
    double lon = std::atan2(ecef.y, ecef.x);
    double lat = std::atan2(ecef.z, p * (1.0 - e2));
    double height = 0.0;
    for (int i = 0; i < 5; i++) {
        double sinLat = std::sin(lat);
        double n = WGS84_A / std::sqrt(1.0 - e2 * sinLat * sinLat);
        height = p / std::cos(lat) - n;
        lat = std::atan2(ecef.z, p * (1.0 - e2 * n / (n + height)));
    }
    return Geodetic { lat / DEG, wrapDegrees180(lon / DEG), height };
}

void sunEclipticLongitude(double unixTime, double& outLambda, double& outObliquity) {
    double n = julianDateFromUnix(unixTime) - 2451545.0;
    double meanLongitude = wrapDegrees360(280.460 + 0.9856474 * n);
    double meanAnomaly = wrapDegrees360(357.528 + 0.9856003 * n) * DEG;
    outLambda = (meanLongitude + 1.915 * std::sin(meanAnomaly) + 0.020 * std::sin(2.0 * meanAnomaly)) * DEG;
    outObliquity = (23.439 - 0.0000004 * n) * DEG;
}

}

bool tleLineValid(const char* line, char expectedLineNumber) {
    if (line == nullptr || strlen(line) < 69 || line[0] != expectedLineNumber || line[1] != ' ') return false;
    int sum = 0;
    for (int i = 0; i < 68; i++) {
        char c = line[i];
        if (c >= '0' && c <= '9') sum += c - '0';
        else if (c == '-') sum += 1;
    }
    char check = line[68];
    return check >= '0' && check <= '9' && (sum % 10) == (check - '0');
}

bool parseTle(const char* name, const char* line1, const char* line2, Tle& out) {
    if (!tleLineValid(line1, '1') || !tleLineValid(line2, '2')) return false;
    if (strncmp(line1 + 2, line2 + 2, 5) != 0) return false;

    memset(&out, 0, sizeof(out));
    if (name != nullptr) {
        while (*name == ' ') name++;
        if (name[0] == '0' && name[1] == ' ') name += 2;
        strncpy(out.name, name, sizeof(out.name) - 1);
        size_t len = strlen(out.name);
        while (len > 0 && (out.name[len - 1] == ' ' || out.name[len - 1] == '\r' || out.name[len - 1] == '\n')) {
            out.name[--len] = '\0';
        }
    }
    memcpy(out.line1, line1, 69);
    memcpy(out.line2, line2, 69);
    if (out.name[0] == '\0') {
        memcpy(out.name, line1 + 2, 5);
    }
    return true;
}

uint32_t tleCatalogNumber(const Tle& tle) {
    const char* field = tle.line1 + 2;
    uint32_t value = 0;
    int first = field[0];
    if (first >= 'A' && first <= 'Z' && first != 'I' && first != 'O') {
        int letterValue = first - 'A' + 10;
        if (first > 'I') letterValue--;
        if (first > 'O') letterValue--;
        value = (uint32_t)letterValue;
    } else if (first >= '0' && first <= '9') {
        value = (uint32_t)(first - '0');
    } else if (first != ' ') {
        return 0;
    }
    for (int i = 1; i < 5; i++) {
        char c = field[i];
        if (c == ' ') c = '0';
        if (c < '0' || c > '9') return 0;
        value = value * 10 + (uint32_t)(c - '0');
    }
    return value;
}

double julianDateFromUnix(double unixTime) {
    return unixTime / 86400.0 + UNIX_EPOCH_JD;
}

double gmstFromUnix(double unixTime) {
    return sgp4_bridge_gmst(julianDateFromUnix(unixTime));
}

Geodetic eciToGeodetic(const Vec3& eciKm, double unixTime) {
    return ecefToGeodetic(eciToEcef(eciKm, gmstFromUnix(unixTime)));
}

Vec3 geodeticToEcef(const Geodetic& geodetic) {
    const double e2 = WGS84_F * (2.0 - WGS84_F);
    double lat = geodetic.latDeg * DEG;
    double lon = geodetic.lonDeg * DEG;
    double sinLat = std::sin(lat);
    double n = WGS84_A / std::sqrt(1.0 - e2 * sinLat * sinLat);
    double r = (n + geodetic.altKm) * std::cos(lat);
    return Vec3 { r * std::cos(lon), r * std::sin(lon), (n * (1.0 - e2) + geodetic.altKm) * sinLat };
}

Geodetic subsolarPoint(double unixTime) {
    double lambda, obliquity;
    sunEclipticLongitude(unixTime, lambda, obliquity);
    double rightAscension = std::atan2(std::cos(obliquity) * std::sin(lambda), std::cos(lambda));
    double declination = std::asin(std::sin(obliquity) * std::sin(lambda));
    double lon = (rightAscension - gmstFromUnix(unixTime)) / DEG;
    return Geodetic { declination / DEG, wrapDegrees180(lon), 0.0 };
}

Vec3 sunDirectionEci(double unixTime) {
    double lambda, obliquity;
    sunEclipticLongitude(unixTime, lambda, obliquity);
    return Vec3 {
        std::cos(lambda),
        std::cos(obliquity) * std::sin(lambda),
        std::sin(obliquity) * std::sin(lambda)
    };
}

bool isSunlit(const Vec3& eciKm, double unixTime) {
    Vec3 sun = sunDirectionEci(unixTime);
    double along = eciKm.x * sun.x + eciKm.y * sun.y + eciKm.z * sun.z;
    if (along >= 0.0) return true;
    Vec3 perpendicular { eciKm.x - along * sun.x, eciKm.y - along * sun.y, eciKm.z - along * sun.z };
    return lengthOf(perpendicular) > EARTH_RADIUS_KM;
}

double footprintRadiusDeg(double altitudeKm) {
    if (altitudeKm <= 0.0) return 0.0;
    return std::acos(EARTH_RADIUS_KM / (EARTH_RADIUS_KM + altitudeKm)) / DEG;
}

void destinationPoint(double latDeg, double lonDeg, double distanceDeg, double bearingDeg, double& outLatDeg, double& outLonDeg) {
    double lat1 = latDeg * DEG;
    double lon1 = lonDeg * DEG;
    double d = distanceDeg * DEG;
    double b = bearingDeg * DEG;
    double lat2 = std::asin(std::sin(lat1) * std::cos(d) + std::cos(lat1) * std::sin(d) * std::cos(b));
    double lon2 = lon1 + std::atan2(std::sin(b) * std::sin(d) * std::cos(lat1), std::cos(d) - std::sin(lat1) * std::sin(lat2));
    outLatDeg = lat2 / DEG;
    outLonDeg = wrapDegrees180(lon2 / DEG);
}

Satellite::Satellite() = default;

Satellite::~Satellite() {
    if (record != nullptr) {
        sgp4_bridge_destroy(record);
    }
}

bool Satellite::init(const Tle& tle) {
    valid = false;
    if (record == nullptr) {
        record = sgp4_bridge_create();
        if (record == nullptr) return false;
    }

    const char* l1 = tle.line1;
    const char* l2 = tle.line2;
    double epochYear, epochDay;
    Sgp4Elements elements {};
    if (!parseField(l1, 18, 2, epochYear) || !parseField(l1, 20, 12, epochDay)) return false;
    if (!parseField(l1, 33, 10, elements.meanMotionDot)) return false;
    if (!parseImpliedExponent(l1, 44, elements.meanMotionDotDot)) return false;
    if (!parseImpliedExponent(l1, 53, elements.bstar)) return false;
    if (!parseField(l2, 8, 8, elements.inclinationDeg)) return false;
    if (!parseField(l2, 17, 8, elements.raanDeg)) return false;
    double eccentricityDigits;
    if (!parseField(l2, 26, 7, eccentricityDigits)) return false;
    elements.eccentricity = eccentricityDigits * 1.0e-7;
    if (!parseField(l2, 34, 8, elements.argumentOfPerigeeDeg)) return false;
    if (!parseField(l2, 43, 8, elements.meanAnomalyDeg)) return false;
    if (!parseField(l2, 52, 11, elements.meanMotionRevPerDay)) return false;
    if (elements.meanMotionRevPerDay <= 0.0) return false;

    int year = (int)epochYear;
    year += (year < 57) ? 2000 : 1900;
    double wholeDay = std::floor(epochDay);
    elements.epochJulianDay = julianDayOfJanuaryFirst(year) + wholeDay - 1.0;
    elements.epochJulianDayFraction = epochDay - wholeDay;

    if (!sgp4_bridge_init(record, &elements)) return false;

    epoch = ((elements.epochJulianDay - UNIX_EPOCH_JD) + elements.epochJulianDayFraction) * 86400.0;
    meanMotion = elements.meanMotionRevPerDay;
    valid = true;
    return true;
}

double Satellite::periodMinutes() const {
    return meanMotion > 0.0 ? 1440.0 / meanMotion : 0.0;
}

bool Satellite::positionEci(double unixTime, Vec3& outPosition, Vec3& outVelocity) {
    if (!valid) return false;
    double r[3], v[3];
    if (!sgp4_bridge_propagate(record, (unixTime - epoch) / 60.0, r, v)) return false;
    outPosition = Vec3 { r[0], r[1], r[2] };
    outVelocity = Vec3 { v[0], v[1], v[2] };
    return true;
}

bool Satellite::geodetic(double unixTime, Geodetic& out) {
    Vec3 r, v;
    if (!positionEci(unixTime, r, v)) return false;
    out = eciToGeodetic(r, unixTime);
    return true;
}

bool Satellite::lookAngles(const Geodetic& observer, double unixTime, LookAngles& out) {
    Vec3 r, v;
    if (!positionEci(unixTime, r, v)) return false;
    Vec3 satellite = eciToEcef(r, gmstFromUnix(unixTime));
    Vec3 station = geodeticToEcef(observer);
    Vec3 range { satellite.x - station.x, satellite.y - station.y, satellite.z - station.z };

    double lat = observer.latDeg * DEG;
    double lon = observer.lonDeg * DEG;
    double sinLat = std::sin(lat), cosLat = std::cos(lat);
    double sinLon = std::sin(lon), cosLon = std::cos(lon);
    double south = sinLat * cosLon * range.x + sinLat * sinLon * range.y - cosLat * range.z;
    double east = -sinLon * range.x + cosLon * range.y;
    double up = cosLat * cosLon * range.x + cosLat * sinLon * range.y + sinLat * range.z;

    double distance = lengthOf(range);
    out.rangeKm = distance;
    out.elevationDeg = std::asin(up / distance) / DEG;
    out.azimuthDeg = wrapDegrees360(std::atan2(east, -south) / DEG);
    return true;
}

bool Satellite::elevationAt(const Geodetic& observer, double unixTime, double& outElevation) {
    LookAngles angles;
    if (!lookAngles(observer, unixTime, angles)) return false;
    outElevation = angles.elevationDeg;
    return true;
}

int64_t Satellite::refineCrossing(const Geodetic& observer, int64_t below, int64_t above) {
    while (std::llabs(above - below) > 1) {
        int64_t middle = below + (above - below) / 2;
        double elevation;
        if (!elevationAt(observer, (double)middle, elevation)) break;
        if (elevation > 0.0) above = middle;
        else below = middle;
    }
    return above;
}

PassSearchResult Satellite::findNextPass(const Geodetic& observer, int64_t startTime, int64_t searchSeconds, double minElevationDeg, Pass& out) {
    if (!valid) return PassSearchResult::Error;

    int64_t endTime = startTime + searchSeconds;
    int64_t t = startTime;
    double elevation;
    if (!elevationAt(observer, (double)t, elevation)) return PassSearchResult::Error;

    if (elevation > 0.0) {
        int64_t back = t;
        double backElevation = elevation;
        while (backElevation > 0.0) {
            if (startTime - back > MAX_PASS_SECONDS) return PassSearchResult::AlwaysUp;
            back -= RISE_SET_STEP_SECONDS;
            if (!elevationAt(observer, (double)back, backElevation)) return PassSearchResult::Error;
        }
        t = back;
        elevation = backElevation;
    }

    while (true) {
        while (elevation <= 0.0) {
            int64_t step = RISE_SET_STEP_SECONDS;
            if (elevation < -2.5) {
                step = (int64_t)(-elevation * 8.0);
            }
            t += step;
            if (t > endTime) return PassSearchResult::None;
            if (!elevationAt(observer, (double)t, elevation)) return PassSearchResult::Error;
            if (elevation > 0.0) {
                t = refineCrossing(observer, t - step, t);
            }
        }

        Pass pass {};
        pass.riseTime = t;
        pass.maxTime = t;
        pass.maxElevationDeg = 0.0;

        int64_t s = t;
        double setElevation = elevation;
        while (setElevation > 0.0) {
            if (s - t > MAX_PASS_SECONDS) return PassSearchResult::AlwaysUp;
            if (setElevation > pass.maxElevationDeg) {
                pass.maxElevationDeg = setElevation;
                pass.maxTime = s;
            }
            s += RISE_SET_STEP_SECONDS;
            if (!elevationAt(observer, (double)s, setElevation)) return PassSearchResult::Error;
        }
        pass.setTime = refineCrossing(observer, s, s - RISE_SET_STEP_SECONDS);

        int64_t low = pass.maxTime - RISE_SET_STEP_SECONDS;
        int64_t high = pass.maxTime + RISE_SET_STEP_SECONDS;
        if (low < pass.riseTime) low = pass.riseTime;
        if (high > pass.setTime) high = pass.setTime;
        while (high - low > 2) {
            int64_t m1 = low + (high - low) / 3;
            int64_t m2 = high - (high - low) / 3;
            double e1, e2;
            if (!elevationAt(observer, (double)m1, e1) || !elevationAt(observer, (double)m2, e2)) return PassSearchResult::Error;
            if (e1 < e2) low = m1;
            else high = m2;
        }
        pass.maxTime = (low + high) / 2;
        elevationAt(observer, (double)pass.maxTime, pass.maxElevationDeg);

        LookAngles angles;
        if (lookAngles(observer, (double)pass.riseTime, angles)) pass.riseAzimuthDeg = angles.azimuthDeg;
        if (lookAngles(observer, (double)pass.setTime, angles)) pass.setAzimuthDeg = angles.azimuthDeg;

        if (pass.maxElevationDeg >= minElevationDeg) {
            out = pass;
            return PassSearchResult::Found;
        }

        t = pass.setTime + 1;
        if (t > endTime) return PassSearchResult::None;
        if (!elevationAt(observer, (double)t, elevation)) return PassSearchResult::Error;
    }
}

}
