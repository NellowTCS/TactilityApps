#pragma once

#include "Downloads.h"
#include "LocationApi.h"
#include "MapRenderer.h"
#include "Storage.h"
#include "TrackerModel.h"

#include <app/instance.h>
#include <lvgl.h>
#include <lvgl_window_manager/window_manager.h>

#include <cstdint>
#include <vector>

enum class View {
    Map,
    Passes,
    Satellites,
    Groups,
    GroupList,
    Search,
    Rename,
    Settings,
};

/** Work that the main task does for the LVGL thread, which must not access files or block for long. */
enum TaskWork : uint32_t {
    TASK_WORK_SETTINGS = 1 << 0,
    TASK_WORK_LIST = 1 << 1,
    TASK_WORK_ORBITS = 1 << 2,
    /** Show the saved copy of the selected group, or download it when it is old or missing. */
    TASK_WORK_GROUP = 1 << 3,
    /** Search the passes that are missing or over. */
    TASK_WORK_PASSES = 1 << 4,
    /** Build the list of the Next passes screen. */
    TASK_WORK_PASS_LIST = 1 << 5,
};

struct PassListRow {
    uint32_t catalogNumber;
    orbit::Pass pass;
    /** Sunlit satellite in a dark sky. */
    bool visible;
};

/** The passes of the next 24 hours, built on the main task. */
struct PassList {
    /** False until the list for the current screen is built. */
    bool ready = false;
    std::vector<PassListRow, LargeAllocator<PassListRow>> rows;
    std::vector<uint32_t, LargeAllocator<uint32_t>> alwaysUp;
};

struct Context;

struct RowAction {
    Context* ctx;
    int index;
    lv_obj_t* label;
};

struct MapWidgets {
    /** Focus container around the canvas, so keypads and encoders can reach the map. */
    lv_obj_t* mapContainer = nullptr;
    lv_obj_t* canvas = nullptr;
    uint8_t* canvasBuffer = nullptr;
    int mapX = 0;
    int mapY = 0;
    lv_obj_t* legendRows[MAX_TRACKED] {};
    lv_obj_t* legendNames[MAX_TRACKED] {};
    lv_obj_t* legendValues[MAX_TRACKED] {};
    lv_obj_t* info = nullptr;
    /** Pass text on the last line next to the status, only in the wide layout with the legend below the map. */
    lv_obj_t* passLabel = nullptr;
    lv_obj_t* status = nullptr;
    /** Legend in a column to the right of the map, else in a grid below it. */
    bool wide = false;
    /** Enough width for the long texts. */
    bool detailed = false;
};

struct Context {
    AppInstanceId appInstanceId = 0;
    WindowId window = 0;
    Downloader downloader;

    TaskEventGroup* eventGroup = nullptr;
    uint32_t taskWorkBit = 0;
    /** TaskWork flags, set with the LVGL lock held. */
    uint32_t taskWork = 0;
    /** Main task only. */
    SavedList savedList;
    PassJobs passJobs;
    PassJobs passListJobs;
    /** Set on the main task with the LVGL lock held. */
    PassList passList;

    TrackerModel model;
    Settings settings;
    /** New orbit data or a new download time that is saved once the queued downloads are done. */
    bool downloadsUnsaved = false;
    int64_t lastRefreshAttempt = 0;
    bool startupRefreshDone = false;
    /** Satellite selected by the user, -1 when none. */
    int focusedIndex = -1;
    /** Satellite with the next pass, used when nothing is selected and the setting is on. */
    int autoIndex = -1;
    int shownFocus = -2;
    View view = View::Map;
    bool grayscale = false;
    bool slowDisplay = false;

    char statusText[96] {};
    int64_t lastMapDraw = 0;
    bool forceRedraw = true;

    char selectedGroup[24] {};
    char selectedGroupName[40] {};
    /** As long as a download tag, which identifies the search when its result arrives. */
    char searchText[sizeof(DownloadJob::tag)] {};
    TleList catalog;
    /** Entries read from a group or search result, lower on devices without external RAM. */
    size_t groupLimit = 400;
    size_t searchLimit = 100;
    std::vector<RowAction> rowActions;
    bool catalogLoading = false;
    /** The main task reads the saved copy of the selected group. */
    bool catalogReading = false;
    int renameIndex = -1;

    map::MapRenderer renderer;
    map::Scene scene;
    uint8_t* iconBuffers[MAX_TRACKED] {};
    int iconSize = 0;

    /** The app's window, null while the window has no widgets. */
    lv_obj_t* root = nullptr;
    lv_obj_t* toolbar = nullptr;
    lv_obj_t* content = nullptr;
    lv_timer_t* updateTimer = nullptr;
    lv_timer_t* viewTimer = nullptr;
    View pendingView = View::Map;
    /** Give the map keyboard focus again after the toolbar was hidden or shown with a key. */
    bool refocusMap = false;
    MapWidgets mapWidgets;
    lv_obj_t* listWidget = nullptr;
    lv_obj_t* messageLabel = nullptr;
    lv_obj_t* textArea = nullptr;
    bool keyboardBound = false;

    /** Latest fix of Tactility's location service, set on the main task with the LVGL lock held. */
    bool deviceFixValid = false;
    orbit::Geodetic deviceFix {};
    float deviceAccuracyM = 0.0f;
    char deviceProvider[LOCATION_PROVIDER_ID_MAX_LENGTH + 1] {};
};

constexpr int64_t MIN_VALID_TIME = 1735689600;
constexpr int64_t TRACKED_REFRESH_SECONDS = 24 * 3600;
constexpr int64_t TRACKED_MIN_REFRESH_SECONDS = 2 * 3600;
constexpr int64_t GROUP_CACHE_SECONDS = 24 * 3600;
constexpr int64_t PASS_SEARCH_SECONDS = 36 * 3600;

/** Main task, before the window exists: reads the settings and the tracked satellites. */
void satelliteTrackerLoad(Context* ctx);
void satelliteTrackerInit(Context* ctx);
void satelliteTrackerCreateWidgets(lv_obj_t* parent, void* userData);
void satelliteTrackerDestroyWidgets(void* userData);
/** Requests saving what is still unsaved and frees the marker icons. */
void satelliteTrackerTeardown(Context* ctx);

/** Asks the main task to do TaskWork. Called with the LVGL lock held. */
void requestTaskWork(Context* ctx, uint32_t work);
/** Main task, without the LVGL lock: does the requested TaskWork. */
void satelliteTrackerDoTaskWork(Context* ctx);

/** Main task, without the LVGL lock: reads the downloaded file that the result needs. */
void satelliteTrackerReadDownload(Context* ctx, const DownloadResult& result, TleList& out);
/** Called on the main task with the LVGL lock held. tles holds what satelliteTrackerReadDownload() read. */
void satelliteTrackerHandleDownload(Context* ctx, const DownloadResult& result, TleList& tles);

/**
 * Queues downloads of the orbit data of all tracked satellites when it is older than minAgeSeconds, else only of those
 * that have none yet. Returns true when anything was queued.
 */
bool requestTrackedRefresh(Context* ctx, int64_t minAgeSeconds);
/** Adds a satellite from a group or search result, with its orbit data. Returns the index or -1. */
int addTracked(Context* ctx, const orbit::Tle& tle);

/** Switches the screen after the current event has finished. */
void showView(Context* ctx, View view);
/** Uses the device location when enabled and available, else the location from the settings. */
void applyObserver(Context* ctx);
/** Called on the main task with the LVGL lock held. nullptr means the device location is no longer available. */
void satelliteTrackerSetDeviceLocation(Context* ctx, const Location* fix);
/** The satellite shown with track and details: the selected one, else the automatic choice, else -1. */
int effectiveFocus(const Context* ctx);
void removeTracked(Context* ctx, int index);
int64_t currentTime();
/** Seconds since the tracked TLEs were downloaded, or -1 when unknown (never, or the clock went back since). */
int64_t orbitDataAge(const Context* ctx);
bool timeIsValid();
int effectiveUpdateSeconds(const Context* ctx);
const uint8_t* markerIcon(Context* ctx, int slot);
lv_obj_t* createMarkerImage(Context* ctx, lv_obj_t* parent, int slot);
void setLabelText(lv_obj_t* label, const char* text);
/** Creates a text area that detaches the on-screen keyboard before its screen is deleted. */
lv_obj_t* createTextArea(Context* ctx, lv_obj_t* parent);
void formatClock(int64_t unixTime, char* out, size_t outSize);
/** Day of a time in the next days: "" for today, "tomorrow " for the next day, else the weekday ("Fri "). */
void formatDay(int64_t unixTime, char* out, size_t outSize);
const char* compassPoint(double azimuthDeg);

void mapViewCreate(Context* ctx);
void mapViewDestroy(Context* ctx);
void mapViewUpdate(Context* ctx, bool force);

void passesViewCreate(Context* ctx);
/** Main task, without the LVGL lock: builds the pass list and shows it when the Next passes screen is still open. */
void satelliteTrackerBuildPassList(Context* ctx);
void satellitesViewCreate(Context* ctx);
void groupsViewCreate(Context* ctx);
void groupListViewCreate(Context* ctx);
void searchViewCreate(Context* ctx);
void renameViewCreate(Context* ctx);
void settingsViewCreate(Context* ctx);
void settingsViewUpdate(Context* ctx);

/** Rebuilds the visible catalog list after a group or search download. */
void catalogViewRefresh(Context* ctx);
