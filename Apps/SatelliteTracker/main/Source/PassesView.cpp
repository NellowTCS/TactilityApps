#include "SatelliteTracker.h"

#include <lvgl/fonts.h>
#include <lvgl/lvgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>

namespace {

constexpr int64_t PASS_LIST_SECONDS = 24 * 3600;
constexpr size_t MAX_ROWS = 40;
constexpr int MAX_PASSES_PER_SATELLITE = 10;

/** The satellite is in sunlight while the observer has a dark sky (sun at least 6 degrees below the horizon). */
bool isVisiblePass(orbit::Satellite& propagator, const orbit::Geodetic& observer, const orbit::Pass& pass) {
    double t = (double)pass.maxTime;
    orbit::Vec3 r, v;
    if (!propagator.positionEci(t, r, v) || !orbit::isSunlit(r, t)) return false;
    orbit::Geodetic sun = orbit::subsolarPoint(t);
    const double deg = 3.14159265358979323846 / 180.0;
    double sunHeight = std::sin(observer.latDeg * deg) * std::sin(sun.latDeg * deg) +
        std::cos(observer.latDeg * deg) * std::cos(sun.latDeg * deg) * std::cos((observer.lonDeg - sun.lonDeg) * deg);
    return sunHeight < std::sin(-6.0 * deg);
}

void addMessage(Context* ctx, const char* text) {
    lv_obj_t* label = lv_label_create(ctx->content);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_text(label, text);
}

}

void satelliteTrackerBuildPassList(Context* ctx) {
    if (!timeIsValid()) return;
    PassJobs& jobs = ctx->passListJobs;
    lvgl_lock();
    bool hasObserver = ctx->model.hasObserver();
    jobs.count = 0;
    jobs.observer = ctx->model.observer();
    jobs.observerVersion = ctx->model.observerChanges();
    jobs.startTime = currentTime();
    for (int i = 0; i < ctx->model.count(); i++) {
        const TrackedSatellite& entry = ctx->model.at(i);
        if (!entry.hasOrbit) continue;
        PassJob& job = jobs.jobs[jobs.count++];
        job.catalogNumber = entry.catalogNumber;
        job.tle = entry.tle;
    }
    lvgl_unlock();

    PassList list;
    if (hasObserver) {
        auto propagator = std::make_unique<orbit::Satellite>();
        int64_t now = jobs.startTime;
        for (int i = 0; i < jobs.count; i++) {
            const PassJob& job = jobs.jobs[i];
            if (!propagator->init(job.tle)) continue;
            int64_t t = now;
            for (int n = 0; n < MAX_PASSES_PER_SATELLITE && t < now + PASS_LIST_SECONDS; n++) {
                orbit::Pass pass;
                auto result = propagator->findNextPass(jobs.observer, t, now + PASS_LIST_SECONDS - t, MIN_PASS_ELEVATION_DEG, pass);
                if (result == orbit::PassSearchResult::AlwaysUp) {
                    list.alwaysUp.push_back(job.catalogNumber);
                    break;
                }
                if (result != orbit::PassSearchResult::Found) break;
                list.rows.push_back(PassListRow { job.catalogNumber, pass, isVisiblePass(*propagator, jobs.observer, pass) });
                t = pass.setTime + 60;
            }
        }
        std::sort(list.rows.begin(), list.rows.end(), [](const PassListRow& a, const PassListRow& b) { return a.pass.riseTime < b.pass.riseTime; });
        if (list.rows.size() > MAX_ROWS) list.rows.resize(MAX_ROWS);
    }

    lvgl_lock();
    if (jobs.observerVersion != ctx->model.observerChanges()) {
        requestTaskWork(ctx, TASK_WORK_PASS_LIST);
    } else {
        ctx->passList.rows.swap(list.rows);
        ctx->passList.alwaysUp.swap(list.alwaysUp);
        ctx->passList.ready = true;
        if (ctx->pendingView == View::Passes) showView(ctx, View::Passes);
    }
    lvgl_unlock();
}

void passesViewCreate(Context* ctx) {
    lv_obj_set_flex_flow(ctx->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->content, 2, 0);

    if (!timeIsValid()) {
        addMessage(ctx, "The clock is not set yet. Connect to Wi-Fi so the time can sync.");
        return;
    }
    if (!ctx->model.hasObserver()) {
        addMessage(ctx, "Set your location in the settings to see passes.");
        return;
    }
    if (ctx->model.count() == 0) {
        addMessage(ctx, "No satellites are tracked yet.");
        return;
    }
    if (!ctx->passList.ready) {
        addMessage(ctx, "Finding passes...");
        return;
    }

    int withoutOrbit = 0;
    for (int i = 0; i < ctx->model.count(); i++) {
        if (!ctx->model.at(i).hasOrbit) withoutOrbit++;
    }

    bool compact = lv_obj_get_content_width(ctx->content) < 400;
    const lv_font_t* font = lvgl_get_text_font(compact ? FONT_SIZE_SMALL : FONT_SIZE_DEFAULT);
    time_t nowValue = (time_t)currentTime();
    struct tm nowTm {};
    localtime_r(&nowValue, &nowTm);
    bool anyVisible = false;
    bool anyShown = false;

    for (uint32_t catalogNumber : ctx->passList.alwaysUp) {
        // The satellite may have been removed since the list was built.
        int index = ctx->model.indexOfCatalog(catalogNumber);
        if (index < 0) continue;
        const TrackedSatellite& entry = ctx->model.at(index);
        anyShown = true;
        lv_obj_t* row = lv_obj_create(ctx->content);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, 0);
        createMarkerImage(ctx, row, entry.slot);
        lv_obj_t* label = lv_label_create(row);
        lv_obj_set_style_text_font(label, font, 0);
        char text[64];
        snprintf(text, sizeof(text), "%s  always above the horizon", entry.shortName);
        lv_label_set_text(label, text);
    }

    for (const auto& item : ctx->passList.rows) {
        int index = ctx->model.indexOfCatalog(item.catalogNumber);
        if (index < 0) continue;
        const TrackedSatellite& entry = ctx->model.at(index);
        anyShown = true;
        char rise[8], set[8], day[8] = "";
        formatClock(item.pass.riseTime, rise, sizeof(rise));
        formatClock(item.pass.setTime, set, sizeof(set));
        time_t riseValue = (time_t)item.pass.riseTime;
        struct tm riseTm {};
        localtime_r(&riseValue, &riseTm);
        if (riseTm.tm_yday != nowTm.tm_yday) {
            strftime(day, sizeof(day), "%a ", &riseTm);
        }
        anyVisible = anyVisible || item.visible;

        lv_obj_t* row = lv_obj_create(ctx->content);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, 0);
        createMarkerImage(ctx, row, entry.slot);
        lv_obj_t* label = lv_label_create(row);
        lv_obj_set_style_text_font(label, font, 0);
        char text[128];
        if (compact) {
            snprintf(text, sizeof(text), "%s%s-%s %2.0f\xC2\xB0 %s%s", day, rise, set, item.pass.maxElevationDeg, entry.shortName, item.visible ? " *" : "");
        } else {
            snprintf(text, sizeof(text), "%s%s - %s   max %2.0f\xC2\xB0   %s to %s   %s%s", day, rise, set, item.pass.maxElevationDeg,
                     compassPoint(item.pass.riseAzimuthDeg), compassPoint(item.pass.setAzimuthDeg), entry.shortName,
                     item.visible ? "  * visible" : "");
        }
        lv_label_set_text(label, text);
    }

    if (!anyShown) {
        addMessage(ctx, "No passes higher than 5° in the next 24 hours.");
    } else if (anyVisible) {
        lv_obj_t* note = lv_label_create(ctx->content);
        lv_obj_set_style_text_font(note, lvgl_get_text_font(FONT_SIZE_SMALL), 0);
        lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(note, LV_PCT(100));
        lv_label_set_text(note, "* sunlit satellite in a dark sky, visible to the eye when bright enough");
    }
    if (withoutOrbit > 0) {
        char text[96];
        snprintf(text, sizeof(text), withoutOrbit == 1 ? "%d satellite has no orbit data yet." : "%d satellites have no orbit data yet.", withoutOrbit);
        addMessage(ctx, text);
    }
}
