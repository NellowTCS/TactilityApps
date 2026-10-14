#pragma once

#include <tactility/freertos/freertos.h>
#include <tactility/freertos/semphr.h>
#include <http/download.h>
#include <tactility/concurrent/task_event_group.h>

#include <cstdint>
#include <deque>

enum class DownloadKind {
    Tracked,
    Group,
    Search,
};

struct DownloadJob {
    DownloadKind kind;
    char url[200];
    char target[200];
    char tag[32];
};

struct DownloadResult {
    DownloadJob job;
    bool success;
    int32_t statusCode;
    char message[HTTP_DOWNLOAD_ERROR_MESSAGE_MAX_LEN];
};

/**
 * Runs CelesTrak downloads one after another on the app's main task.
 * enqueue() and the status getters may be called from any task.
 */
class Downloader {

public:

    bool init(TaskEventGroup* eventGroup);
    void deinit();

    /** Returns false when the downloader could not be initialized, the job is then dropped. */
    bool enqueue(const DownloadJob& job);
    bool isBusy();
    bool hasQueued(DownloadKind kind, const char* tag);

    /** Main task only: starts queued work and collects a finished download. Returns true when out holds a result. */
    bool process(DownloadResult& out);

    static bool buildCatalogJob(uint32_t catalogNumber, DownloadJob& out);
    static bool buildGroupJob(const char* groupId, DownloadJob& out);
    static bool buildSearchJob(const char* text, DownloadJob& out);

private:

    TaskEventGroup* group = nullptr;
    uint32_t wakeBit = 0;
    SemaphoreHandle_t mutex = nullptr;
    std::deque<DownloadJob> queue;
    bool running = false;
    DownloadJob current {};
    HttpDownloadSubscription subscription {};
    char certificatePath[200] {};
};
