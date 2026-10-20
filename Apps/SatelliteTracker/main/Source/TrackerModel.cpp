#include "TrackerModel.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr int TRACK_POINTS = 90;

void appendTrack(orbit::Satellite& satellite, double from, double to, std::vector<map::GeoPoint>& out) {
    out.clear();
    out.reserve(TRACK_POINTS + 1);
    double step = (to - from) / TRACK_POINTS;
    for (int i = 0; i <= TRACK_POINTS; i++) {
        orbit::Geodetic g;
        if (satellite.geodetic(from + step * i, g)) {
            out.push_back(map::GeoPoint { (float)g.latDeg, (float)g.lonDeg });
        }
    }
}

}

bool TrackerModel::slotUsed(int slot) const {
    for (int i = 0; i < satelliteCount; i++) {
        if (satellites[i]->slot == slot) return true;
    }
    return false;
}

int TrackerModel::freeSlot() const {
    for (int slot = 0; slot < MAX_TRACKED; slot++) {
        if (!slotUsed(slot)) return slot;
    }
    return -1;
}

int TrackerModel::add(uint32_t catalogNumber, const char* shortName, int slot, bool restoring) {
    if (catalogNumber == 0 || satelliteCount >= MAX_TRACKED || (!restoring && satelliteCount >= trackedLimit)) return -1;
    if (indexOfCatalog(catalogNumber) >= 0) return -1;
    auto entry = std::make_unique<TrackedSatellite>();
    entry->catalogNumber = catalogNumber;
    if (shortName != nullptr && shortName[0] != '\0') {
        snprintf(entry->shortName, sizeof(entry->shortName), "%s", shortName);
    } else {
        snprintf(entry->shortName, sizeof(entry->shortName), "%u", (unsigned)catalogNumber);
    }
    entry->slot = (slot >= 0 && slot < MAX_TRACKED && !slotUsed(slot)) ? slot : freeSlot();
    satellites[satelliteCount] = std::move(entry);
    return satelliteCount++;
}

int TrackerModel::add(const orbit::Tle& tle, const char* shortName) {
    orbit::Satellite check;
    if (!check.init(tle)) return -1;
    char automatic[SHORT_NAME_LENGTH];
    if (shortName == nullptr || shortName[0] == '\0') {
        makeShortName(tle.name, automatic, sizeof(automatic));
        shortName = automatic;
    }
    int index = add(orbit::tleCatalogNumber(tle), shortName);
    if (index >= 0) setTle(index, tle);
    return index;
}

void TrackerModel::remove(int index) {
    if (index < 0 || index >= satelliteCount) return;
    for (int i = index; i < satelliteCount - 1; i++) {
        satellites[i] = std::move(satellites[i + 1]);
    }
    satellites[--satelliteCount].reset();
}

int TrackerModel::indexOfCatalog(uint32_t catalogNumber) const {
    for (int i = 0; i < satelliteCount; i++) {
        if (satellites[i]->catalogNumber == catalogNumber) return i;
    }
    return -1;
}

bool TrackerModel::setTle(int index, const orbit::Tle& tle) {
    if (index < 0 || index >= satelliteCount) return false;
    TrackedSatellite& entry = *satellites[index];
    if (orbit::tleCatalogNumber(tle) != entry.catalogNumber) return false;
    if (entry.hasOrbit && memcmp(entry.tle.line1, tle.line1, sizeof(tle.line1)) == 0 && memcmp(entry.tle.line2, tle.line2, sizeof(tle.line2)) == 0) {
        return false;
    }
    if (!entry.propagator.init(tle)) {
        // The previous orbit data stays in use.
        if (entry.hasOrbit) entry.propagator.init(entry.tle);
        return false;
    }
    entry.tle = tle;
    entry.hasOrbit = true;
    entry.passResult = orbit::PassSearchResult::None;
    entry.passSearchedAt = 0;
    entry.passKnown = false;
    entry.orbitVersion = ++orbitVersions;
    return true;
}

void TrackerModel::setObserver(bool available, const orbit::Geodetic& location) {
    bool changed = available != observerAvailable || location.latDeg != observerLocation.latDeg || location.lonDeg != observerLocation.lonDeg;
    observerAvailable = available;
    observerLocation = location;
    if (changed) {
        observerVersion++;
        invalidatePasses();
    }
}

void TrackerModel::updatePositions(double unixTime) {
    for (int i = 0; i < satelliteCount; i++) {
        TrackedSatellite& entry = *satellites[i];
        orbit::Vec3 r, v;
        entry.positionValid = entry.hasOrbit && entry.propagator.positionEci(unixTime, r, v);
        if (!entry.positionValid) continue;
        entry.position = orbit::eciToGeodetic(r, unixTime);
        entry.sunlit = orbit::isSunlit(r, unixTime);
        entry.speedKmS = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (observerAvailable) {
            entry.propagator.lookAngles(observerLocation, unixTime, entry.look);
        }
    }
}

bool TrackerModel::passStale(const TrackedSatellite& entry, int64_t unixTime) const {
    if (!entry.hasOrbit) return false;
    if (entry.passSearchedAt == 0) return true;
    if (entry.passResult == orbit::PassSearchResult::Found) return entry.nextPass.setTime < unixTime;
    return unixTime - entry.passSearchedAt > 3600;
}

bool TrackerModel::passesStale(int64_t unixTime) const {
    if (!observerAvailable) return false;
    for (int i = 0; i < satelliteCount; i++) {
        if (passStale(*satellites[i], unixTime)) return true;
    }
    return false;
}

bool TrackerModel::collectPassJobs(int64_t unixTime, PassJobs& out) {
    out.count = 0;
    if (!observerAvailable) return false;
    out.observer = observerLocation;
    out.observerVersion = observerVersion;
    out.startTime = unixTime;
    for (int i = 0; i < satelliteCount; i++) {
        TrackedSatellite& entry = *satellites[i];
        if (!passStale(entry, unixTime)) continue;
        PassJob& job = out.jobs[out.count++];
        job.catalogNumber = entry.catalogNumber;
        job.orbitVersion = entry.orbitVersion;
        job.tle = entry.tle;
        entry.passSearchedAt = unixTime;
    }
    return out.count > 0;
}

void TrackerModel::runPassJobs(PassJobs& jobs, int64_t searchSeconds) {
    auto propagator = std::make_unique<orbit::Satellite>();
    for (int i = 0; i < jobs.count; i++) {
        PassJob& job = jobs.jobs[i];
        job.pass = orbit::Pass {};
        job.result = propagator->init(job.tle)
            ? propagator->findNextPass(jobs.observer, jobs.startTime, searchSeconds, MIN_PASS_ELEVATION_DEG, job.pass)
            : orbit::PassSearchResult::Error;
    }
}

bool TrackerModel::applyPassJobs(const PassJobs& jobs) {
    if (jobs.observerVersion != observerVersion) return false;
    bool stored = false;
    for (int i = 0; i < jobs.count; i++) {
        const PassJob& job = jobs.jobs[i];
        int index = indexOfCatalog(job.catalogNumber);
        if (index < 0) continue;
        TrackedSatellite& entry = *satellites[index];
        if (entry.orbitVersion != job.orbitVersion) continue;
        entry.passResult = job.result;
        entry.nextPass = job.pass;
        entry.passKnown = true;
        stored = true;
    }
    return stored;
}

bool TrackerModel::updatePasses(int64_t unixTime, int64_t searchSeconds) {
    PassJobs jobs;
    if (!collectPassJobs(unixTime, jobs)) return false;
    runPassJobs(jobs, searchSeconds);
    return applyPassJobs(jobs);
}

void TrackerModel::invalidatePasses() {
    for (int i = 0; i < satelliteCount; i++) {
        satellites[i]->passSearchedAt = 0;
        satellites[i]->passKnown = false;
    }
}

void TrackerModel::buildScene(double unixTime, int focusedIndex, bool tracksForAll, map::Scene& out) {
    orbit::Geodetic sun = orbit::subsolarPoint(unixTime);
    out.showNight = true;
    out.subsolar = map::GeoPoint { (float)sun.latDeg, (float)sun.lonDeg };
    out.hasObserver = observerAvailable;
    out.observer = map::GeoPoint { (float)observerLocation.latDeg, (float)observerLocation.lonDeg };
    out.satellites.resize(satelliteCount);
    for (int i = 0; i < satelliteCount; i++) {
        TrackedSatellite& entry = *satellites[i];
        map::SatelliteOverlay& overlay = out.satellites[i];
        overlay.visible = entry.positionValid;
        overlay.position = map::GeoPoint { (float)entry.position.latDeg, (float)entry.position.lonDeg };
        overlay.shape = static_cast<map::MarkerShape>(entry.slot % map::MARKER_SHAPE_COUNT);
        overlay.hollow = entry.slot >= map::MARKER_SHAPE_COUNT;
        overlay.colorIndex = entry.slot;
        overlay.focused = i == focusedIndex;
        overlay.drawTrack = entry.positionValid && (overlay.focused || tracksForAll);
        overlay.footprintRadiusDeg = overlay.drawTrack ? (float)orbit::footprintRadiusDeg(entry.position.altKm) : 0.0f;
        if (overlay.drawTrack) {
            double period = entry.propagator.periodMinutes() * 60.0;
            if (period > 6.0 * 3600.0) period = 6.0 * 3600.0;
            appendTrack(entry.propagator, unixTime - period * 0.5, unixTime, overlay.pastTrack);
            appendTrack(entry.propagator, unixTime, unixTime + period, overlay.futureTrack);
        } else {
            overlay.pastTrack.clear();
            overlay.futureTrack.clear();
        }
    }
}

void TrackerModel::makeShortName(const char* fullName, char* out, size_t outSize) {
    if (outSize == 0) return;
    size_t length = 0;
    const char* bracket = strchr(fullName, '(');
    size_t end = bracket != nullptr && bracket != fullName ? (size_t)(bracket - fullName) : strlen(fullName);
    while (end > 0 && fullName[end - 1] == ' ') end--;
    for (size_t i = 0; i < end && length + 1 < outSize; i++) {
        out[length++] = fullName[i];
    }
    out[length] = '\0';
}
