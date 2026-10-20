#include "SatelliteTracker.h"

#include "LargeMemory.h"

#include <lvgl/fonts.h>
#include <lvgl/widgets/focus_container.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

constexpr int GAP = 4;
constexpr int COMPACT_ROW_PAD = 3;
/** Lines of details and status below the legend in the column next to the map. */
constexpr int SIDE_TEXT_LINES = 4;

struct LegendClick {
    Context* ctx;
    int index;
};

LegendClick legendClicks[MAX_TRACKED];

void updateLegendFocus(Context* ctx) {
    MapWidgets& w = ctx->mapWidgets;
    for (int i = 0; i < MAX_TRACKED; i++) {
        if (w.legendRows[i] != nullptr) lv_obj_invalidate(w.legendRows[i]);
    }
    int focus = effectiveFocus(ctx);
    ctx->shownFocus = ctx->focusedIndex >= 0 ? focus : -2 - focus;
}

/** Draws a rectangle frame just inside the object's area. */
void drawFrame(lv_event_t* event, lv_obj_t* obj, lv_color_t color, int width, bool dashed) {
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = width;
    if (dashed) {
        line.dash_width = width * 4;
        line.dash_gap = width * 3;
    }
    int inset = width / 2;
    lv_value_precise_t left = area.x1 + inset, right = area.x2 - inset, top = area.y1 + inset, bottom = area.y2 - inset;
    const lv_value_precise_t corners[5][2] = { { left, top }, { right, top }, { right, bottom }, { left, bottom }, { left, top } };
    lv_layer_t* layer = lv_event_get_layer(event);
    for (int i = 0; i < 4; i++) {
        line.p1.x = corners[i][0];
        line.p1.y = corners[i][1];
        line.p2.x = corners[i + 1][0];
        line.p2.y = corners[i + 1][1];
        lv_draw_line(layer, &line);
    }
}

lv_color_t accentColor(const Context* ctx, lv_obj_t* obj) {
    return ctx->grayscale ? lv_color_black() : lv_theme_get_color_primary(obj);
}

/** Frames the focused legend row: a solid line when selected, a dashed line when chosen automatically. */
void onLegendDraw(lv_event_t* event) {
    auto* click = static_cast<LegendClick*>(lv_event_get_user_data(event));
    Context* ctx = click->ctx;
    lv_obj_t* row = ctx->mapWidgets.legendRows[click->index];
    if (row == nullptr || click->index != effectiveFocus(ctx)) return;
    bool inverted = ctx->grayscale && lv_obj_has_state(row, LV_STATE_FOCUS_KEY);
    lv_color_t color = inverted ? lv_color_white() : accentColor(ctx, row);
    drawFrame(event, row, color, ctx->mapWidgets.detailed ? 2 : 1, click->index != ctx->focusedIndex);
}

/** Keyboard focus on the map is drawn into the map image itself, the theme's outline would cover the legend below. */
void onMapFocusChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    lv_obj_t* container = ctx->mapWidgets.mapContainer;
    if (container == nullptr) return;
    if (lv_obj_has_state(container, LV_STATE_FOCUS_KEY) == ctx->scene.frame) return;
    mapViewUpdate(ctx, true);
}

void setFocus(Context* ctx, int index) {
    if (index < -1 || index >= ctx->model.count() || index == ctx->focusedIndex) return;
    ctx->focusedIndex = index;
    mapViewUpdate(ctx, true);
}

/** Tapping the selected satellite again clears the selection. */
void toggleFocus(Context* ctx, int index) {
    setFocus(ctx, index == ctx->focusedIndex ? -1 : index);
}

void onLegendClicked(lv_event_t* event) {
    auto* click = static_cast<LegendClick*>(lv_event_get_user_data(event));
    toggleFocus(click->ctx, click->index);
}

/** A satellite with orbit data has no pass result yet, the search runs in the background. */
bool passSearchPending(const Context* ctx) {
    for (int i = 0; i < ctx->model.count(); i++) {
        const TrackedSatellite& entry = ctx->model.at(i);
        if (entry.hasOrbit && !entry.passKnown) return true;
    }
    return false;
}

/** The tracked satellite whose pass is in progress or starts first. */
int pickNextPass(Context* ctx) {
    if (!ctx->model.hasObserver()) return -1;
    int best = -1;
    int64_t bestRise = 0;
    for (int i = 0; i < ctx->model.count(); i++) {
        const TrackedSatellite& entry = ctx->model.at(i);
        if (!entry.passKnown || entry.passResult != orbit::PassSearchResult::Found) continue;
        if (best < 0 || entry.nextPass.riseTime < bestRise) {
            best = i;
            bestRise = entry.nextPass.riseTime;
        }
    }
    return best;
}

void toggleToolbar(Context* ctx, bool fromKeys) {
    ctx->refocusMap = fromKeys;
    ctx->settings.hideToolbar = !ctx->settings.hideToolbar;
    requestTaskWork(ctx, TASK_WORK_SETTINGS);
    showView(ctx, View::Map);
}

/** A tap on a marker toggles its selection, a tap on the empty map (or the trackball button) hides or shows the toolbar. */
void onMapClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr || ctx->mapWidgets.canvas == nullptr) return;
    if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
        toggleToolbar(ctx, true);
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    lv_area_t area;
    lv_obj_get_coords(ctx->mapWidgets.canvas, &area);
    int touchRadius = ctx->renderer.markerRadius() * 3;
    int hit = ctx->renderer.hitTest(ctx->scene, point.x - area.x1, point.y - area.y1, touchRadius);
    if (hit >= 0) toggleFocus(ctx, hit);
    else toggleToolbar(ctx, false);
}

void onMapKey(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    uint32_t key = lv_event_get_key(event);
    int count = ctx->model.count();
    if (count == 0) return;
    if (key == LV_KEY_RIGHT) setFocus(ctx, ctx->focusedIndex + 1 >= count ? -1 : ctx->focusedIndex + 1);
    else if (key == LV_KEY_LEFT) setFocus(ctx, ctx->focusedIndex - 1 < -1 ? count - 1 : ctx->focusedIndex - 1);
}

lv_obj_t* createLabel(lv_obj_t* parent, const lv_font_t* font, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, text);
    return label;
}

void describeLegendValue(Context* ctx, const TrackedSatellite& entry, char* out, size_t outSize) {
    if (!entry.positionValid) {
        snprintf(out, outSize, "--");
    } else if (!ctx->model.hasObserver()) {
        snprintf(out, outSize, "%.0f km", entry.position.altKm);
    } else if (entry.look.elevationDeg > 0.0) {
        snprintf(out, outSize, "up %.0f\xC2\xB0", entry.look.elevationDeg);
    } else if (entry.passKnown && entry.passResult == orbit::PassSearchResult::Found) {
        formatClock(entry.nextPass.riseTime, out, outSize);
    } else {
        snprintf(out, outSize, "--");
    }
}

void describePass(Context* ctx, const TrackedSatellite& entry, char* out, size_t outSize, bool compact) {
    if (!ctx->model.hasObserver()) {
        snprintf(out, outSize, compact ? "Set location in settings" : "Set your location in the settings to see passes.");
        return;
    }
    char rise[8], set[8];
    bool passInProgress = entry.passKnown && entry.passResult == orbit::PassSearchResult::Found && entry.nextPass.riseTime <= currentTime();
    if (entry.look.elevationDeg > 0.0 && passInProgress) {
        formatClock(entry.nextPass.setTime, set, sizeof(set));
        if (compact) {
            snprintf(out, outSize, "Up %.0f° %s, sets %s", entry.look.elevationDeg, compassPoint(entry.look.azimuthDeg), set);
        } else {
            snprintf(out, outSize, "Above the horizon: %.0f°, azimuth %.0f° %s, sets %s", entry.look.elevationDeg,
                     entry.look.azimuthDeg, compassPoint(entry.look.azimuthDeg), set);
        }
        return;
    }
    if (!entry.passKnown) {
        snprintf(out, outSize, compact ? "Finding pass..." : "Finding the next pass...");
        return;
    }
    switch (entry.passResult) {
        case orbit::PassSearchResult::Found: {
            formatClock(entry.nextPass.riseTime, rise, sizeof(rise));
            formatClock(entry.nextPass.setTime, set, sizeof(set));
            char day[16];
            formatDay(entry.nextPass.riseTime, day, sizeof(day));
            if (compact) {
                snprintf(out, outSize, "Pass %s%s max %.0f\xC2\xB0 %s", day, rise, entry.nextPass.maxElevationDeg, compassPoint(entry.nextPass.riseAzimuthDeg));
            } else {
                snprintf(out, outSize, "Next pass %s%s - %s, max %.0f\xC2\xB0, %s to %s", day, rise, set, entry.nextPass.maxElevationDeg,
                         compassPoint(entry.nextPass.riseAzimuthDeg), compassPoint(entry.nextPass.setAzimuthDeg));
            }
            break;
        }
        case orbit::PassSearchResult::AlwaysUp:
            snprintf(out, outSize, compact ? "Always up, %.0f\xC2\xB0" : "Always above your horizon, elevation %.0f\xC2\xB0", entry.look.elevationDeg);
            break;
        case orbit::PassSearchResult::None:
            snprintf(out, outSize, compact ? "No pass in 36 h" : "No pass higher than 5° in the next 36 hours");
            break;
        case orbit::PassSearchResult::Error:
            snprintf(out, outSize, compact ? "Orbit data is too old" : "The orbit data is too old for a prediction");
            break;
    }
}

void describeUpdated(Context* ctx, char* out, size_t outSize) {
    char clock[8];
    formatClock(currentTime(), clock, sizeof(clock));
    int64_t age = orbitDataAge(ctx);
    if (age >= 0) {
        snprintf(out, outSize, "Updated %s, orbit data %d h old", clock, (int)(age / 3600));
    } else {
        snprintf(out, outSize, "Updated %s", clock);
    }
}

void describeFooter(Context* ctx, char* out, size_t outSize) {
    if (ctx->statusText[0] != '\0') {
        snprintf(out, outSize, "%s", ctx->statusText);
    } else {
        describeUpdated(ctx, out, outSize);
    }
}

int textWidth(const lv_font_t* font, const char* text) {
    int width = 0;
    for (const char* p = text; *p; p++) {
        width += (int)lv_font_get_glyph_width(font, (uint32_t)(unsigned char)*p, (uint32_t)(unsigned char)p[1]);
    }
    return width;
}

lv_obj_t* createLineLabel(lv_obj_t* parent, const lv_font_t* font, int x, int y, int width) {
    lv_obj_t* label = createLabel(parent, font, "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, y);
    return label;
}

bool prepareRenderer(Context* ctx, int width, int height) {
    map::PixelFormat format = ctx->grayscale ? map::PixelFormat::Gray8 : map::PixelFormat::Rgb565;
    if (ctx->renderer.width() == width && ctx->renderer.height() == height) return true;
    return ctx->renderer.setup(width, height, format, ctx->grayscale ? map::grayTheme() : map::colorTheme());
}

void createLegendRow(Context* ctx, lv_obj_t* parent, int index, int x, int y, int width, int height, const lv_font_t* font) {
    MapWidgets& w = ctx->mapWidgets;
    const TrackedSatellite& entry = ctx->model.at(index);
    lv_obj_t* row = lvgl_focus_container_create(parent);
    lv_obj_set_pos(row, x, y);
    lv_obj_set_size(row, width, height);
    lv_obj_set_clickable(row, true);
    // Keyboard focus is shown as a tint, so the frame stays the only mark of the selected satellite.
    // E-paper shows it inverted instead, a light tint turns fully black on black and white panels.
    lv_obj_set_style_outline_width(row, 0, LV_STATE_FOCUS_KEY);
    if (ctx->grayscale) {
        lv_obj_set_style_bg_color(row, lv_color_black(), LV_STATE_FOCUS_KEY);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_text_color(row, lv_color_white(), LV_STATE_FOCUS_KEY);
    } else {
        lv_obj_set_style_bg_color(row, lv_theme_get_color_primary(row), LV_STATE_FOCUS_KEY);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_STATE_FOCUS_KEY);
    }
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_pad_left(row, 3, 0);
    lv_obj_set_style_pad_right(row, ctx->mapWidgets.detailed ? 8 : 3, 0);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    legendClicks[index] = LegendClick { ctx, index };
    lv_obj_add_event_cb(row, onLegendClicked, LV_EVENT_CLICKED, &legendClicks[index]);
    lv_obj_add_event_cb(row, onLegendDraw, LV_EVENT_DRAW_POST, &legendClicks[index]);

    createMarkerImage(ctx, row, entry.slot);

    lv_obj_t* name = createLabel(row, font, entry.shortName);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_flex_grow(name, 1);
    w.legendNames[index] = name;

    lv_obj_t* value = createLabel(row, font, "");
    w.legendValues[index] = value;
    w.legendRows[index] = row;
}

}

void mapViewCreate(Context* ctx) {
    lv_obj_t* content = ctx->content;
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scrollable(content, false);
    lv_obj_update_layout(content);
    int width = lv_obj_get_content_width(content);
    int height = lv_obj_get_content_height(content);

    MapWidgets& w = ctx->mapWidgets;
    w.detailed = width >= 560;
    const lv_font_t* smallFont = lvgl_get_text_font(FONT_SIZE_SMALL);
    const lv_font_t* defaultFont = lvgl_get_text_font(FONT_SIZE_DEFAULT);
    int smallHeight = (int)lv_font_get_line_height(smallFont);
    int count = ctx->model.count();

    // A legend entry needs room for the icon, a short name and a time like "ABCDEFG 88:88". Longer names are clipped.
    int rowHeight = smallHeight + (w.detailed ? COMPACT_ROW_PAD * 2 : COMPACT_ROW_PAD);
    int minimumEntryWidth = ctx->iconSize + textWidth(smallFont, "ABCDEFG 88:88") + (w.detailed ? 8 : 3) + 3 + 8;
    int columns = (width + minimumEntryWidth / 2) / minimumEntryWidth;
    if (columns < 1) columns = 1;
    if (columns > MAX_TRACKED / 2) columns = MAX_TRACKED / 2;
    int rows = count > columns ? (count + columns - 1) / columns : 1;
    int bottomHeight = rows * rowHeight + smallHeight * 2 + 3;
    int bottomMapWidth = width;
    if (bottomMapWidth > (height - bottomHeight) * 2) bottomMapWidth = (height - bottomHeight) * 2;

    // Legend, details and status in a column next to the map, for short and wide screens.
    int sideColumnWidth = minimumEntryWidth;
    if (width - height * 2 - GAP > sideColumnWidth) sideColumnWidth = width - height * 2 - GAP;
    int sideMapWidth = 0;
    if (width > height) {
        sideMapWidth = width - sideColumnWidth - GAP;
        if (sideMapWidth > height * 2) sideMapWidth = height * 2;
    }

    w.wide = sideMapWidth > bottomMapWidth;
    int mapWidth = w.wide ? sideMapWidth : bottomMapWidth;
    mapWidth &= ~1;
    if (mapWidth < 40) mapWidth = 40;
    int mapHeight = mapWidth / 2;
    int mapX = w.wide ? 0 : (width - mapWidth) / 2;
    if (w.wide) {
        // From the full window height, so hiding or showing the toolbar does not change the limit.
        int fullHeight = lv_obj_get_content_height(lv_obj_get_parent(content));
        ctx->model.setLimit((fullHeight - smallHeight * SIDE_TEXT_LINES - GAP) / (rowHeight + 2));
    } else {
        ctx->model.setLimit(columns * 2);
    }

    if (!prepareRenderer(ctx, mapWidth, mapHeight)) {
        lv_obj_t* label = createLabel(content, defaultFont, "Not enough memory for the map.");
        lv_obj_center(label);
        return;
    }

    int bytesPerPixel = ctx->grayscale ? 1 : 2;
    w.canvasBuffer = static_cast<uint8_t*>(largeAlloc((size_t)mapWidth * mapHeight * bytesPerPixel));
    if (w.canvasBuffer == nullptr) {
        lv_obj_t* label = createLabel(content, defaultFont, "Not enough memory for the map.");
        lv_obj_center(label);
        return;
    }
    w.mapContainer = lvgl_focus_container_create(content);
    lv_obj_set_pos(w.mapContainer, mapX, 0);
    lv_obj_set_size(w.mapContainer, mapWidth, mapHeight);
    lv_obj_add_event_cb(w.mapContainer, onMapClicked, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(w.mapContainer, onMapKey, LV_EVENT_KEY, ctx);
    lv_obj_add_event_cb(w.mapContainer, onMapFocusChanged, LV_EVENT_FOCUSED, ctx);
    lv_obj_add_event_cb(w.mapContainer, onMapFocusChanged, LV_EVENT_DEFOCUSED, ctx);
    lv_obj_set_style_outline_width(w.mapContainer, 0, LV_STATE_FOCUS_KEY);
    w.canvas = lv_canvas_create(w.mapContainer);
    lv_canvas_set_buffer(w.canvas, w.canvasBuffer, mapWidth, mapHeight, ctx->grayscale ? LV_COLOR_FORMAT_L8 : LV_COLOR_FORMAT_RGB565);
    lv_obj_set_clickable(w.canvas, false);
    w.mapX = mapX;

    if (ctx->focusedIndex >= count) ctx->focusedIndex = -1;
    ctx->shownFocus = -100;

    if (w.wide) {
        // The details and the status keep a fixed place at the bottom, the legend above scrolls when it does not fit.
        int columnX = mapWidth + GAP;
        int columnWidth = width - mapWidth - GAP * 2;
        int textHeight = smallHeight * SIDE_TEXT_LINES;
        lv_obj_t* legend = lv_obj_create(content);
        lv_obj_remove_style_all(legend);
        lv_obj_set_pos(legend, columnX, 0);
        lv_obj_set_size(legend, width - columnX, height - textHeight - GAP);
        lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(legend, 2, 0);
        lv_obj_set_scroll_dir(legend, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(legend, LV_SCROLLBAR_MODE_AUTO);
        for (int i = 0; i < count; i++) {
            createLegendRow(ctx, legend, i, 0, 0, columnWidth, rowHeight, smallFont);
            lv_obj_set_scroll_on_focus(w.legendRows[i], true);
        }
        w.info = createLabel(content, smallFont, count == 0 ? "No satellites yet. Use the list button to add some." : "");
        lv_label_set_long_mode(w.info, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(w.info, columnWidth, smallHeight * (SIDE_TEXT_LINES - 1));
        lv_obj_set_pos(w.info, columnX, height - textHeight);
        w.status = createLabel(content, smallFont, "");
        lv_label_set_long_mode(w.status, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(w.status, columnWidth, smallHeight);
        lv_obj_set_pos(w.status, columnX, height - smallHeight);
    } else {
        int columnWidth = (width - GAP) / columns;
        for (int i = 0; i < count; i++) {
            createLegendRow(ctx, content, i, (i % columns) * columnWidth, mapHeight + 1 + (i / columns) * rowHeight, columnWidth, rowHeight, smallFont);
        }
        int infoY = mapHeight + 2 + rows * rowHeight;
        w.info = createLineLabel(content, smallFont, 2, infoY, width - 2);
        if (count == 0) setLabelText(w.info, "No satellites yet. Use the list button to add some.");
        if (w.detailed) {
            int statusWidth = textWidth(smallFont, "Updated 88:88, orbit data 88 h old");
            w.passLabel = createLineLabel(content, smallFont, 2, infoY + smallHeight, width - statusWidth - GAP * 2);
            w.status = createLineLabel(content, smallFont, width - statusWidth - 2, infoY + smallHeight, statusWidth);
            lv_obj_set_style_text_align(w.status, LV_TEXT_ALIGN_RIGHT, 0);
        } else {
            w.status = createLineLabel(content, smallFont, 2, infoY + smallHeight, width - 2);
        }
    }

    if (ctx->refocusMap) {
        ctx->refocusMap = false;
        if (lv_obj_get_group(w.mapContainer) != nullptr) {
            lv_obj_add_state(w.mapContainer, LV_STATE_FOCUS_KEY);
            lv_group_focus_obj(w.mapContainer);
        }
    }

    mapViewUpdate(ctx, true);
}

void mapViewDestroy(Context* ctx) {
    MapWidgets& w = ctx->mapWidgets;
    if (w.mapContainer != nullptr) {
        // Cleared first: deleting the focused map moves the focus, and the focus handler must not redraw a map that is going away.
        lv_obj_t* container = w.mapContainer;
        w.mapContainer = nullptr;
        w.canvas = nullptr;
        lv_obj_delete(container);
    }
    largeFree(w.canvasBuffer);
    w.canvasBuffer = nullptr;
}

void mapViewUpdate(Context* ctx, bool force) {
    MapWidgets& w = ctx->mapWidgets;
    if (w.canvas == nullptr) return;
    int64_t now = currentTime();
    if (!force && !ctx->forceRedraw && now - ctx->lastMapDraw < effectiveUpdateSeconds(ctx)) return;

    map::Surface surface(w.canvasBuffer, ctx->renderer.width(), ctx->renderer.height(),
                         ctx->renderer.width() * (ctx->grayscale ? 1 : 2), ctx->grayscale ? map::PixelFormat::Gray8 : map::PixelFormat::Rgb565);

    if (!timeIsValid()) {
        ctx->scene = map::Scene {};
        ctx->renderer.render(surface, ctx->scene);
        lv_obj_invalidate(w.canvas);
        setLabelText(w.info, "The clock is not set yet.");
        setLabelText(w.status, "Connect to Wi-Fi so the time can sync.");
        ctx->lastMapDraw = now;
        ctx->forceRedraw = false;
        return;
    }

    ctx->model.updatePositions((double)now);
    if (ctx->focusedIndex >= ctx->model.count()) ctx->focusedIndex = -1;
    ctx->autoIndex = pickNextPass(ctx);
    int focus = effectiveFocus(ctx);
    int shownFocus = ctx->focusedIndex >= 0 ? focus : -2 - focus;
    if (shownFocus != ctx->shownFocus) updateLegendFocus(ctx);
    ctx->model.buildScene((double)now, focus, ctx->settings.tracksForAll, ctx->scene);
    ctx->scene.frame = w.mapContainer != nullptr && lv_obj_has_state(w.mapContainer, LV_STATE_FOCUS_KEY);
    if (ctx->scene.frame) {
        lv_color_t color = accentColor(ctx, w.mapContainer);
        ctx->scene.frameColor = map::Rgb { color.red, color.green, color.blue };
    }
    ctx->renderer.render(surface, ctx->scene);
    lv_obj_invalidate(w.canvas);

    char text[320];
    for (int i = 0; i < ctx->model.count(); i++) {
        describeLegendValue(ctx, ctx->model.at(i), text, sizeof(text));
        setLabelText(w.legendValues[i], text);
    }

    if (focus >= 0 && !ctx->model.at(focus).positionValid) {
        const TrackedSatellite& focused = ctx->model.at(focus);
        if (focused.hasOrbit) {
            snprintf(text, sizeof(text), w.detailed ? "%s: the orbit data can no longer be used (re-entered or too old)." : "%s: orbit data unusable",
                     focused.shortName);
        } else {
            snprintf(text, sizeof(text), w.detailed ? "%s: waiting for orbit data, it is downloaded when Wi-Fi is available." : "%s: waiting for orbit data",
                     focused.shortName);
        }
        setLabelText(w.info, text);
        setLabelText(w.passLabel, "");
    } else if (focus >= 0) {
        const TrackedSatellite& focused = ctx->model.at(focus);
        bool automatic = focus != ctx->focusedIndex;
        char pass[128];
        describePass(ctx, focused, pass, sizeof(pass), !w.detailed);
        const char* ns = focused.position.latDeg >= 0.0 ? "N" : "S";
        const char* ew = focused.position.lonDeg >= 0.0 ? "E" : "W";
        double lat = focused.position.latDeg < 0 ? -focused.position.latDeg : focused.position.latDeg;
        double lon = focused.position.lonDeg < 0 ? -focused.position.lonDeg : focused.position.lonDeg;
        unsigned catalog = (unsigned)focused.catalogNumber;
        if (w.wide && w.detailed) {
            snprintf(text, sizeof(text), "%s (%u)%s\n%.1f\xC2\xB0%s %.1f\xC2\xB0%s, %.0f km, %s\n%s", focused.tle.name, catalog,
                     automatic ? ", auto" : "", lat, ns, lon, ew, focused.position.altKm, focused.sunlit ? "sunlit" : "in shadow", pass);
        } else if (w.wide) {
            snprintf(text, sizeof(text), "%s%s\n%.0f km, %s\n%s", automatic ? "auto " : "", focused.shortName, focused.position.altKm,
                     focused.sunlit ? "sunlit" : "in shadow", pass);
        } else if (w.detailed) {
            snprintf(text, sizeof(text), "%s (%u)%s:  %.1f\xC2\xB0%s %.1f\xC2\xB0%s, %.0f km, %.2f km/s, %s", focused.tle.name, catalog,
                     automatic ? ", selected automatically" : "", lat, ns, lon, ew, focused.position.altKm, focused.speedKmS,
                     focused.sunlit ? "sunlit" : "in shadow");
            if (w.passLabel != nullptr && ctx->statusText[0] == '\0') setLabelText(w.passLabel, pass);
        } else {
            snprintf(text, sizeof(text), "%s%s %.0f km %s | %s", automatic ? "auto " : "", focused.shortName, focused.position.altKm,
                     focused.sunlit ? "lit" : "dark", pass);
        }
        setLabelText(w.info, text);
    } else if (ctx->model.count() > 0) {
        const char* hint;
        if (!ctx->settings.autoSelectPass) {
            hint = w.detailed ? "No satellite selected. Tap a satellite to see its track and details." : "Tap a satellite to select it";
        } else if (!ctx->model.hasObserver()) {
            hint = w.detailed ? "No satellite selected. Set your location in the settings to select the next pass automatically."
                          : "Set location for automatic selection";
        } else if (passSearchPending(ctx)) {
            hint = "Finding the next pass...";
        } else {
            hint = w.detailed ? "No satellite selected and no pass higher than 5° in the next 36 hours." : "No pass in 36 h";
        }
        setLabelText(w.info, hint);
        setLabelText(w.passLabel, "");
    }

    if (w.passLabel != nullptr) {
        // The status message replaces the pass text so it has the whole line.
        if (ctx->statusText[0] != '\0') {
            setLabelText(w.passLabel, ctx->statusText);
            setLabelText(w.status, "");
        } else {
            describeUpdated(ctx, text, sizeof(text));
            setLabelText(w.status, text);
        }
    } else {
        describeFooter(ctx, text, sizeof(text));
        setLabelText(w.status, text);
    }
    ctx->lastMapDraw = now;
    ctx->forceRedraw = false;
}
