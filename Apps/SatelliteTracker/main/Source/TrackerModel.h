#pragma once

#include "MapRenderer.h"
#include "Orbit.h"

#include <array>
#include <cstdint>
#include <memory>

/** Upper bound for the arrays. The usable limit depends on the screen, see TrackerModel::setLimit(). */
constexpr int MAX_TRACKED = 12;
constexpr int DEFAULT_TRACKED_LIMIT = 6;
constexpr int SHORT_NAME_LENGTH = 16;
/** Passes that stay lower than this are skipped, they are hardly usable. */
constexpr double MIN_PASS_ELEVATION_DEG = 5.0;

struct TrackedSatellite {
    uint32_t catalogNumber = 0;
    char shortName[SHORT_NAME_LENGTH] {};
    int slot = 0;

    /** False until orbit data was downloaded or restored. tle and propagator are only usable when true. */
    bool hasOrbit = false;
    orbit::Tle tle {};
    orbit::Satellite propagator;

    bool positionValid = false;
    orbit::Geodetic position {};
    orbit::LookAngles look {};
    bool sunlit = false;
    double speedKmS = 0.0;

    orbit::PassSearchResult passResult = orbit::PassSearchResult::None;
    orbit::Pass nextPass {};
    int64_t passSearchedAt = 0;

    /** Download state, not saved: failed attempts and the earliest time of the next attempt for missing orbit data. */
    int downloadFailures = 0;
    int64_t retryAt = 0;
};

class TrackerModel {

public:

    int count() const { return satelliteCount; }
    int limit() const { return trackedLimit; }
    void setLimit(int value) { trackedLimit = value < 1 ? 1 : (value > MAX_TRACKED ? MAX_TRACKED : value); }
    TrackedSatellite& at(int index) { return *satellites[index]; }
    const TrackedSatellite& at(int index) const { return *satellites[index]; }

    /**
     * Adds a satellite without orbit data. Returns the index or -1 when full or already tracked. A slot of -1 picks a free one.
     * restoring skips the limit so a list saved on a bigger screen is kept.
     */
    int add(uint32_t catalogNumber, const char* shortName, int slot = -1, bool restoring = false);
    /** Adds a satellite with its orbit data. A null or empty shortName is made from the TLE name. Returns -1 when the TLE is not usable. */
    int add(const orbit::Tle& tle, const char* shortName);
    void remove(int index);
    int indexOfCatalog(uint32_t catalogNumber) const;

    /** Sets new orbit data. Returns false when the TLE belongs to another satellite, is not usable or did not change. */
    bool setTle(int index, const orbit::Tle& tle);

    void setObserver(bool available, const orbit::Geodetic& location);
    bool hasObserver() const { return observerAvailable; }
    const orbit::Geodetic& observer() const { return observerLocation; }

    void updatePositions(double unixTime);

    /** Searches passes that ended or are missing. Returns true when anything changed. */
    bool updatePasses(int64_t unixTime, int64_t searchSeconds);
    void invalidatePasses();

    void buildScene(double unixTime, int focusedIndex, bool tracksForAll, map::Scene& out);

    /** The name from the orbit data without the part in brackets ("ISS" for "ISS (ZARYA)"). */
    static void makeShortName(const char* fullName, char* out, size_t outSize);

private:

    int freeSlot() const;
    bool slotUsed(int slot) const;

    std::array<std::unique_ptr<TrackedSatellite>, MAX_TRACKED> satellites;
    int satelliteCount = 0;
    int trackedLimit = DEFAULT_TRACKED_LIMIT;
    bool observerAvailable = false;
    orbit::Geodetic observerLocation {};
};
