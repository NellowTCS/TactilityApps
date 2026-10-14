#pragma once

#include <cstdint>
#include <vector>

namespace map {

enum class PixelFormat {
    Gray8,
    Rgb565,
};

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

enum class MarkerShape {
    Circle,
    Square,
    Triangle,
    Diamond,
    Plus,
    TriangleDown,
};

constexpr int MARKER_SHAPE_COUNT = 6;

struct Theme {
    Rgb ocean;
    Rgb land;
    Rgb coast;
    Rgb grid;
    Rgb observer;
    Rgb markerOutline;
    Rgb focus;
    Rgb satellite[MARKER_SHAPE_COUNT];
    uint8_t nightDarken;
    uint8_t twilightDarken;
};

const Theme& colorTheme();
const Theme& grayTheme();

struct GeoPoint {
    float lat;
    float lon;
};

struct PixelPoint {
    float x;
    float y;
};

struct SatelliteOverlay {
    /** False when the position is unknown (no orbit data, or SGP4 failed). Such satellites get no marker and no track. */
    bool visible = true;
    GeoPoint position;
    MarkerShape shape;
    /** Slots after the first six use the outlines of the same shapes. */
    bool hollow;
    int colorIndex;
    bool focused;
    bool drawTrack;
    /** Points before the current position, oldest first. */
    std::vector<GeoPoint> pastTrack;
    /** Points after the current position, oldest first. */
    std::vector<GeoPoint> futureTrack;
    /** 0 means no footprint. */
    float footprintRadiusDeg;
};

struct Scene {
    bool showNight;
    GeoPoint subsolar;
    bool hasObserver;
    GeoPoint observer;
    std::vector<SatelliteOverlay> satellites;
    /** A frame along the map edges, used to show keyboard focus. */
    bool frame = false;
    Rgb frameColor {};
};

/** A pixel buffer that the renderer can draw into. Does not own the memory. */
class Surface {

public:

    Surface(void* data, int width, int height, int strideBytes, PixelFormat format);

    int width() const { return w; }
    int height() const { return h; }

    void fill(Rgb color);
    void setPixel(int x, int y, Rgb color);
    void darkenPixel(int x, int y, uint8_t amount);
    /** dashPhase (optional) carries the dash pattern position from one call to the next, so a polyline keeps its dashes. */
    void line(int x0, int y0, int x1, int y1, Rgb color, int thickness, int dashOn, int dashOff, int* dashPhase = nullptr);
    void marker(int cx, int cy, int radius, MarkerShape shape, bool hollow, Rgb fillColor, Rgb outlineColor);
    void circle(int cx, int cy, int radius, int thickness, Rgb color);
    void copyFrom(const Surface& other);

private:

    uint8_t* rowPointer(int y) const { return bytes + y * stride; }

    uint8_t* bytes;
    int w;
    int h;
    int stride;
    PixelFormat pixelFormat;
};

class MapRenderer {

public:

    MapRenderer() = default;
    ~MapRenderer();
    MapRenderer(const MapRenderer&) = delete;
    MapRenderer& operator=(const MapRenderer&) = delete;

    /** Allocates the base map buffer and draws land, coasts and grid. Returns false when out of memory. */
    bool setup(int width, int height, PixelFormat format, const Theme& theme);

    int width() const { return mapWidth; }
    int height() const { return mapHeight; }
    int markerRadius() const;

    /** Draws the full map into target, which must have the size and format given to setup(). */
    void render(Surface& target, const Scene& scene) const;

    /** Pixel position of a geographic point. */
    void project(GeoPoint point, float& x, float& y) const;

    /** Index of the satellite whose marker is closest to (x, y) within the touch radius, or -1. */
    int hitTest(const Scene& scene, int x, int y, int touchRadius) const;

    /**
     * Pixel positions of the markers. Markers that would cover each other (a module docked to a station) are spread around their common spot.
     * Satellites that are not visible keep their projected position and are left out of the spreading.
     */
    void markerPositions(const Scene& scene, std::vector<PixelPoint>& out) const;

private:

    void drawBase(Surface& surface) const;
    void drawNight(Surface& surface, const Scene& scene) const;
    void drawPolyline(Surface& surface, const std::vector<GeoPoint>& points, Rgb color, int thickness, int dashOn, int dashOff) const;
    void drawFootprint(Surface& surface, GeoPoint center, float radiusDeg, Rgb color) const;

    uint8_t* baseData = nullptr;
    int mapWidth = 0;
    int mapHeight = 0;
    int bytesPerPixel = 1;
    PixelFormat pixelFormat = PixelFormat::Gray8;
    const Theme* activeTheme = nullptr;
};

}
