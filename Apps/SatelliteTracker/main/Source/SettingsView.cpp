#include "SatelliteTracker.h"

#include "LocationApi.h"

#include <lvgl/fonts.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct SettingsWidgets {
    lv_obj_t* deviceStatus;
    lv_obj_t* latitude;
    lv_obj_t* longitude;
    lv_obj_t* altitude;
    lv_obj_t* interval;
    lv_obj_t* tracksSwitch;
    lv_obj_t* autoSwitch;
    lv_obj_t* orbitLabel;
};

SettingsWidgets widgets {};

lv_obj_t* addLabel(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t* addField(Context* ctx, lv_obj_t* parent, const char* title, const char* acceptedChars, const char* value, const char* placeholder) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_width(label, LV_PCT(45));
    lv_obj_t* textArea = createTextArea(ctx, row);
    lv_textarea_set_one_line(textArea, true);
    lv_textarea_set_accepted_chars(textArea, acceptedChars);
    lv_textarea_set_max_length(textArea, 12);
    lv_textarea_set_text(textArea, value);
    lv_textarea_set_placeholder_text(textArea, placeholder);
    lv_obj_set_flex_grow(textArea, 1);
    return textArea;
}

bool parseNumber(lv_obj_t* textArea, double minimum, double maximum, double& out) {
    const char* text = lv_textarea_get_text(textArea);
    if (text == nullptr || text[0] == '\0') return false;
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%s", text);
    for (char* p = buffer; *p; p++) {
        if (*p == ',') *p = '.';
    }
    char* end = nullptr;
    double value = strtod(buffer, &end);
    if (end == buffer || *end != '\0' || value < minimum || value > maximum) return false;
    out = value;
    return true;
}

void describeOrbitData(Context* ctx, char* out, size_t outSize) {
    int64_t age = orbitDataAge(ctx);
    if (age < 0) {
        snprintf(out, outSize, ctx->settings.lastTrackedRefresh > 0 ? "The age of the orbit data is unknown." : "Orbit data has not been downloaded yet.");
        return;
    }
    int64_t minutes = age / 60;
    if (minutes < 120) {
        snprintf(out, outSize, "Orbit data is %d minutes old.", (int)minutes);
    } else {
        snprintf(out, outSize, "Orbit data is %d hours old.", (int)(minutes / 60));
    }
}

void onSaveClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    Settings& settings = ctx->settings;
    double latitude, longitude, altitude = 0.0, interval;
    bool hasLatitude = parseNumber(widgets.latitude, -90.0, 90.0, latitude);
    bool hasLongitude = parseNumber(widgets.longitude, -180.0, 180.0, longitude);
    const char* latitudeText = lv_textarea_get_text(widgets.latitude);
    const char* longitudeText = lv_textarea_get_text(widgets.longitude);
    bool bothEmpty = latitudeText[0] == '\0' && longitudeText[0] == '\0';

    if (!bothEmpty && (!hasLatitude || !hasLongitude)) {
        setLabelText(ctx->messageLabel, "Latitude must be -90 to 90 and longitude -180 to 180, in decimal degrees (for example 40.7128 and -74.0060). Use a minus sign for south and west.");
        return;
    }
    if (lv_textarea_get_text(widgets.altitude)[0] != '\0' && !parseNumber(widgets.altitude, -500.0, 9000.0, altitude)) {
        setLabelText(ctx->messageLabel, "Altitude must be -500 to 9000 m.");
        return;
    }
    if (lv_textarea_get_text(widgets.interval)[0] == '\0') {
        settings.updateSeconds = 0;
    } else if (parseNumber(widgets.interval, 1.0, 3600.0, interval)) {
        settings.updateSeconds = (int32_t)interval;
    } else {
        setLabelText(ctx->messageLabel, "The update interval must be 1 to 3600 seconds.");
        return;
    }

    settings.observerSet = !bothEmpty;
    if (settings.observerSet) {
        settings.latitudeDeg = latitude;
        settings.longitudeDeg = longitude;
        settings.altitudeM = altitude;
    }
    settingsSave(settings);
    applyObserver(ctx);
    ctx->forceRedraw = true;
    setLabelText(ctx->messageLabel, "Saved.");
}

void onTracksChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->settings.tracksForAll = lv_obj_has_state(widgets.tracksSwitch, LV_STATE_CHECKED);
    settingsSave(ctx->settings);
    ctx->forceRedraw = true;
}

void describeDeviceLocation(Context* ctx, char* out, size_t outSize) {
    if (!ctx->settings.useDeviceLocation) {
        snprintf(out, outSize, "The location below is used.");
    } else if (!location_is_enabled()) {
        snprintf(out, outSize, "Location services are off in Tactility's settings, the location below is used.");
    } else if (ctx->deviceFixValid) {
        const orbit::Geodetic& fix = ctx->deviceFix;
        snprintf(out, outSize, "Using the device location: %.3f\xC2\xB0%s %.3f\xC2\xB0%s (\xC2\xB1%.0f m, %s).", fix.latDeg < 0 ? -fix.latDeg : fix.latDeg,
                 fix.latDeg >= 0 ? "N" : "S", fix.lonDeg < 0 ? -fix.lonDeg : fix.lonDeg, fix.lonDeg >= 0 ? "E" : "W", ctx->deviceAccuracyM, ctx->deviceProvider);
    } else {
        snprintf(out, outSize, "Waiting for the device location, the location below is used until then.");
    }
}

void onDeviceLocationChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->settings.useDeviceLocation = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
    settingsSave(ctx->settings);
    applyObserver(ctx);
    ctx->forceRedraw = true;
    settingsViewUpdate(ctx);
}

void onAutoSelectChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->settings.autoSelectPass = lv_obj_has_state(widgets.autoSwitch, LV_STATE_CHECKED);
    settingsSave(ctx->settings);
    ctx->forceRedraw = true;
}

lv_obj_t* addSwitch(lv_obj_t* parent, const char* title, bool checked, lv_event_cb_t callback, void* userData) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(label, title);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_t* toggle = lv_switch_create(row);
    if (checked) lv_obj_add_state(toggle, LV_STATE_CHECKED);
    lv_obj_add_event_cb(toggle, callback, LV_EVENT_VALUE_CHANGED, userData);
    return toggle;
}

void onRefreshClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (!timeIsValid()) {
        setLabelText(ctx->messageLabel, "The clock is not set yet. Connect to Wi-Fi so the time can sync.");
    } else if (requestTrackedRefresh(ctx, TRACKED_MIN_REFRESH_SECONDS)) {
        setLabelText(ctx->messageLabel, "Updating orbit data...");
    } else if (ctx->downloader.isBusy()) {
        setLabelText(ctx->messageLabel, "A download is already running.");
    } else {
        setLabelText(ctx->messageLabel, "Orbit data is less than 2 hours old. CelesTrak asks not to download it more often.");
    }
}

}

void settingsViewCreate(Context* ctx) {
    lv_obj_set_flex_flow(ctx->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->content, 6, 0);
    widgets = SettingsWidgets {};
    const Settings& settings = ctx->settings;

    addSwitch(ctx->content, "Use the device location (GPS or network) when available", settings.useDeviceLocation, onDeviceLocationChanged, ctx);
    widgets.deviceStatus = addLabel(ctx->content, "");
    settingsViewUpdate(ctx);
    addLabel(ctx->content, "Location used when the device location is off or not available. Leave empty for none.");
    lv_obj_t* help = addLabel(ctx->content,
        "Use decimal degrees. North and east are positive, south and west are negative. "
        "Example for New York City: latitude 40.7128, longitude -74.0060, altitude 10. "
        "To convert degrees and minutes, add minutes / 60: 40\xC2\xB0 43' is 40.72.");
    lv_obj_set_style_text_font(help, lvgl_get_text_font(FONT_SIZE_SMALL), 0);
    char latitude[16] = "", longitude[16] = "", altitude[16] = "", interval[16] = "";
    if (settings.observerSet) {
        snprintf(latitude, sizeof(latitude), "%.4f", settings.latitudeDeg);
        snprintf(longitude, sizeof(longitude), "%.4f", settings.longitudeDeg);
        snprintf(altitude, sizeof(altitude), "%.0f", settings.altitudeM);
    }
    if (settings.updateSeconds > 0) {
        snprintf(interval, sizeof(interval), "%d", (int)settings.updateSeconds);
    }
    widgets.latitude = addField(ctx, ctx->content, "Latitude", "-0123456789.,", latitude, "40.7128");
    widgets.longitude = addField(ctx, ctx->content, "Longitude", "-0123456789.,", longitude, "-74.0060");
    widgets.altitude = addField(ctx, ctx->content, "Altitude (m)", "-0123456789", altitude, "10");

    char intervalTitle[48];
    snprintf(intervalTitle, sizeof(intervalTitle), "Map update (s), default %d", ctx->slowDisplay ? 60 : 5);
    widgets.interval = addField(ctx, ctx->content, intervalTitle, "0123456789", interval, ctx->slowDisplay ? "60" : "5");

    widgets.tracksSwitch = addSwitch(ctx->content, "Tracks for all satellites", settings.tracksForAll, onTracksChanged, ctx);
    widgets.autoSwitch = addSwitch(ctx->content, "Select the next pass automatically when no satellite is selected", settings.autoSelectPass,
                                   onAutoSelectChanged, ctx);

    lv_obj_t* saveButton = lv_button_create(ctx->content);
    lv_obj_t* saveLabel = lv_label_create(saveButton);
    lv_label_set_text(saveLabel, LV_SYMBOL_SAVE " Save");
    lv_obj_add_event_cb(saveButton, onSaveClicked, LV_EVENT_SHORT_CLICKED, ctx);

    char orbit[64];
    describeOrbitData(ctx, orbit, sizeof(orbit));
    widgets.orbitLabel = addLabel(ctx->content, orbit);
    lv_obj_t* refreshButton = lv_button_create(ctx->content);
    lv_obj_t* refreshLabel = lv_label_create(refreshButton);
    lv_label_set_text(refreshLabel, LV_SYMBOL_REFRESH " Update orbit data");
    lv_obj_add_event_cb(refreshButton, onRefreshClicked, LV_EVENT_SHORT_CLICKED, ctx);

    ctx->messageLabel = addLabel(ctx->content, "");
    lv_obj_t* credit = addLabel(ctx->content, "Orbit data: CelesTrak. Map: Natural Earth.");
    lv_obj_set_style_text_font(credit, lvgl_get_text_font(FONT_SIZE_SMALL), 0);
}

void settingsViewUpdate(Context* ctx) {
    if (widgets.deviceStatus == nullptr) return;
    char text[160];
    describeDeviceLocation(ctx, text, sizeof(text));
    setLabelText(widgets.deviceStatus, text);
}
