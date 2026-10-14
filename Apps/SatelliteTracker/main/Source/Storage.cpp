#include "Storage.h"

#include <app/paths.h>
#include <tactility/preferences.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>

namespace {

constexpr const char* SETTINGS_FILE = "settings.properties";
/** The tracked satellites, written only when the user changes the list. */
constexpr const char* LIST_FILE = "satellites.txt";
/** The latest orbit data of the tracked satellites, written after downloads. */
constexpr const char* ORBIT_FILE = "orbits.tle";

void trimLineEnd(char* line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
}

FILE* openUserFile(const char* name) {
    char path[256];
    if (!storageEnsureDirectory() || !storageUserPath(name, path, sizeof(path))) return nullptr;
    return fopen(path, "w");
}

}

bool storageEnsureDirectory() {
    char dir[256];
    if (app_paths_get_user_data_directory(APP_ID, dir, sizeof(dir)) != ERROR_NONE) return false;
    for (char* p = dir + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(dir, 0755);
            *p = '/';
        }
    }
    mkdir(dir, 0755);
    return true;
}

bool storageUserPath(const char* fileName, char* out, size_t outSize) {
    return app_paths_get_user_data_path(APP_ID, fileName, out, outSize) == ERROR_NONE;
}

bool storageAssetPath(const char* fileName, char* out, size_t outSize) {
    return app_paths_get_assets_path(APP_ID, fileName, out, outSize) == ERROR_NONE;
}

void settingsLoad(Settings& settings) {
    char path[256];
    if (!storageUserPath(SETTINGS_FILE, path, sizeof(path))) return;
    Preferences* preferences = preferences_open(path);
    if (preferences == nullptr) return;
    int32_t latMicro = 0, lonMicro = 0, altM = 0;
    preferences_opt_bool(preferences, "observerSet", &settings.observerSet);
    if (preferences_opt_int32(preferences, "latMicro", &latMicro)) settings.latitudeDeg = latMicro / 1.0e6;
    if (preferences_opt_int32(preferences, "lonMicro", &lonMicro)) settings.longitudeDeg = lonMicro / 1.0e6;
    if (preferences_opt_int32(preferences, "altM", &altM)) settings.altitudeM = altM;
    preferences_opt_int32(preferences, "updateSeconds", &settings.updateSeconds);
    preferences_opt_bool(preferences, "tracksForAll", &settings.tracksForAll);
    preferences_opt_bool(preferences, "autoSelect", &settings.autoSelectPass);
    preferences_opt_bool(preferences, "hideToolbar", &settings.hideToolbar);
    preferences_opt_bool(preferences, "useDeviceLoc", &settings.useDeviceLocation);
    preferences_opt_int64(preferences, "lastRefresh", &settings.lastTrackedRefresh);
    preferences_close(preferences);
}

void settingsSave(const Settings& settings) {
    if (!storageEnsureDirectory()) return;
    char path[256];
    if (!storageUserPath(SETTINGS_FILE, path, sizeof(path))) return;
    Preferences* preferences = preferences_open(path);
    if (preferences == nullptr) return;
    preferences_put_bool(preferences, "observerSet", settings.observerSet);
    preferences_put_int32(preferences, "latMicro", (int32_t)(settings.latitudeDeg * 1.0e6));
    preferences_put_int32(preferences, "lonMicro", (int32_t)(settings.longitudeDeg * 1.0e6));
    preferences_put_int32(preferences, "altM", (int32_t)settings.altitudeM);
    preferences_put_int32(preferences, "updateSeconds", settings.updateSeconds);
    preferences_put_bool(preferences, "tracksForAll", settings.tracksForAll);
    preferences_put_bool(preferences, "autoSelect", settings.autoSelectPass);
    preferences_put_bool(preferences, "hideToolbar", settings.hideToolbar);
    preferences_put_bool(preferences, "useDeviceLoc", settings.useDeviceLocation);
    preferences_put_int64(preferences, "lastRefresh", settings.lastTrackedRefresh);
    preferences_close(preferences);
}

namespace {

/** Reads the list format "catalog|slot|short name". The short name is the rest of the line and may contain '|'. */
bool listLoad(const char* path, TrackerModel& model) {
    FILE* file = fopen(path, "r");
    if (file == nullptr) return false;
    char line[96];
    while (fgets(line, sizeof(line), file)) {
        trimLineEnd(line);
        char* end = nullptr;
        unsigned long catalog = strtoul(line, &end, 10);
        if (end == line || *end != '|') continue;
        char* slotText = end + 1;
        long slot = strtol(slotText, &end, 10);
        if (end == slotText || *end != '|') continue;
        model.add((uint32_t)catalog, end + 1, (int)slot, true);
    }
    fclose(file);
    return true;
}

}

bool trackedLoad(TrackerModel& model) {
    char path[256];
    if (!storageUserPath(LIST_FILE, path, sizeof(path)) || !listLoad(path, model)) return false;
    if (storageUserPath(ORBIT_FILE, path, sizeof(path))) {
        TleList tles;
        tleFileRead(path, tles, MAX_TRACKED * 2);
        for (const auto& tle : tles) {
            model.setTle(model.indexOfCatalog(orbit::tleCatalogNumber(tle)), tle);
        }
    }
    return true;
}

void trackedListSave(const TrackerModel& model) {
    FILE* file = openUserFile(LIST_FILE);
    if (file == nullptr) return;
    for (int i = 0; i < model.count(); i++) {
        const TrackedSatellite& entry = model.at(i);
        fprintf(file, "%u|%d|%s\n", (unsigned)entry.catalogNumber, entry.slot, entry.shortName);
    }
    fclose(file);
}

void orbitCacheSave(const TrackerModel& model) {
    FILE* file = openUserFile(ORBIT_FILE);
    if (file == nullptr) return;
    for (int i = 0; i < model.count(); i++) {
        const TrackedSatellite& entry = model.at(i);
        if (!entry.hasOrbit) continue;
        fprintf(file, "%s\n%.69s\n%.69s\n", entry.tle.name, entry.tle.line1, entry.tle.line2);
    }
    fclose(file);
}

bool tleFileRead(const char* path, TleList& out, size_t maxEntries) {
    out.clear();
    FILE* file = fopen(path, "r");
    if (file == nullptr) return false;
    // One allocation of the final size, growing step by step would need the old and the new block at the same time.
    out.reserve(maxEntries);
    char lines[3][96];
    int filled = 0;
    while (out.size() < maxEntries && fgets(lines[filled], sizeof(lines[filled]), file)) {
        trimLineEnd(lines[filled]);
        if (lines[filled][0] == '\0') continue;
        filled++;
        if (filled < 3) continue;
        orbit::Tle tle;
        if (orbit::parseTle(lines[0], lines[1], lines[2], tle)) {
            out.push_back(tle);
            filled = 0;
        } else {
            memmove(lines[0], lines[1], sizeof(lines[0]));
            memmove(lines[1], lines[2], sizeof(lines[1]));
            filled = 2;
        }
    }
    fclose(file);
    return true;
}

void sortByName(TleList& list) {
    std::sort(list.begin(), list.end(), [](const orbit::Tle& a, const orbit::Tle& b) { return strcasecmp(a.name, b.name) < 0; });
}

int64_t fileAgeSeconds(const char* path, int64_t now) {
    struct stat info;
    if (stat(path, &info) != 0) return -1;
    int64_t age = now - (int64_t)info.st_mtime;
    return age < 0 ? 0 : age;
}
