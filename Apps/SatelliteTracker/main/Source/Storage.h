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
    /** Last device location, used at the start until the location service has a fix, so the passes do not change then. */
    bool lastDeviceSet = false;
    double lastDeviceLatDeg = 0.0;
    double lastDeviceLonDeg = 0.0;
    double lastDeviceAltM = 0.0;
    /** When orbit data of a tracked satellite was last downloaded. */
    int64_t lastTrackedRefresh = 0;
};

bool storageEnsureDirectory();
bool storageUserPath(const char* fileName, char* out, size_t outSize);
bool storageAssetPath(const char* fileName, char* out, size_t outSize);
/** Reads an asset file as text. Returns nullptr when it cannot be read, else a buffer to free with largeFree(). */
char* storageReadAsset(const char* fileName);

void settingsLoad(Settings& settings);
void settingsSave(const Settings& settings);

/**
 * Restores the list of tracked satellites and their orbit data from the cache. Satellites without cached orbit data are
 * restored without it. Returns false when no list was saved yet, which means the app starts for the first time.
 */
bool trackedLoad(TrackerModel& model);

/** The saved part of the tracked satellites. It is copied from the model with the LVGL lock held and written without it. */
struct SavedSatellite {
    uint32_t catalogNumber;
    int slot;
    char shortName[SHORT_NAME_LENGTH];
    bool hasOrbit;
    orbit::Tle tle;
};

struct SavedList {
    int count = 0;
    SavedSatellite entries[MAX_TRACKED] {};
};

void savedListFromModel(const TrackerModel& model, SavedList& out);
/** Writes the list of tracked satellites: catalog number, slot and short name. */
void trackedListSave(const SavedList& list);
/** Writes the orbit data of the tracked satellites that have it. */
void orbitCacheSave(const SavedList& list);

/** Reads a CelesTrak TLE file (name line plus two element lines). Stops after maxEntries, which is also reserved up front. */
bool tleFileRead(const char* path, TleList& out, size_t maxEntries);

void sortByName(TleList& list);

/** Seconds since the file was written, or -1 when it does not exist. */
int64_t fileAgeSeconds(const char* path, int64_t now);
