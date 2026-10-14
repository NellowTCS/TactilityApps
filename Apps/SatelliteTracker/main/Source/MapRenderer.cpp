#include "MapRenderer.h"

#include "LargeMemory.h"
#include "Orbit.h"
#include "WorldMap.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace map {

namespace {

constexpr float PI_F = 3.14159265f;
constexpr float DEG_F = PI_F / 180.0f;
constexpr float TWILIGHT_LIMIT = -0.1045f;

const Theme COLOR_THEME = {
    .ocean = { 0x12, 0x2B, 0x45 },
    .land = { 0x3F, 0x5E, 0x34 },
    .coast = { 0x7D, 0x9A, 0x6A },
    .grid = { 0x22, 0x40, 0x60 },
    .observer = { 0xFF, 0xFF, 0xFF },
    .markerOutline = { 0x00, 0x00, 0x00 },
    .focus = { 0xFF, 0xFF, 0xFF },
    .satellite = {
        { 0xF5, 0xC5, 0x42 },
        { 0xFF, 0x6B, 0x6B },
        { 0x7F, 0xD1, 0xFF },
        { 0xC0, 0x9C, 0xFF },
        { 0x6E, 0xE7, 0xA8 },
        { 0xFF, 0x9F, 0x43 },
    },
    .nightDarken = 120,
    .twilightDarken = 60,
};

const Theme GRAY_THEME = {
    .ocean = { 0xFF, 0xFF, 0xFF },
    .land = { 0xD0, 0xD0, 0xD0 },
    .coast = { 0x60, 0x60, 0x60 },
    .grid = { 0xE4, 0xE4, 0xE4 },
    .observer = { 0x00, 0x00, 0x00 },
    .markerOutline = { 0xFF, 0xFF, 0xFF },
    .focus = { 0x00, 0x00, 0x00 },
    .satellite = {
        { 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00 },
        { 0x00, 0x00, 0x00 },
    },
    .nightDarken = 70,
    .twilightDarken = 35,
};

uint8_t toGray(Rgb c) {
    return (uint8_t)((c.r * 77 + c.g * 150 + c.b * 29) >> 8);
}

uint16_t toRgb565(Rgb c) {
    return (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
}

bool insideShape(MarkerShape shape, float dx, float dy, float r, float grow) {
    switch (shape) {
        case MarkerShape::Circle:
            return dx * dx + dy * dy <= (r + grow) * (r + grow);
        case MarkerShape::Square: {
            float half = r * 0.8f + grow;
            return std::fabs(dx) <= half && std::fabs(dy) <= half;
        }
        case MarkerShape::Triangle: {
            if (dy - 0.75f * r > grow) return false;
            const float nx = 1.75f, ny = -0.95f;
            const float len = std::sqrt(nx * nx + ny * ny);
            float right = (dx * nx + (dy + r) * ny) / len;
            float left = (-dx * nx + (dy + r) * ny) / len;
            return right <= grow && left <= grow;
        }
        case MarkerShape::Diamond:
            return std::fabs(dx) + std::fabs(dy) <= r + grow * 1.414f;
        case MarkerShape::Plus: {
            float arm = r * 0.42f + grow;
            float reach = r + grow;
            return (std::fabs(dx) <= arm && std::fabs(dy) <= reach) || (std::fabs(dy) <= arm && std::fabs(dx) <= reach);
        }
        case MarkerShape::TriangleDown:
            return insideShape(MarkerShape::Triangle, dx, -dy, r, grow);
    }
    return false;
}

float hollowWall(MarkerShape shape, float r) {
    float wall = shape == MarkerShape::Plus ? r * 0.19f : r * 0.3f;
    return wall < 1.5f ? 1.5f : wall;
}

}

const Theme& colorTheme() { return COLOR_THEME; }
const Theme& grayTheme() { return GRAY_THEME; }

Surface::Surface(void* data, int width, int height, int strideBytes, PixelFormat format)
    : bytes(static_cast<uint8_t*>(data)), w(width), h(height), stride(strideBytes), pixelFormat(format) {}

void Surface::fill(Rgb color) {
    for (int y = 0; y < h; y++) {
        if (pixelFormat == PixelFormat::Gray8) {
            memset(rowPointer(y), toGray(color), w);
        } else {
            auto* row = reinterpret_cast<uint16_t*>(rowPointer(y));
            uint16_t value = toRgb565(color);
            for (int x = 0; x < w; x++) row[x] = value;
        }
    }
}

void Surface::setPixel(int x, int y, Rgb color) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    if (pixelFormat == PixelFormat::Gray8) {
        rowPointer(y)[x] = toGray(color);
    } else {
        reinterpret_cast<uint16_t*>(rowPointer(y))[x] = toRgb565(color);
    }
}

void Surface::darkenPixel(int x, int y, uint8_t amount) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint32_t keep = 255 - amount;
    if (pixelFormat == PixelFormat::Gray8) {
        uint8_t& v = rowPointer(y)[x];
        v = (uint8_t)((v * keep) / 255);
    } else {
        uint16_t& v = reinterpret_cast<uint16_t*>(rowPointer(y))[x];
        uint32_t r = ((v >> 11) & 0x1F) * keep / 255;
        uint32_t g = ((v >> 5) & 0x3F) * keep / 255;
        uint32_t b = (v & 0x1F) * keep / 255;
        v = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

void Surface::line(int x0, int y0, int x1, int y1, Rgb color, int thickness, int dashOn, int dashOff, int* dashPhase) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int offset = (thickness - 1) / 2;
    int step = dashPhase != nullptr ? *dashPhase : 0;
    int period = dashOn + dashOff;
    while (true) {
        if (period == 0 || dashOff == 0 || (step % period) < dashOn) {
            for (int ty = 0; ty < thickness; ty++) {
                for (int tx = 0; tx < thickness; tx++) {
                    setPixel(x0 + tx - offset, y0 + ty - offset, color);
                }
            }
        }
        if (x0 == x1 && y0 == y1) break;
        step++;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    if (dashPhase != nullptr && period > 0) {
        *dashPhase = step % period;
    }
}

void Surface::marker(int cx, int cy, int radius, MarkerShape shape, bool hollow, Rgb fillColor, Rgb outlineColor) {
    float r = (float)radius;
    float wall = hollow ? hollowWall(shape, r) : 0.0f;
    int extent = radius + 2;
    for (int dy = -extent; dy <= extent; dy++) {
        for (int dx = -extent; dx <= extent; dx++) {
            float x = (float)dx, y = (float)dy;
            bool inside = insideShape(shape, x, y, r, 0.0f);
            if (inside && hollow && insideShape(shape, x, y, r, -wall)) {
                if (!insideShape(shape, x, y, r, -wall - 1.2f)) setPixel(cx + dx, cy + dy, outlineColor);
            } else if (inside) {
                setPixel(cx + dx, cy + dy, fillColor);
            } else if (insideShape(shape, x, y, r, 1.2f)) {
                setPixel(cx + dx, cy + dy, outlineColor);
            }
        }
    }
}

void Surface::circle(int cx, int cy, int radius, int thickness, Rgb color) {
    float outer = (float)radius + 0.5f;
    float inner = outer - (float)thickness;
    for (int dy = -radius - 1; dy <= radius + 1; dy++) {
        for (int dx = -radius - 1; dx <= radius + 1; dx++) {
            float d = std::sqrt((float)(dx * dx + dy * dy));
            if (d <= outer && d > inner) setPixel(cx + dx, cy + dy, color);
        }
    }
}

void Surface::copyFrom(const Surface& other) {
    int rowBytes = w * (pixelFormat == PixelFormat::Gray8 ? 1 : 2);
    for (int y = 0; y < h && y < other.h; y++) {
        memcpy(rowPointer(y), other.rowPointer(y), rowBytes);
    }
}

MapRenderer::~MapRenderer() {
    largeFree(baseData);
}

bool MapRenderer::setup(int width, int height, PixelFormat format, const Theme& theme) {
    largeFree(baseData);
    baseData = nullptr;
    mapWidth = width;
    mapHeight = height;
    pixelFormat = format;
    bytesPerPixel = (format == PixelFormat::Gray8) ? 1 : 2;
    activeTheme = &theme;
    baseData = static_cast<uint8_t*>(largeAlloc((size_t)width * height * bytesPerPixel));
    if (baseData == nullptr) return false;
    Surface base(baseData, width, height, width * bytesPerPixel, format);
    drawBase(base);
    return true;
}

int MapRenderer::markerRadius() const {
    int r = mapWidth / 70;
    return r < 5 ? 5 : r;
}

void MapRenderer::project(GeoPoint point, float& x, float& y) const {
    x = (point.lon + 180.0f) / 360.0f * (float)mapWidth;
    y = (90.0f - point.lat) / 180.0f * (float)mapHeight;
}

void MapRenderer::drawBase(Surface& surface) const {
    const Theme& theme = *activeTheme;
    surface.fill(theme.ocean);

    for (int lon = -150; lon < 180; lon += 30) {
        int x = (int)((lon + 180) / 360.0f * mapWidth);
        surface.line(x, 0, x, mapHeight - 1, theme.grid, 1, 0, 0);
    }
    for (int lat = -60; lat <= 60; lat += 30) {
        int y = (int)((90 - lat) / 180.0f * mapHeight);
        surface.line(0, y, mapWidth - 1, y, theme.grid, 1, 0, 0);
    }

    std::vector<float> crossings;
    crossings.reserve(64);
    for (int y = 0; y < mapHeight; y++) {
        float lat = (90.0f - ((float)y + 0.5f) * 180.0f / (float)mapHeight) * 100.0f;
        crossings.clear();
        const int16_t* points = WORLD_MAP_POINTS;
        for (int ring = 0; ring < WORLD_MAP_RING_COUNT; ring++) {
            int count = WORLD_MAP_RING_LENGTHS[ring];
            for (int i = 0; i < count; i++) {
                const int16_t* a = points + i * 2;
                const int16_t* b = points + ((i + 1) % count) * 2;
                float latA = a[1], latB = b[1];
                if ((latA > lat) != (latB > lat)) {
                    float lon = a[0] + (lat - latA) * (float)(b[0] - a[0]) / (latB - latA);
                    crossings.push_back((lon / 100.0f + 180.0f) / 360.0f * (float)mapWidth);
                }
            }
            points += count * 2;
        }
        std::sort(crossings.begin(), crossings.end());
        for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
            int start = (int)std::ceil(crossings[i] - 0.5f);
            int end = (int)std::floor(crossings[i + 1] - 0.5f);
            for (int x = start; x <= end; x++) {
                surface.setPixel(x, y, theme.land);
            }
        }
    }

    const int16_t* points = WORLD_MAP_POINTS;
    for (int ring = 0; ring < WORLD_MAP_RING_COUNT; ring++) {
        int count = WORLD_MAP_RING_LENGTHS[ring];
        for (int i = 0; i < count; i++) {
            const int16_t* a = points + i * 2;
            const int16_t* b = points + ((i + 1) % count) * 2;
            bool onDateLine = std::abs(a[0]) >= 17999 && std::abs(b[0]) >= 17999 && ((a[0] > 0) == (b[0] > 0));
            bool onSouthEdge = a[1] <= -8999 && b[1] <= -8999;
            if (onDateLine || onSouthEdge) continue;
            float ax, ay, bx, by;
            project(GeoPoint { a[1] / 100.0f, a[0] / 100.0f }, ax, ay);
            project(GeoPoint { b[1] / 100.0f, b[0] / 100.0f }, bx, by);
            surface.line((int)ax, (int)ay, (int)bx, (int)by, theme.coast, 1, 0, 0);
        }
        points += count * 2;
    }
}

void MapRenderer::drawNight(Surface& surface, const Scene& scene) const {
    const Theme& theme = *activeTheme;
    float sinDec = std::sin(scene.subsolar.lat * DEG_F);
    float cosDec = std::cos(scene.subsolar.lat * DEG_F);
    std::vector<float> columnCos(mapWidth);
    for (int x = 0; x < mapWidth; x++) {
        float lon = ((float)x + 0.5f) / (float)mapWidth * 360.0f - 180.0f;
        columnCos[x] = std::cos((lon - scene.subsolar.lon) * DEG_F) * cosDec;
    }
    for (int y = 0; y < mapHeight; y++) {
        float lat = (90.0f - ((float)y + 0.5f) * 180.0f / (float)mapHeight) * DEG_F;
        float a = std::sin(lat) * sinDec;
        float b = std::cos(lat);
        for (int x = 0; x < mapWidth; x++) {
            float sunHeight = a + b * columnCos[x];
            if (sunHeight < TWILIGHT_LIMIT) {
                surface.darkenPixel(x, y, theme.nightDarken);
            } else if (sunHeight < 0.0f) {
                surface.darkenPixel(x, y, theme.twilightDarken);
            }
        }
    }
}

void MapRenderer::drawPolyline(Surface& surface, const std::vector<GeoPoint>& points, Rgb color, int thickness, int dashOn, int dashOff) const {
    int dashPhase = 0;
    for (size_t i = 1; i < points.size(); i++) {
        float x0, y0, x1, y1;
        project(points[i - 1], x0, y0);
        project(points[i], x1, y1);
        if (std::fabs(x1 - x0) > mapWidth / 2.0f) continue;
        surface.line((int)x0, (int)y0, (int)x1, (int)y1, color, thickness, dashOn, dashOff, &dashPhase);
    }
}

void MapRenderer::drawFootprint(Surface& surface, GeoPoint center, float radiusDeg, Rgb color) const {
    std::vector<GeoPoint> ring;
    ring.reserve(73);
    for (int bearing = 0; bearing <= 360; bearing += 5) {
        double lat, lon;
        orbit::destinationPoint(center.lat, center.lon, radiusDeg, bearing, lat, lon);
        ring.push_back(GeoPoint { (float)lat, (float)lon });
    }
    drawPolyline(surface, ring, color, 1, 3, 3);
}

void MapRenderer::render(Surface& target, const Scene& scene) const {
    const Theme& theme = *activeTheme;
    Surface base(baseData, mapWidth, mapHeight, mapWidth * bytesPerPixel, pixelFormat);
    target.copyFrom(base);

    if (scene.showNight) {
        drawNight(target, scene);
    }

    int trackThickness = mapWidth >= 600 ? 2 : 1;
    for (const auto& satellite : scene.satellites) {
        if (!satellite.visible || !satellite.drawTrack) continue;
        Rgb color = theme.satellite[satellite.colorIndex % MARKER_SHAPE_COUNT];
        drawPolyline(target, satellite.pastTrack, color, trackThickness, 0, 0);
        int dash = mapWidth >= 600 ? 10 : 5;
        drawPolyline(target, satellite.futureTrack, color, trackThickness, dash, dash * 2 / 3 + 1);
        if (satellite.footprintRadiusDeg > 0.0f) {
            drawFootprint(target, satellite.position, satellite.footprintRadiusDeg, color);
        }
    }

    int radius = markerRadius();
    if (scene.hasObserver) {
        float ox, oy;
        project(scene.observer, ox, oy);
        int arm = radius - 1;
        target.line((int)ox - arm, (int)oy - arm, (int)ox + arm, (int)oy + arm, theme.observer, 2, 0, 0);
        target.line((int)ox - arm, (int)oy + arm, (int)ox + arm, (int)oy - arm, theme.observer, 2, 0, 0);
    }

    std::vector<PixelPoint> positions;
    markerPositions(scene, positions);
    for (size_t i = 0; i < scene.satellites.size(); i++) {
        const auto& satellite = scene.satellites[i];
        if (!satellite.visible) continue;
        int sx = (int)positions[i].x;
        int sy = (int)positions[i].y;
        Rgb color = theme.satellite[satellite.colorIndex % MARKER_SHAPE_COUNT];
        if (satellite.focused) {
            target.circle(sx, sy, radius + 5, 2, theme.focus);
        }
        target.marker(sx, sy, radius, satellite.shape, satellite.hollow, color, theme.markerOutline);
    }

    if (scene.frame) {
        int right = mapWidth - 2, bottom = mapHeight - 2;
        target.line(0, 0, right, 0, scene.frameColor, 2, 0, 0);
        target.line(0, bottom, right, bottom, scene.frameColor, 2, 0, 0);
        target.line(0, 0, 0, bottom, scene.frameColor, 2, 0, 0);
        target.line(right, 0, right, bottom, scene.frameColor, 2, 0, 0);
    }
}

void MapRenderer::markerPositions(const Scene& scene, std::vector<PixelPoint>& out) const {
    size_t count = scene.satellites.size();
    out.resize(count);
    for (size_t i = 0; i < count; i++) {
        float x, y;
        project(scene.satellites[i].position, x, y);
        out[i] = PixelPoint { x, y };
    }

    float minimumDistance = markerRadius() * 2.2f;
    std::vector<int> group(count);
    for (size_t i = 0; i < count; i++) group[i] = (int)i;
    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t i = 0; i < count; i++) {
            if (!scene.satellites[i].visible) continue;
            for (size_t j = i + 1; j < count; j++) {
                if (group[i] == group[j] || !scene.satellites[j].visible) continue;
                float dx = out[i].x - out[j].x;
                float dy = out[i].y - out[j].y;
                if (dx * dx + dy * dy < minimumDistance * minimumDistance) {
                    int from = group[j], to = group[i];
                    for (auto& g : group) {
                        if (g == from) g = to;
                    }
                    merged = true;
                }
            }
        }
    }

    std::vector<PixelPoint> original = out;
    for (size_t leader = 0; leader < count; leader++) {
        int members = 0;
        float cx = 0.0f, cy = 0.0f;
        for (size_t i = 0; i < count; i++) {
            if (group[i] != (int)leader) continue;
            members++;
            cx += original[i].x;
            cy += original[i].y;
        }
        if (members < 2) continue;
        cx /= members;
        cy /= members;
        float spread = minimumDistance / (2.0f * std::sin(PI_F / members));
        int n = 0;
        for (size_t i = 0; i < count; i++) {
            if (group[i] != (int)leader) continue;
            float angle = 2.0f * PI_F * n / members - PI_F / 2.0f;
            out[i] = PixelPoint { cx + spread * std::cos(angle), cy + spread * std::sin(angle) };
            n++;
        }
    }
}

int MapRenderer::hitTest(const Scene& scene, int x, int y, int touchRadius) const {
    int best = -1;
    float bestDistance = (float)(touchRadius * touchRadius);
    std::vector<PixelPoint> positions;
    markerPositions(scene, positions);
    for (size_t i = 0; i < scene.satellites.size(); i++) {
        if (!scene.satellites[i].visible) continue;
        float sx = positions[i].x;
        float sy = positions[i].y;
        float d = (sx - x) * (sx - x) + (sy - y) * (sy - y);
        if (d <= bestDistance) {
            bestDistance = d;
            best = (int)i;
        }
    }
    return best;
}

}
