#include "Sgp4Bridge.h"

#include "sgp4/SGP4.h"

#include <stdlib.h>
#include <string.h>

struct ElsetRec* sgp4_bridge_create(void) {
    return (struct ElsetRec*)calloc(1, sizeof(ElsetRec));
}

void sgp4_bridge_destroy(struct ElsetRec* record) {
    free(record);
}

bool sgp4_bridge_init(struct ElsetRec* record, const struct Sgp4Elements* elements) {
    const double xpdotp = 1440.0 / (2.0 * pi);

    memset(record, 0, sizeof(ElsetRec));
    record->whichconst = wgs72;
    record->jdsatepoch = elements->epochJulianDay;
    record->jdsatepochF = elements->epochJulianDayFraction;
    record->bstar = elements->bstar;
    record->inclo = elements->inclinationDeg * deg2rad;
    record->nodeo = elements->raanDeg * deg2rad;
    record->argpo = elements->argumentOfPerigeeDeg * deg2rad;
    record->mo = elements->meanAnomalyDeg * deg2rad;
    record->ecco = elements->eccentricity;
    record->no_kozai = elements->meanMotionRevPerDay / xpdotp;
    record->ndot = elements->meanMotionDot / (xpdotp * 1440.0);
    record->nddot = elements->meanMotionDotDot / (xpdotp * 1440.0 * 1440.0);

    sgp4init('a', record);
    return record->error == 0;
}

bool sgp4_bridge_propagate(struct ElsetRec* record, double minutesSinceEpoch, double position[3], double velocity[3]) {
    record->error = 0;
    sgp4(record, minutesSinceEpoch, position, velocity);
    return record->error == 0;
}

double sgp4_bridge_gmst(double julianDayUt1) {
    return gstime(julianDayUt1);
}
