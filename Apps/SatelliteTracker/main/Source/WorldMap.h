#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Land outlines as rings of (longitude, latitude) pairs in 1/100 degree. Rings are closed implicitly. */
extern const uint16_t WORLD_MAP_RING_COUNT;
extern const uint16_t WORLD_MAP_RING_LENGTHS[];
extern const int16_t WORLD_MAP_POINTS[];

#ifdef __cplusplus
}
#endif
