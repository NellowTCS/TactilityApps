#include "SatelliteTracker.h"

#include "LargeMemory.h"

#include <app/event.h>
#include <lvgl/devices/keyboard.h>
#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/widgets/toolbar.h>
#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/memory.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

struct DefaultSatellite {
    uint32_t catalogNumber;
    const char* shortName;
};

/** Tracked on the first start: the ISS, the Chinese space station, the Hubble Space Telescope and XMM-Newton. */
constexpr DefaultSatellite DEFAULT_SATELLITES[] = {
    { 25544, "ISS" },
    { 48274, "CSS" },
    { 20580, "HST" },
    { 25989, "XMM-NEWTON" },
};

/** Missing orbit data is tried this many times at a short interval, then at the normal retry interval. */
constexpr int MISSING_ORBIT_QUICK_ATTEMPTS = 3;
constexpr int64_t MISSING_ORBIT_RETRY_SECONDS = 30;
constexpr int64_t REFRESH_RETRY_SECONDS = 15 * 60;

struct DisplayInfo {
    bool found;
    bool grayscale;
    bool slow;
};

bool onDisplayFound(Device* device, void* context) {
    auto* info = static_cast<DisplayInfo*>(context);
    auto format = display_get_color_format(device);
    info->found = true;
    info->grayscale = format == DISPLAY_COLOR_FORMAT_GRAYSCALE8 || format == DISPLAY_COLOR_FORMAT_MONOCHROME;
    info->slow = display_has_capability(device, DISPLAY_CAPABILITY_SLOW_REFRESH);
    return false;
}

void setStatus(Context* ctx, const char* text) {
    snprintf(ctx->statusText, sizeof(ctx->statusText), "%s", text);
    ctx->forceRedraw = true;
}

bool queueTracked(Context* ctx, uint32_t catalogNumber) {
    DownloadJob job;
    if (!Downloader::buildCatalogJob(catalogNumber, job) || ctx->downloader.hasQueued(DownloadKind::Tracked, job.tag)) return false;
    return ctx->downloader.enqueue(job);
}

/** Queues the tracked satellites that have orbit data, those without it, or both. */
bool queueRefresh(Context* ctx, bool withOrbit, bool withoutOrbit) {
    bool queued = false;
    for (int i = 0; i < ctx->model.count(); i++) {
        const TrackedSatellite& entry = ctx->model.at(i);
        if ((entry.hasOrbit ? withOrbit : withoutOrbit) && queueTracked(ctx, entry.catalogNumber)) queued = true;
    }
    if (queued) setStatus(ctx, "Updating satellite data...");
    return queued;
}

/** Satellites without orbit data (the first start, or a list restored without the cache) are downloaded as soon as possible. */
void requestMissingOrbits(Context* ctx, int64_t now) {
    bool queued = false;
    for (int i = 0; i < ctx->model.count(); i++) {
        TrackedSatellite& entry = ctx->model.at(i);
        if (entry.hasOrbit || now < entry.retryAt) continue;
        if (queueTracked(ctx, entry.catalogNumber)) queued = true;
        // Until the result is handled. The download leaves the queue before its result reaches the LVGL task.
        entry.retryAt = now + REFRESH_RETRY_SECONDS;
    }
    if (queued) setStatus(ctx, "Getting satellite data...");
}

void scheduleDownloads(Context* ctx) {
    if (!timeIsValid()) return;
    int64_t now = currentTime();
    requestMissingOrbits(ctx, now);
    int64_t age = orbitDataAge(ctx);
    int64_t needed = ctx->startupRefreshDone ? TRACKED_REFRESH_SECONDS : TRACKED_MIN_REFRESH_SECONDS;
    if (age >= 0 && age < needed) {
        ctx->startupRefreshDone = true;
        return;
    }
    if (now - ctx->lastRefreshAttempt < REFRESH_RETRY_SECONDS || ctx->downloader.isBusy()) return;
    ctx->lastRefreshAttempt = now;
    ctx->startupRefreshDone = true;
    // Satellites without orbit data have their own schedule in requestMissingOrbits().
    queueRefresh(ctx, true, false);
}

/** Orbit data and the download time are written once per batch of downloads, flash writes are slow on some devices. */
void saveDownloads(Context* ctx) {
    if (!ctx->downloadsUnsaved) return;
    ctx->downloadsUnsaved = false;
    orbitCacheSave(ctx->model);
    settingsSave(ctx->settings);
}

void freeMarkerIcons(Context* ctx) {
    for (auto& buffer : ctx->iconBuffers) {
        largeFree(buffer);
        buffer = nullptr;
    }
}

void onCloseClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onBackClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    switch (ctx->view) {
        case View::Groups:
        case View::Search:
        case View::Rename:
            showView(ctx, View::Satellites);
            break;
        case View::GroupList:
            showView(ctx, View::Groups);
            break;
        default:
            showView(ctx, View::Map);
            break;
    }
}

void onPassesClicked(lv_event_t* event) {
    showView(static_cast<Context*>(lv_event_get_user_data(event)), View::Passes);
}

void onSatellitesClicked(lv_event_t* event) {
    showView(static_cast<Context*>(lv_event_get_user_data(event)), View::Satellites);
}

void onSettingsClicked(lv_event_t* event) {
    showView(static_cast<Context*>(lv_event_get_user_data(event)), View::Settings);
}

const char* viewTitle(const Context* ctx) {
    switch (ctx->view) {
        case View::Map: return "Satellite Tracker";
        case View::Passes: return "Next passes";
        case View::Satellites: return "Satellites";
        case View::Groups: return "Add from group";
        case View::GroupList: return ctx->selectedGroupName;
        case View::Search: return "Search";
        case View::Rename: return "Rename";
        case View::Settings: return "Settings";
    }
    return "";
}

void onUpdateTimer(lv_timer_t* timer) {
    auto* ctx = static_cast<Context*>(lv_timer_get_user_data(timer));
    if (window_manager_get_state(ctx->window) != WINDOW_STATE_GRANTED) return;
    scheduleDownloads(ctx);
    if (ctx->view == View::Map) {
        mapViewUpdate(ctx, false);
    } else if (ctx->view == View::Settings) {
        settingsViewUpdate(ctx);
    }
}

void onTextAreaFocused(lv_event_t* event) {
    static_cast<Context*>(lv_event_get_user_data(event))->keyboardBound = true;
}

/**
 * Tactility hides the on-screen keyboard when its text area is deleted, but the keyboard keeps a pointer to it.
 * Focusing the next text area then touches the deleted one, so the keyboard is detached before our widgets go.
 */
void releaseKeyboard(Context* ctx) {
    if (!ctx->keyboardBound) return;
    ctx->keyboardBound = false;
    LvglSoftwareKeyboard* keyboard = lvgl_software_keyboard_get_last();
    if (keyboard != nullptr && keyboard->object != nullptr) {
        lv_keyboard_set_textarea(keyboard->object, nullptr);
        lvgl_software_keyboard_hide(keyboard);
    }
}

void buildView(Context* ctx) {
    if (ctx->root == nullptr) return;
    mapViewDestroy(ctx);
    releaseKeyboard(ctx);
    lv_obj_clean(ctx->root);
    ctx->mapWidgets = MapWidgets {};
    ctx->listWidget = nullptr;
    ctx->messageLabel = nullptr;
    ctx->textArea = nullptr;

    ctx->toolbar = nullptr;
    if (ctx->view != View::Map) {
        ctx->toolbar = lvgl_toolbar_create(ctx->root, viewTitle(ctx));
        lvgl_toolbar_set_nav_action(ctx->toolbar, LV_SYMBOL_LEFT, onBackClicked, ctx);
    } else if (!ctx->settings.hideToolbar) {
        ctx->toolbar = lvgl_toolbar_create(ctx->root, viewTitle(ctx));
        lvgl_toolbar_set_nav_action(ctx->toolbar, LV_SYMBOL_CLOSE, onCloseClicked, ctx);
        lv_obj_t* passesButton = lvgl_toolbar_add_text_button_action(ctx->toolbar, LVGL_ICON_SHARED_CALENDAR_MONTH, onPassesClicked, ctx);
        lv_obj_set_style_text_font(lv_obj_get_child(passesButton, 0), lvgl_get_shared_icon_default_font(), 0);
        lvgl_toolbar_add_text_button_action(ctx->toolbar, LV_SYMBOL_LIST, onSatellitesClicked, ctx);
        lvgl_toolbar_add_text_button_action(ctx->toolbar, LV_SYMBOL_SETTINGS, onSettingsClicked, ctx);
    }

    ctx->content = lv_obj_create(ctx->root);
    lv_obj_set_width(ctx->content, LV_PCT(100));
    lv_obj_set_flex_grow(ctx->content, 1);
    lv_obj_set_style_border_width(ctx->content, 0, 0);
    lv_obj_set_style_radius(ctx->content, 0, 0);
    lv_obj_update_layout(ctx->root);

    switch (ctx->view) {
        case View::Map: mapViewCreate(ctx); break;
        case View::Passes: passesViewCreate(ctx); break;
        case View::Satellites: satellitesViewCreate(ctx); break;
        case View::Groups: groupsViewCreate(ctx); break;
        case View::GroupList: groupListViewCreate(ctx); break;
        case View::Search: searchViewCreate(ctx); break;
        case View::Rename: renameViewCreate(ctx); break;
        case View::Settings: settingsViewCreate(ctx); break;
    }
}

void onViewTimer(lv_timer_t* timer) {
    auto* ctx = static_cast<Context*>(lv_timer_get_user_data(timer));
    ctx->viewTimer = nullptr;
    ctx->view = ctx->pendingView;
    if (ctx->view == View::Map) ctx->forceRedraw = true;
    buildView(ctx);
}

void handleTrackedDownload(Context* ctx, const DownloadResult& result) {
    uint32_t catalog = (uint32_t)strtoul(result.job.tag, nullptr, 10);
    int index = ctx->model.indexOfCatalog(catalog);
    int64_t now = currentTime();
    if (!result.success) {
        // Satellites that have orbit data keep it and are retried with the next refresh.
        if (index >= 0 && !ctx->model.at(index).hasOrbit) {
            TrackedSatellite& entry = ctx->model.at(index);
            entry.downloadFailures++;
            entry.retryAt = now + (entry.downloadFailures < MISSING_ORBIT_QUICK_ATTEMPTS ? MISSING_ORBIT_RETRY_SECONDS : REFRESH_RETRY_SECONDS);
        }
        return;
    }

    TleList tles;
    tleFileRead(result.job.target, tles, 4);
    remove(result.job.target);
    // The satellite may have been removed while its download was queued.
    if (index < 0) return;

    TrackedSatellite& entry = ctx->model.at(index);
    const orbit::Tle* received = nullptr;
    for (const auto& tle : tles) {
        if (orbit::tleCatalogNumber(tle) == catalog) received = &tle;
    }
    if (received != nullptr) {
        ctx->settings.lastTrackedRefresh = now;
        ctx->downloadsUnsaved = true;
        if (ctx->model.setTle(index, *received)) ctx->forceRedraw = true;
    }
    if (!entry.hasOrbit) {
        // CelesTrak has no usable data for this number (re-entered, or never existed). Asking again soon would not help.
        entry.retryAt = now + TRACKED_REFRESH_SECONDS;
        char text[64];
        snprintf(text, sizeof(text), "No orbit data for %s (%u)", entry.shortName, (unsigned)catalog);
        setStatus(ctx, text);
    } else if (!ctx->downloader.isBusy()) {
        setStatus(ctx, "");
    }
}

/** Fills the catalog from a group or search download, when it still belongs to the screen that asked for it. */
void handleCatalogDownload(Context* ctx, const DownloadResult& result) {
    bool current;
    size_t maxEntries;
    if (result.job.kind == DownloadKind::Group) {
        current = ctx->view == View::GroupList && strcmp(ctx->selectedGroup, result.job.tag) == 0;
        maxEntries = ctx->groupLimit;
    } else {
        DownloadJob expected;
        current = ctx->view == View::Search && Downloader::buildSearchJob(ctx->searchText, expected) && strcmp(expected.tag, result.job.tag) == 0;
        maxEntries = ctx->searchLimit;
    }
    if (!current) return;
    ctx->catalogLoading = false;
    // After a failed download a group falls back to its saved copy, which a failed download leaves untouched.
    bool fallback = !result.success && result.job.kind == DownloadKind::Group;
    if (result.success || fallback) {
        tleFileRead(result.job.target, ctx->catalog, maxEntries);
        sortByName(ctx->catalog);
    }
    catalogViewRefresh(ctx);
    if (fallback && !ctx->catalog.empty()) {
        setLabelText(ctx->messageLabel, "Download failed, this is the saved copy of the group.");
    }
}

}

void applyObserver(Context* ctx) {
    if (ctx->settings.useDeviceLocation && ctx->deviceFixValid) {
        ctx->model.setObserver(true, ctx->deviceFix);
        return;
    }
    orbit::Geodetic location { ctx->settings.latitudeDeg, ctx->settings.longitudeDeg, ctx->settings.altitudeM / 1000.0 };
    ctx->model.setObserver(ctx->settings.observerSet, location);
}

void satelliteTrackerSetDeviceLocation(Context* ctx, const Location* fix) {
    if (fix == nullptr) {
        if (!ctx->deviceFixValid) return;
        ctx->deviceFixValid = false;
    } else {
        ctx->deviceAccuracyM = fix->horizontal_accuracy;
        snprintf(ctx->deviceProvider, sizeof(ctx->deviceProvider), "%s", fix->provider);
        // Movements below about 1 km are ignored, so GPS noise does not recalculate the passes every second.
        bool moved = !ctx->deviceFixValid || std::fabs(fix->latitude - ctx->deviceFix.latDeg) > 0.01 ||
            std::fabs(fix->longitude - ctx->deviceFix.lonDeg) > 0.01;
        if (!moved) return;
        double altitudeM = std::isnan(fix->altitude) ? 0.0 : fix->altitude;
        ctx->deviceFix = orbit::Geodetic { fix->latitude, fix->longitude, altitudeM / 1000.0 };
        ctx->deviceFixValid = true;
    }
    applyObserver(ctx);
    ctx->forceRedraw = true;
}

int effectiveFocus(const Context* ctx) {
    int count = ctx->model.count();
    if (ctx->focusedIndex >= 0 && ctx->focusedIndex < count) return ctx->focusedIndex;
    if (ctx->settings.autoSelectPass && ctx->autoIndex >= 0 && ctx->autoIndex < count) return ctx->autoIndex;
    return -1;
}

int addTracked(Context* ctx, const orbit::Tle& tle) {
    int index = ctx->model.add(tle, nullptr);
    if (index < 0) return -1;
    trackedListSave(ctx->model);
    // The orbit data comes from a group or search download and goes into the cache with the next save.
    ctx->downloadsUnsaved = true;
    ctx->autoIndex = -1;
    ctx->forceRedraw = true;
    return index;
}

void removeTracked(Context* ctx, int index) {
    ctx->model.remove(index);
    trackedListSave(ctx->model);
    if (ctx->focusedIndex == index) ctx->focusedIndex = -1;
    else if (ctx->focusedIndex > index) ctx->focusedIndex--;
    ctx->autoIndex = -1;
    ctx->forceRedraw = true;
}

int64_t orbitDataAge(const Context* ctx) {
    int64_t last = ctx->settings.lastTrackedRefresh;
    if (last <= 0 || !timeIsValid()) return -1;
    int64_t age = currentTime() - last;
    return age < 0 ? -1 : age;
}

int64_t currentTime() {
    return (int64_t)time(nullptr);
}

bool timeIsValid() {
    return currentTime() >= MIN_VALID_TIME;
}

int effectiveUpdateSeconds(const Context* ctx) {
    if (ctx->settings.updateSeconds > 0) return ctx->settings.updateSeconds;
    return ctx->slowDisplay ? 60 : 5;
}

void setLabelText(lv_obj_t* label, const char* text) {
    if (label == nullptr) return;
    const char* current = lv_label_get_text(label);
    if (current != nullptr && strcmp(current, text) == 0) return;
    lv_label_set_text(label, text);
}

lv_obj_t* createTextArea(Context* ctx, lv_obj_t* parent) {
    lv_obj_t* textArea = lv_textarea_create(parent);
    lv_obj_add_event_cb(textArea, onTextAreaFocused, LV_EVENT_FOCUSED, ctx);
    return textArea;
}

void formatClock(int64_t unixTime, char* out, size_t outSize) {
    time_t value = (time_t)unixTime;
    struct tm local {};
    localtime_r(&value, &local);
    snprintf(out, outSize, "%02d:%02d", local.tm_hour, local.tm_min);
}

void formatDay(int64_t unixTime, char* out, size_t outSize) {
    time_t now = (time_t)currentTime();
    time_t value = (time_t)unixTime;
    struct tm nowTm {}, valueTm {}, tomorrowTm {};
    localtime_r(&now, &nowTm);
    localtime_r(&value, &valueTm);
    // Noon today plus one day is tomorrow even on days with a daylight saving change.
    time_t tomorrow = now + (12 - nowTm.tm_hour) * 3600 - nowTm.tm_min * 60 - nowTm.tm_sec + 24 * 3600;
    localtime_r(&tomorrow, &tomorrowTm);
    bool sameDay = valueTm.tm_yday == nowTm.tm_yday && valueTm.tm_year == nowTm.tm_year;
    bool nextDay = valueTm.tm_yday == tomorrowTm.tm_yday && valueTm.tm_year == tomorrowTm.tm_year;
    if (sameDay) {
        if (outSize > 0) out[0] = '\0';
    } else if (nextDay) {
        snprintf(out, outSize, "tomorrow ");
    } else {
        strftime(out, outSize, "%a ", &valueTm);
    }
}

const char* compassPoint(double azimuthDeg) {
    static const char* const POINTS[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    int index = (int)((azimuthDeg + 22.5) / 45.0) & 7;
    return POINTS[index];
}

const uint8_t* markerIcon(Context* ctx, int slot) {
    slot %= MAX_TRACKED;
    if (ctx->iconBuffers[slot] != nullptr) return ctx->iconBuffers[slot];
    int bytesPerPixel = ctx->grayscale ? 1 : 2;
    int size = ctx->iconSize;
    auto* buffer = static_cast<uint8_t*>(largeAlloc((size_t)size * size * bytesPerPixel));
    if (buffer == nullptr) return nullptr;
    map::PixelFormat format = ctx->grayscale ? map::PixelFormat::Gray8 : map::PixelFormat::Rgb565;
    const map::Theme& theme = ctx->grayscale ? map::grayTheme() : map::colorTheme();
    map::Surface surface(buffer, size, size, size * bytesPerPixel, format);
    surface.fill(ctx->grayscale ? theme.ocean : map::Rgb { 0x20, 0x20, 0x20 });
    surface.marker(size / 2, size / 2, (size - 5) / 2, static_cast<map::MarkerShape>(slot % map::MARKER_SHAPE_COUNT),
                   slot >= map::MARKER_SHAPE_COUNT, theme.satellite[slot % map::MARKER_SHAPE_COUNT], theme.markerOutline);
    ctx->iconBuffers[slot] = buffer;
    return buffer;
}

lv_obj_t* createMarkerImage(Context* ctx, lv_obj_t* parent, int slot) {
    const uint8_t* buffer = markerIcon(ctx, slot);
    lv_obj_t* canvas = lv_canvas_create(parent);
    if (buffer != nullptr) {
        lv_canvas_set_buffer(canvas, const_cast<uint8_t*>(buffer), ctx->iconSize, ctx->iconSize,
                             ctx->grayscale ? LV_COLOR_FORMAT_L8 : LV_COLOR_FORMAT_RGB565);
    }
    lv_obj_set_size(canvas, ctx->iconSize, ctx->iconSize);
    lv_obj_set_clickable(canvas, false);
    return canvas;
}

void satelliteTrackerInit(Context* ctx) {
    DisplayInfo display { false, false, false };
    device_for_each_of_type(&DISPLAY_TYPE, &display, onDisplayFound);
    ctx->grayscale = display.found && display.grayscale;
    ctx->slowDisplay = display.found && display.slow;
    // A list entry takes about 170 bytes. Without external RAM big groups would use too much of the internal heap.
    if (memory_external_total() == 0) {
        ctx->groupLimit = 150;
        ctx->searchLimit = 60;
    }

    settingsLoad(ctx->settings);
    if (!trackedLoad(ctx->model)) {
        // First start: the list is saved right away, the orbit data follows when the network is available.
        for (const auto& satellite : DEFAULT_SATELLITES) {
            ctx->model.add(satellite.catalogNumber, satellite.shortName, -1, true);
        }
        trackedListSave(ctx->model);
    }
    applyObserver(ctx);
    ctx->focusedIndex = (ctx->settings.autoSelectPass || ctx->model.count() == 0) ? -1 : 0;
}

void satelliteTrackerTeardown(Context* ctx) {
    saveDownloads(ctx);
    freeMarkerIcons(ctx);
}

void satelliteTrackerCreateWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->root = parent;
    lv_obj_set_scrollable(parent, false);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_pad_row(parent, 0, 0);

    int iconSize = (int)lvgl_get_text_font_height(FONT_SIZE_SMALL) + 2;
    if (iconSize < 11) iconSize = 11;
    if (iconSize != ctx->iconSize) {
        freeMarkerIcons(ctx);
        ctx->iconSize = iconSize;
    }

    ctx->forceRedraw = true;
    buildView(ctx);
    ctx->updateTimer = lv_timer_create(onUpdateTimer, 1000, ctx);
}

void satelliteTrackerDestroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    releaseKeyboard(ctx);
    if (ctx->updateTimer != nullptr) {
        lv_timer_delete(ctx->updateTimer);
        ctx->updateTimer = nullptr;
    }
    if (ctx->viewTimer != nullptr) {
        lv_timer_delete(ctx->viewTimer);
        ctx->viewTimer = nullptr;
        ctx->view = ctx->pendingView;
    }
    mapViewDestroy(ctx);
    ctx->mapWidgets = MapWidgets {};
    ctx->toolbar = nullptr;
    ctx->content = nullptr;
    ctx->listWidget = nullptr;
    ctx->messageLabel = nullptr;
    ctx->textArea = nullptr;
    ctx->root = nullptr;
}

void showView(Context* ctx, View view) {
    ctx->pendingView = view;
    if (ctx->viewTimer == nullptr) {
        ctx->viewTimer = lv_timer_create(onViewTimer, 0, ctx);
        lv_timer_set_repeat_count(ctx->viewTimer, 1);
    }
}

bool requestTrackedRefresh(Context* ctx, int64_t minAgeSeconds) {
    if (!timeIsValid()) return false;
    int64_t age = orbitDataAge(ctx);
    bool recent = age >= 0 && age < minAgeSeconds;
    return queueRefresh(ctx, !recent, true);
}

void satelliteTrackerHandleDownload(Context* ctx, const DownloadResult& result) {
    if (!result.success) {
        char text[96];
        if (result.statusCode != 0) {
            snprintf(text, sizeof(text), "Download failed (HTTP %d)", (int)result.statusCode);
        } else {
            snprintf(text, sizeof(text), "Download failed: %s. Check Wi-Fi.", result.message);
        }
        setStatus(ctx, text);
    }
    if (result.job.kind == DownloadKind::Tracked) {
        handleTrackedDownload(ctx, result);
    } else {
        handleCatalogDownload(ctx, result);
    }
    if (!ctx->downloader.isBusy()) saveDownloads(ctx);
}
