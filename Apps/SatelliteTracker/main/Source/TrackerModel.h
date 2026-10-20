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
    /** True once passResult belongs to the current orbit data and observer. */
    bool passKnown = false;
    /** Unique for every new orbit data in the model, so a pass search with older data is dropped. */
    uint32_t orbitVersion = 0;

    /** Download state, not saved: failed attempts and the earliest time of the next attempt for missing orbit data. */
    int downloadFailures = 0;
    int64_t retryAt = 0;
};

/** Pass searches that run outside the LVGL lock, see TrackerModel::collectPassJobs(). */
struct PassJob {
    uint32_t catalogNumber;
    uint32_t orbitVersion;
    orbit::Tle tle;
    orbit::PassSearchResult result;
    orbit::Pass pass;
};

struct PassJobs {
    int count = 0;
    PassJob jobs[MAX_TRACKED] {};
    orbit::Geodetic observer {};
    uint32_t observerVersion = 0;
    int64_t startTime = 0;
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
    /** Changes with every new observer location. */
    uint32_t observerChanges() const { return observerVersion; }

    void updatePositions(double unixTime);

    /** Searches passes that ended or are missing. Returns true when anything changed. */
    bool updatePasses(int64_t unixTime, int64_t searchSeconds);
    void invalidatePasses();

    /** True when a pass is missing or over. */
    bool passesStale(int64_t unixTime) const;
    /** Copies the stale satellites into jobs and marks them as searched. Returns false when there is nothing to search. */
    bool collectPassJobs(int64_t unixTime, PassJobs& out);
    /** Searches the passes of the jobs. Uses only the jobs, so it can run without the lock that guards the model. */
    static void runPassJobs(PassJobs& jobs, int64_t searchSeconds);
    /** Stores the results whose orbit data and observer did not change since they were collected. Returns true when any was stored. */
    bool applyPassJobs(const PassJobs& jobs);

    void buildScene(double unixTime, int focusedIndex, bool tracksForAll, map::Scene& out);

    /** The name from the orbit data without the part in brackets ("ISS" for "ISS (ZARYA)"). */
    static void makeShortName(const char* fullName, char* out, size_t outSize);

private:

    int freeSlot() const;
    bool slotUsed(int slot) const;
    bool passStale(const TrackedSatellite& entry, int64_t unixTime) const;

    std::array<std::unique_ptr<TrackedSatellite>, MAX_TRACKED> satellites;
    int satelliteCount = 0;
    int trackedLimit = DEFAULT_TRACKED_LIMIT;
    bool observerAvailable = false;
    orbit::Geodetic observerLocation {};
    uint32_t observerVersion = 0;
    uint32_t orbitVersions = 0;
};
