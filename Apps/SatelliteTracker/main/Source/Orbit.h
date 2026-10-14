#pragma once

#include <cstdint>

struct ElsetRec;

namespace orbit {

constexpr double EARTH_RADIUS_KM = 6378.137;

struct Vec3 {
    double x;
    double y;
    double z;
};

struct Tle {
    char name[25];
    char line1[70];
    char line2[70];
};

struct Geodetic {
    double latDeg;
    double lonDeg;
    double altKm;
};

struct LookAngles {
    double azimuthDeg;
    double elevationDeg;
    double rangeKm;
};

struct Pass {
    int64_t riseTime;
    int64_t maxTime;
    int64_t setTime;
    double maxElevationDeg;
    double riseAzimuthDeg;
    double setAzimuthDeg;
};

enum class PassSearchResult {
    Found,
    None,
    AlwaysUp,
    Error,
};

bool tleLineValid(const char* line, char expectedLineNumber);

/** Copies and validates a TLE. The name is trimmed. Returns false on a bad line length or checksum. */
bool parseTle(const char* name, const char* line1, const char* line2, Tle& out);

/** NORAD catalog number, including the Alpha-5 form (A0001 = 100001). Returns 0 when invalid. */
uint32_t tleCatalogNumber(const Tle& tle);

double julianDateFromUnix(double unixTime);

/** Greenwich mean sidereal time in radians. */
double gmstFromUnix(double unixTime);

Geodetic eciToGeodetic(const Vec3& eciKm, double unixTime);

Vec3 geodeticToEcef(const Geodetic& geodetic);

/** Point on Earth where the sun is in the zenith (low precision, about 0.01 degree). */
Geodetic subsolarPoint(double unixTime);

/** Unit vector to the sun in the TEME/ECI frame. */
Vec3 sunDirectionEci(double unixTime);

bool isSunlit(const Vec3& eciKm, double unixTime);

/** Angular radius (degrees of arc on the ground) of the area that sees the satellite above the horizon. */
double footprintRadiusDeg(double altitudeKm);

/** Point at the given angular distance and bearing from a start point on the sphere. */
void destinationPoint(double latDeg, double lonDeg, double distanceDeg, double bearingDeg, double& outLatDeg, double& outLonDeg);

class Satellite {

public:

    Satellite();
    ~Satellite();
    Satellite(const Satellite&) = delete;
    Satellite& operator=(const Satellite&) = delete;

    bool init(const Tle& tle);
    bool isValid() const { return valid; }

    /** TEME position and velocity in km and km/s. Returns false when SGP4 reports an error (for example a decayed orbit). */
    bool positionEci(double unixTime, Vec3& outPosition, Vec3& outVelocity);
    bool geodetic(double unixTime, Geodetic& out);
    bool lookAngles(const Geodetic& observer, double unixTime, LookAngles& out);

    double epochUnix() const { return epoch; }
    double periodMinutes() const;

    /**
     * Searches the next pass that is in progress at startTime or begins within searchSeconds.
     * Passes lower than minElevationDeg at their highest point are skipped.
     */
    PassSearchResult findNextPass(const Geodetic& observer, int64_t startTime, int64_t searchSeconds, double minElevationDeg, Pass& out);

private:

    bool elevationAt(const Geodetic& observer, double unixTime, double& outElevation);
    int64_t refineCrossing(const Geodetic& observer, int64_t below, int64_t above);

    ElsetRec* record = nullptr;
    double epoch = 0.0;
    double meanMotion = 0.0;
    bool valid = false;
};

}
