#include "DeviceLocation.h"
#include "SatelliteTracker.h"

#include <app/event.h>
#include <app/scheduler.h>
#include <lvgl/lvgl.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>

#include <memory>

extern "C" {

int main(int argc, char* argv[]) {
    AppInstanceId app_instance_id = app_scheduler_current_app_id();

    auto ctx = std::make_unique<Context>();
    ctx->appInstanceId = app_instance_id;

    struct TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    struct AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    ctx->eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx->taskWorkBit) == ERROR_NONE);

    bool downloads_ready = ctx->downloader.init(&event_group);
    satelliteTrackerLoad(ctx.get());

    lvgl_lock();
    satelliteTrackerInit(ctx.get());
    lvgl_unlock();

    WindowId window = window_manager_create_ext(app_instance_id, satelliteTrackerCreateWidgets, satelliteTrackerDestroyWidgets, ctx.get());
    ctx->window = window;

    DeviceLocation device_location;

    bool should_close = false;
    while (!should_close) {
        // The timeout lets a changed location setting take effect without another wake-up source.
        task_event_group_wait_any(&event_group, nullptr, pdMS_TO_TICKS(2000));

        struct AppEvent event;
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                should_close = true;
                break;
            }
        }
        if (should_close) break;

        if (downloads_ready) {
            DownloadResult result;
            while (ctx->downloader.process(result)) {
                TleList tles;
                satelliteTrackerReadDownload(ctx.get(), result, tles);
                lvgl_lock();
                satelliteTrackerHandleDownload(ctx.get(), result, tles);
                lvgl_unlock();
            }
        }

        satelliteTrackerDoTaskWork(ctx.get());

        lvgl_lock();
        bool use_device_location = ctx->settings.useDeviceLocation;
        lvgl_unlock();
        device_location.update(&event_group, use_device_location);
        Location fix;
        if (device_location.poll(fix)) {
            lvgl_lock();
            satelliteTrackerSetDeviceLocation(ctx.get(), &fix);
            lvgl_unlock();
        } else if (!device_location.isActive()) {
            lvgl_lock();
            satelliteTrackerSetDeviceLocation(ctx.get(), nullptr);
            lvgl_unlock();
        }
    }
    device_location.stop();

    window_manager_remove(window);
    ctx->downloader.deinit();
    lvgl_lock();
    satelliteTrackerTeardown(ctx.get());
    lvgl_unlock();
    satelliteTrackerDoTaskWork(ctx.get());
    task_event_group_release_bit(&event_group, ctx->taskWorkBit);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

}
