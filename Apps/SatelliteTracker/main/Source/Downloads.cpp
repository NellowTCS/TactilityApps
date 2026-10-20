#include "Downloads.h"

#include "Storage.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* BASE_URL = "https://celestrak.org/NORAD/elements/gp.php";
constexpr const char* CERTIFICATE_FILE = "celestrak.pem";

void partFile(const char* target, char* out, size_t outSize) {
    snprintf(out, outSize, "%s.part", target);
}

/** Downloads go to a ".part" file first, so a failed download keeps the previous copy. */
bool finishFile(const char* target) {
    char partPath[sizeof(DownloadJob::target) + 8];
    partFile(target, partPath, sizeof(partPath));
    remove(target);
    return rename(partPath, target) == 0;
}

}

bool Downloader::init(TaskEventGroup* eventGroup) {
    group = eventGroup;
    if (task_event_group_claim_bit(group, &wakeBit) != ERROR_NONE) return false;
    mutex = xSemaphoreCreateMutex();
    if (mutex == nullptr) return false;
    certificate = storageReadAsset(CERTIFICATE_FILE);
    return certificate != nullptr;
}

void Downloader::deinit() {
    if (running) {
        http_download_cancel(&subscription);
        http_download_unsubscribe(&subscription);
        running = false;
    }
    if (mutex != nullptr) {
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }
    largeFree(certificate);
    certificate = nullptr;
    if (group != nullptr && wakeBit != 0) {
        task_event_group_release_bit(group, wakeBit);
        wakeBit = 0;
    }
}

bool Downloader::enqueue(const DownloadJob& job) {
    if (mutex == nullptr) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    queue.push_back(job);
    xSemaphoreGive(mutex);
    task_event_group_signal(group, wakeBit);
    return true;
}

bool Downloader::isBusy() {
    if (mutex == nullptr) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool busy = running || !queue.empty();
    xSemaphoreGive(mutex);
    return busy;
}

bool Downloader::hasQueued(DownloadKind kind, const char* tag) {
    if (mutex == nullptr) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool found = running && current.kind == kind && strcmp(current.tag, tag) == 0;
    for (const auto& job : queue) {
        if (job.kind == kind && strcmp(job.tag, tag) == 0) found = true;
    }
    xSemaphoreGive(mutex);
    return found;
}

bool Downloader::process(DownloadResult& out) {
    if (running) {
        HttpDownloadEvent event;
        if (http_download_poll(&subscription, &event) != ERROR_NONE) return false;
        http_download_unsubscribe(&subscription);
        xSemaphoreTake(mutex, portMAX_DELAY);
        running = false;
        out.job = current;
        xSemaphoreGive(mutex);
        out.success = event.type == HTTP_DOWNLOAD_EVENT_SUCCESS && finishFile(current.target);
        out.statusCode = event.status_code;
        out.message[0] = '\0';
        if (event.type == HTTP_DOWNLOAD_EVENT_ERROR) {
            strncpy(out.message, event.error.message, sizeof(out.message) - 1);
            out.message[sizeof(out.message) - 1] = '\0';
        } else if (!out.success) {
            snprintf(out.message, sizeof(out.message), "could not save the file");
        }
        task_event_group_signal(group, wakeBit);
        return true;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    if (queue.empty()) {
        xSemaphoreGive(mutex);
        return false;
    }
    current = queue.front();
    queue.pop_front();
    running = true;
    xSemaphoreGive(mutex);

    subscription = HttpDownloadSubscription {};
    bool started = http_download_subscribe(&subscription, group) == ERROR_NONE;
    char partPath[sizeof(current.target) + 8];
    partFile(current.target, partPath, sizeof(partPath));
    if (started && http_download_start(current.url, certificate, partPath, &subscription) != ERROR_NONE) {
        http_download_unsubscribe(&subscription);
        started = false;
    }
    if (!started) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        running = false;
        out.job = current;
        xSemaphoreGive(mutex);
        out.success = false;
        out.statusCode = 0;
        strncpy(out.message, "Could not start the download", sizeof(out.message) - 1);
        out.message[sizeof(out.message) - 1] = '\0';
        task_event_group_signal(group, wakeBit);
        return true;
    }
    return false;
}

bool Downloader::buildCatalogJob(uint32_t catalogNumber, DownloadJob& out) {
    out = DownloadJob {};
    out.kind = DownloadKind::Tracked;
    snprintf(out.tag, sizeof(out.tag), "%u", (unsigned)catalogNumber);
    snprintf(out.url, sizeof(out.url), "%s?CATNR=%u&FORMAT=tle", BASE_URL, (unsigned)catalogNumber);
    char name[32];
    snprintf(name, sizeof(name), "sat-%u.tle", (unsigned)catalogNumber);
    return storageUserPath(name, out.target, sizeof(out.target));
}

bool Downloader::buildGroupJob(const char* groupId, DownloadJob& out) {
    out = DownloadJob {};
    out.kind = DownloadKind::Group;
    snprintf(out.tag, sizeof(out.tag), "%s", groupId);
    snprintf(out.url, sizeof(out.url), "%s?GROUP=%s&FORMAT=tle", BASE_URL, groupId);
    char name[48];
    snprintf(name, sizeof(name), "group-%s.tle", groupId);
    return storageUserPath(name, out.target, sizeof(out.target));
}

bool Downloader::buildSearchJob(const char* text, DownloadJob& out) {
    out = DownloadJob {};
    out.kind = DownloadKind::Search;
    while (*text == ' ') text++;
    if (*text == '\0') return false;
    // The tag identifies the search when its result arrives, so the URL is built from the tag and both describe the same text.
    if (snprintf(out.tag, sizeof(out.tag), "%s", text) >= (int)sizeof(out.tag)) return false;

    bool numeric = true;
    for (const char* p = out.tag; *p; p++) {
        if (!isdigit((unsigned char)*p)) numeric = false;
    }
    constexpr const char* SUFFIX = "&FORMAT=tle";
    int written;
    if (numeric) {
        written = snprintf(out.url, sizeof(out.url), "%s?CATNR=%s%s", BASE_URL, out.tag, SUFFIX);
    } else {
        written = snprintf(out.url, sizeof(out.url), "%s?NAME=", BASE_URL);
        int reserved = (int)strlen(SUFFIX) + 1;
        for (const char* p = out.tag; *p; p++) {
            // An encoded character takes up to 3 bytes.
            if (written + 3 + reserved > (int)sizeof(out.url)) return false;
            unsigned char c = (unsigned char)*p;
            if (isalnum(c) || c == '-' || c == '.' || c == '_') {
                out.url[written++] = (char)c;
            } else {
                written += snprintf(out.url + written, sizeof(out.url) - written, "%%%02X", c);
            }
        }
        written += snprintf(out.url + written, sizeof(out.url) - written, "%s", SUFFIX);
    }
    if (written >= (int)sizeof(out.url)) return false;
    return storageUserPath("search.tle", out.target, sizeof(out.target));
}
