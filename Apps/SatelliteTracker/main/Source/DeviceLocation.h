#pragma once

#include "LocationApi.h"

#include <cstdint>

/** Follows Tactility's location service. Only used on the app's main task. */
class DeviceLocation {

public:

    /** Subscribes while wanted and location services are enabled, so the providers (GPS, network) run. */
    void update(TaskEventGroup* eventGroup, bool wanted);
    void stop();

    bool isActive() const { return subscribed; }

    /** Returns true when the service has a fix that was not returned before. */
    bool poll(Location& out);

private:

    LocationSubscription subscription {};
    bool subscribed = false;
    uint64_t lastUptimeMs = 0;
};
