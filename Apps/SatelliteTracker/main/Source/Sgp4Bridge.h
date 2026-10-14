#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ElsetRec;

struct Sgp4Elements {
    double epochJulianDay;
    double epochJulianDayFraction;
    double bstar;
    double meanMotionDot;
    double meanMotionDotDot;
    double inclinationDeg;
    double raanDeg;
    double eccentricity;
    double argumentOfPerigeeDeg;
    double meanAnomalyDeg;
    double meanMotionRevPerDay;
};

struct ElsetRec* sgp4_bridge_create(void);
void sgp4_bridge_destroy(struct ElsetRec* record);
bool sgp4_bridge_init(struct ElsetRec* record, const struct Sgp4Elements* elements);
bool sgp4_bridge_propagate(struct ElsetRec* record, double minutesSinceEpoch, double position[3], double velocity[3]);
double sgp4_bridge_gmst(double julianDayUt1);

#ifdef __cplusplus
}
#endif
