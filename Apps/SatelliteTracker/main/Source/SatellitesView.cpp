#include "SatelliteTracker.h"

#include <lvgl/fonts.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr size_t MAX_CATALOG_ROWS = 250;

struct Group {
    const char* id;
    const char* name;
};

constexpr Group GROUPS[] = {
    { "stations", "Space stations" },
    { "visual", "100 brightest" },
    { "last-30-days", "Launched in the last 30 days" },
    { "weather", "Weather" },
    { "noaa", "NOAA" },
    { "goes", "GOES" },
    { "resource", "Earth resources" },
    { "sarsat", "Search and rescue" },
    { "amateur", "Amateur radio" },
    { "gps-ops", "GPS" },
    { "galileo", "Galileo" },
    { "glo-ops", "GLONASS" },
    { "beidou", "BeiDou" },
    { "science", "Space and earth science" },
    { "geodetic", "Geodetic" },
    { "engineering", "Engineering" },
    { "education", "Education" },
    { "cubesat", "CubeSats" },
};

/** "Tracking 3 of 6", or an explanation when a list saved on a bigger screen holds more than this screen fits. */
void describeTracking(const Context* ctx, char* out, size_t outSize) {
    int count = ctx->model.count();
    int limit = ctx->model.limit();
    if (count > limit) {
        snprintf(out, outSize, "Tracking %d satellites, this screen fits %d. Remove some to add others.", count, limit);
    } else {
        snprintf(out, outSize, "Tracking %d of %d", count, limit);
    }
}

void addMessage(Context* ctx, lv_obj_t* parent, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_text(label, text);
    ctx->messageLabel = label;
}

lv_obj_t* addButton(lv_obj_t* parent, const char* text, lv_event_cb_t callback, void* userData) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, userData);
    return button;
}

void onRenameClicked(lv_event_t* event) {
    auto* action = static_cast<RowAction*>(lv_event_get_user_data(event));
    action->ctx->renameIndex = action->index;
    showView(action->ctx, View::Rename);
}

void onRemoveClicked(lv_event_t* event) {
    auto* action = static_cast<RowAction*>(lv_event_get_user_data(event));
    Context* ctx = action->ctx;
    removeTracked(ctx, action->index);
    showView(ctx, View::Satellites);
}

void onAddFromGroupClicked(lv_event_t* event) {
    showView(static_cast<Context*>(lv_event_get_user_data(event)), View::Groups);
}

void onSearchClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->catalog.clear();
    ctx->catalogLoading = false;
    showView(ctx, View::Search);
}

void onGroupClicked(lv_event_t* event) {
    auto* action = static_cast<RowAction*>(lv_event_get_user_data(event));
    Context* ctx = action->ctx;
    const Group& group = GROUPS[action->index];
    snprintf(ctx->selectedGroup, sizeof(ctx->selectedGroup), "%s", group.id);
    snprintf(ctx->selectedGroupName, sizeof(ctx->selectedGroupName), "%s", group.name);
    ctx->catalog.clear();
    showView(ctx, View::GroupList);
}

void catalogRowText(Context* ctx, const orbit::Tle& tle, char* out, size_t outSize) {
    bool tracked = ctx->model.indexOfCatalog(orbit::tleCatalogNumber(tle)) >= 0;
    snprintf(out, outSize, "%s %s", tracked ? LV_SYMBOL_OK : LV_SYMBOL_PLUS, tle.name);
}

void onCatalogRowClicked(lv_event_t* event) {
    auto* action = static_cast<RowAction*>(lv_event_get_user_data(event));
    Context* ctx = action->ctx;
    if (action->index < 0 || action->index >= (int)ctx->catalog.size()) return;
    const orbit::Tle& tle = ctx->catalog[action->index];
    int tracked = ctx->model.indexOfCatalog(orbit::tleCatalogNumber(tle));
    if (tracked >= 0) {
        removeTracked(ctx, tracked);
    } else if (ctx->model.count() >= ctx->model.limit()) {
        char full[64];
        snprintf(full, sizeof(full), "You can track up to %d satellites. Remove one first.", ctx->model.limit());
        setLabelText(ctx->messageLabel, full);
        return;
    } else if (addTracked(ctx, tle) < 0) {
        setLabelText(ctx->messageLabel, "This satellite has no usable orbit data.");
        return;
    }
    char text[64];
    catalogRowText(ctx, tle, text, sizeof(text));
    lv_label_set_text(action->label, text);
    char message[96];
    describeTracking(ctx, message, sizeof(message));
    setLabelText(ctx->messageLabel, message);
}

void startSearch(Context* ctx) {
    if (ctx->textArea == nullptr) return;
    const char* text = lv_textarea_get_text(ctx->textArea);
    snprintf(ctx->searchText, sizeof(ctx->searchText), "%s", text);
    DownloadJob job;
    if (!Downloader::buildSearchJob(ctx->searchText, job)) {
        setLabelText(ctx->messageLabel, "Enter a name or a NORAD catalog number.");
        return;
    }
    if (!timeIsValid()) {
        setLabelText(ctx->messageLabel, "The clock is not set yet. Connect to Wi-Fi so the time can sync.");
        return;
    }
    if (!ctx->downloader.enqueue(job)) {
        setLabelText(ctx->messageLabel, "Downloads are not available. Restart the app.");
        return;
    }
    ctx->catalog.clear();
    ctx->catalogLoading = true;
    catalogViewRefresh(ctx);
}

void onSearchStart(lv_event_t* event) {
    startSearch(static_cast<Context*>(lv_event_get_user_data(event)));
}

void onRenameSave(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (ctx->renameIndex >= 0 && ctx->renameIndex < ctx->model.count() && ctx->textArea != nullptr) {
        TrackedSatellite& entry = ctx->model.at(ctx->renameIndex);
        const char* text = lv_textarea_get_text(ctx->textArea);
        while (*text == ' ') text++;
        if (*text != '\0') {
            snprintf(entry.shortName, sizeof(entry.shortName), "%s", text);
        } else if (entry.hasOrbit) {
            TrackerModel::makeShortName(entry.tle.name, entry.shortName, sizeof(entry.shortName));
        }
        trackedListSave(ctx->model);
        ctx->forceRedraw = true;
    }
    showView(ctx, View::Satellites);
}

/** The automatic name comes from the orbit data, so the button is only offered once that was downloaded. */
void onRenameAuto(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (ctx->renameIndex >= 0 && ctx->renameIndex < ctx->model.count() && ctx->textArea != nullptr) {
        const TrackedSatellite& entry = ctx->model.at(ctx->renameIndex);
        char name[SHORT_NAME_LENGTH];
        TrackerModel::makeShortName(entry.tle.name, name, sizeof(name));
        lv_textarea_set_text(ctx->textArea, name);
    }
}

lv_obj_t* createColumn(lv_obj_t* parent) {
    lv_obj_t* column = lv_obj_create(parent);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, 4, 0);
    return column;
}

}

void satellitesViewCreate(Context* ctx) {
    lv_obj_set_flex_flow(ctx->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->content, 6, 0);
    ctx->rowActions.clear();
    ctx->rowActions.reserve(MAX_TRACKED);

    lv_obj_t* header = lv_label_create(ctx->content);
    char tracking[96];
    describeTracking(ctx, tracking, sizeof(tracking));
    lv_label_set_long_mode(header, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(header, LV_PCT(100));
    lv_label_set_text(header, tracking);

    for (int i = 0; i < ctx->model.count(); i++) {
        const TrackedSatellite& entry = ctx->model.at(i);
        lv_obj_t* row = lv_obj_create(ctx->content);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, 0);

        createMarkerImage(ctx, row, entry.slot);
        lv_obj_t* label = lv_label_create(row);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_flex_grow(label, 1);
        char text[96];
        if (entry.hasOrbit) {
            snprintf(text, sizeof(text), "%s  (%s)", entry.shortName, entry.tle.name);
        } else {
            snprintf(text, sizeof(text), "%s  (%u, waiting for orbit data)", entry.shortName, (unsigned)entry.catalogNumber);
        }
        lv_label_set_text(label, text);

        ctx->rowActions.push_back(RowAction { ctx, i, label });
        addButton(row, LV_SYMBOL_EDIT, onRenameClicked, &ctx->rowActions.back());
        addButton(row, LV_SYMBOL_TRASH, onRemoveClicked, &ctx->rowActions.back());
    }

    lv_obj_t* buttons = lv_obj_create(ctx->content);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(buttons, 8, 0);
    lv_obj_set_style_pad_row(buttons, 4, 0);
    addButton(buttons, LV_SYMBOL_LIST " Add from group", onAddFromGroupClicked, ctx);
    addButton(buttons, LV_SYMBOL_DIRECTORY " Search", onSearchClicked, ctx);

    if (ctx->statusText[0] != '\0') {
        addMessage(ctx, ctx->content, ctx->statusText);
    }
}

void groupsViewCreate(Context* ctx) {
    lv_obj_set_flex_flow(ctx->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->content, 4, 0);
    ctx->rowActions.clear();
    ctx->rowActions.reserve(sizeof(GROUPS) / sizeof(GROUPS[0]));
    for (size_t i = 0; i < sizeof(GROUPS) / sizeof(GROUPS[0]); i++) {
        lv_obj_t* button = lv_button_create(ctx->content);
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_t* label = lv_label_create(button);
        lv_label_set_text_fmt(label, LV_SYMBOL_DIRECTORY "  %s", GROUPS[i].name);
        ctx->rowActions.push_back(RowAction { ctx, (int)i, label });
        lv_obj_add_event_cb(button, onGroupClicked, LV_EVENT_SHORT_CLICKED, &ctx->rowActions.back());
    }
}

void groupListViewCreate(Context* ctx) {
    lv_obj_set_flex_flow(ctx->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->content, 4, 0);
    addMessage(ctx, ctx->content, "");
    ctx->listWidget = createColumn(ctx->content);

    char name[48], path[256];
    snprintf(name, sizeof(name), "group-%s.tle", ctx->selectedGroup);
    int64_t age = storageUserPath(name, path, sizeof(path)) ? fileAgeSeconds(path, currentTime()) : -1;
    bool fresh = age >= 0 && (age < GROUP_CACHE_SECONDS || !timeIsValid());
    ctx->catalogLoading = false;
    if (fresh) {
        tleFileRead(path, ctx->catalog, ctx->groupLimit);
        sortByName(ctx->catalog);
    }
    if (!fresh || ctx->catalog.empty()) {
        DownloadJob job;
        bool queued = timeIsValid() && Downloader::buildGroupJob(ctx->selectedGroup, job) &&
            (ctx->downloader.hasQueued(DownloadKind::Group, job.tag) || ctx->downloader.enqueue(job));
        if (queued) {
            ctx->catalogLoading = true;
        } else if (age >= 0) {
            tleFileRead(path, ctx->catalog, ctx->groupLimit);
            sortByName(ctx->catalog);
        }
    }
    catalogViewRefresh(ctx);
}

void searchViewCreate(Context* ctx) {
    lv_obj_set_flex_flow(ctx->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->content, 4, 0);

    lv_obj_t* row = lv_obj_create(ctx->content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);

    ctx->textArea = createTextArea(ctx, row);
    lv_textarea_set_one_line(ctx->textArea, true);
    lv_textarea_set_max_length(ctx->textArea, sizeof(ctx->searchText) - 1);
    lv_textarea_set_placeholder_text(ctx->textArea, "Name or NORAD number");
    lv_textarea_set_text(ctx->textArea, ctx->searchText);
    lv_obj_set_flex_grow(ctx->textArea, 1);
    lv_obj_add_event_cb(ctx->textArea, onSearchStart, LV_EVENT_READY, ctx);
    addButton(row, LV_SYMBOL_DIRECTORY, onSearchStart, ctx);

    addMessage(ctx, ctx->content, "");
    ctx->listWidget = createColumn(ctx->content);
    catalogViewRefresh(ctx);
}

void catalogViewRefresh(Context* ctx) {
    if ((ctx->view != View::GroupList && ctx->view != View::Search) || ctx->listWidget == nullptr) return;
    lv_obj_clean(ctx->listWidget);
    ctx->rowActions.clear();
    ctx->rowActions.reserve(ctx->catalog.size());

    if (ctx->catalogLoading) {
        setLabelText(ctx->messageLabel, "Downloading from CelesTrak...");
        return;
    }
    if (ctx->catalog.empty()) {
        if (ctx->view == View::Search && ctx->searchText[0] == '\0') {
            setLabelText(ctx->messageLabel, "Type a satellite name (for example ISS or NOAA 19) or a NORAD catalog number.");
        } else {
            setLabelText(ctx->messageLabel, ctx->statusText[0] != '\0' ? ctx->statusText : "Nothing found.");
        }
        return;
    }

    char tracking[96], message[160];
    describeTracking(ctx, tracking, sizeof(tracking));
    size_t shown = ctx->catalog.size() > MAX_CATALOG_ROWS ? MAX_CATALOG_ROWS : ctx->catalog.size();
    if (shown < ctx->catalog.size()) {
        snprintf(message, sizeof(message), "%s. Showing the first %d, search to find others.", tracking, (int)shown);
    } else if (ctx->model.count() > ctx->model.limit()) {
        snprintf(message, sizeof(message), "%s", tracking);
    } else {
        snprintf(message, sizeof(message), "%s. Tap to add or remove.", tracking);
    }
    setLabelText(ctx->messageLabel, message);
    for (size_t i = 0; i < shown; i++) {
        lv_obj_t* button = lv_button_create(ctx->listWidget);
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_t* label = lv_label_create(button);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(label, LV_PCT(100));
        char text[64];
        catalogRowText(ctx, ctx->catalog[i], text, sizeof(text));
        lv_label_set_text(label, text);
        ctx->rowActions.push_back(RowAction { ctx, (int)i, label });
        lv_obj_add_event_cb(button, onCatalogRowClicked, LV_EVENT_SHORT_CLICKED, &ctx->rowActions.back());
    }
}

void renameViewCreate(Context* ctx) {
    lv_obj_set_flex_flow(ctx->content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->content, 6, 0);
    if (ctx->renameIndex < 0 || ctx->renameIndex >= ctx->model.count()) {
        addMessage(ctx, ctx->content, "This satellite is no longer tracked.");
        return;
    }
    const TrackedSatellite& entry = ctx->model.at(ctx->renameIndex);
    lv_obj_t* label = lv_label_create(ctx->content);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    char text[96];
    if (entry.hasOrbit) {
        snprintf(text, sizeof(text), "Short name for %s, shown in the legend:", entry.tle.name);
    } else {
        snprintf(text, sizeof(text), "Short name for satellite %u, shown in the legend:", (unsigned)entry.catalogNumber);
    }
    lv_label_set_text(label, text);

    ctx->textArea = createTextArea(ctx, ctx->content);
    lv_textarea_set_one_line(ctx->textArea, true);
    lv_textarea_set_max_length(ctx->textArea, SHORT_NAME_LENGTH - 1);
    lv_textarea_set_text(ctx->textArea, entry.shortName);
    lv_obj_set_width(ctx->textArea, LV_PCT(100));
    lv_obj_add_event_cb(ctx->textArea, onRenameSave, LV_EVENT_READY, ctx);

    lv_obj_t* buttons = lv_obj_create(ctx->content);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, 8, 0);
    addButton(buttons, LV_SYMBOL_OK " Save", onRenameSave, ctx);
    if (entry.hasOrbit) addButton(buttons, LV_SYMBOL_REFRESH " Automatic", onRenameAuto, ctx);
}
