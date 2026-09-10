#include "segmenter.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (0)

int main() {
    float frame[160];
    std::fill(std::begin(frame), std::end(frame), 0.1f);

    float silence[160]{};
    Segmenter silentSegmenter;
    for (int i = 0; i < 100; ++i) CHECK(!silentSegmenter.feed(silence, 0.99f));

    Segmenter segmenter;
    int partials = 0;
    int finals = 0;
    for (int i = 0; i < 300; ++i) {
        if (auto event = segmenter.feed(frame, i < 220 ? 0.99f : 0.0f)) {
            event->final ? ++finals : ++partials;
        }
    }
    CHECK(partials >= 2);
    CHECK(finals == 1);
    CHECK(!segmenter.flush());

    Segmenter forcedSegmenter;
    std::optional<Segment> forced;
    for (int i = 0; i < 1300 && !forced; ++i) {
        auto event = forcedSegmenter.feed(frame, 0.99f);
        if (event && event->final) forced = std::move(event);
    }
    CHECK(forced.has_value());
    CHECK(forced->audio.size() == 188800);
    CHECK(forced->end > forced->start);

    std::optional<Segment> carried;
    for (int i = 0; i < 50 && !carried; ++i) {
        auto event = forcedSegmenter.feed(i < 5 ? frame : silence, i < 5 ? 0.99f : 0.0f);
        if (event && event->final) carried = std::move(event);
    }
    CHECK(carried.has_value());
    CHECK(carried->audio.size() >= 3200);

    SegmentQueue queue;
    queue.push({1, false, {}});
    queue.push({1, false, {1.0f}});
    CHECK(queue.pop()->audio.size() == 1);
    for (int i = 0; i < 5; ++i) queue.push({static_cast<uint64_t>(i), true, {}});
    CHECK(queue.drops == 1);
    CHECK(queue.pop()->id == 1);

    std::cout << "PASS: silence, endpointing, hard split carry, partial replacement, bounded queue\n";
    return 0;
}
