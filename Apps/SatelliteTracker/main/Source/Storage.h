#pragma once

#include "LargeMemory.h"
#include "Orbit.h"
#include "TrackerModel.h"

#include <cstdint>
#include <vector>

constexpr const char* APP_ID = "tactility.satellitetracker";

using TleList = std::vector<orbit::Tle, LargeAllocator<orbit::Tle>>;

/** Until the user sets a location, New York City is used, so passes and the automatic selection work from the start. */
struct Settings {
    bool observerSet = true;
    double latitudeDeg = 40.7128;
    double longitudeDeg = -74.0060;
    double altitudeM = 10.0;
    /** 0 means the default for the display type. */
    int32_t updateSeconds = 0;
    bool tracksForAll = false;
    bool autoSelectPass = true;
    bool hideToolbar = false;
    /** Follow Tactility's location service (GPS or network) when it has a fix. */
    bool useDeviceLocation = true;
    /** When orbit data of a tracked satellite was last downloaded. */
    int64_t lastTrackedRefresh = 0;
};

bool storageEnsureDirectory();
bool storageUserPath(const char* fileName, char* out, size_t outSize);
bool storageAssetPath(const char* fileName, char* out, size_t outSize);

void settingsLoad(Settings& settings);
void settingsSave(const Settings& settings);

/**
 * Restores the list of tracked satellites and their orbit data from the cache. Satellites without cached orbit data are
 * restored without it. Returns false when no list was saved yet, which means the app starts for the first time.
 */
bool trackedLoad(TrackerModel& model);
/** Writes the list of tracked satellites: catalog number, slot and short name. */
void trackedListSave(const TrackerModel& model);
/** Writes the orbit data of the tracked satellites that have it. */
void orbitCacheSave(const TrackerModel& model);

/** Reads a CelesTrak TLE file (name line plus two element lines). Stops after maxEntries, which is also reserved up front. */
bool tleFileRead(const char* path, TleList& out, size_t maxEntries);

void sortByName(TleList& list);

/** Seconds since the file was written, or -1 when it does not exist. */
int64_t fileAgeSeconds(const char* path, int64_t now);
