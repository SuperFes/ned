#include "MultibufferFoldSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::size_t& LineThresholdStorage() {
        static std::size_t lines = 40;
        return lines;
    }

    std::size_t& ByteThresholdStorage() {
        static std::size_t bytes = 2000;
        return bytes;
    }

    std::size_t& ExcerptCapStorage() {
        static std::size_t count = 100;
        return count;
    }

} // namespace

void SetMultibufferAutoCollapseLineThreshold(std::size_t lines) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    LineThresholdStorage() = lines;
}

std::size_t MultibufferAutoCollapseLineThreshold() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return LineThresholdStorage();
}

void SetMultibufferAutoCollapseByteThreshold(std::size_t bytes) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    ByteThresholdStorage() = bytes;
}

std::size_t MultibufferAutoCollapseByteThreshold() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return ByteThresholdStorage();
}

void SetMultibufferAutoCollapseExcerptCap(std::size_t count) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    ExcerptCapStorage() = count;
}

std::size_t MultibufferAutoCollapseExcerptCap() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return ExcerptCapStorage();
}

} // namespace ned::editor
