/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>

namespace model {

enum class Subdivision {
    None = 0,
    Eighths,
    Triplets,
    Sixteenths,
};

struct BeatEvent {
    int beat{0};             // zero based beat inside the measure
    int sub_step{0};         // zero based subdivision step inside the beat
    bool main_beat{false};   // first step of a beat
    bool downbeat{false};    // first beat of the measure
};

class MetronomeModel {
public:
    static constexpr int kMinBpm = 20;
    static constexpr int kMaxBpm = 300;
    static constexpr int kMaxBeatsPerMeasure = 12;

    int bpm() const;
    void set_bpm(int bpm);
    void adjust_bpm(int delta);

    int beats_per_measure() const;
    void set_beats_per_measure(int beats);
    void adjust_beats_per_measure(int delta);

    int beat_unit() const;
    void set_beat_unit(int unit);
    void adjust_beat_unit(int delta);

    Subdivision subdivision() const;
    int subdivision_index() const;
    void set_subdivision_index(int index);
    void cycle_subdivision(int delta);
    int subdivision_factor() const;

    bool playing() const;
    void set_playing(bool playing);
    void toggle_playing();

    bool accent() const;
    void set_accent(bool enabled);

    bool sound_enabled() const;
    void set_sound_enabled(bool enabled);

    int volume() const;
    void set_volume(int volume);

    int current_beat() const;
    int current_sub_step() const;
    void reset_position();

    BeatEvent advance();
    bool tap(uint32_t now_ms);

    // 60000 / bpm / subdivision_factor, rounded to the nearest millisecond.
    uint32_t step_interval_ms() const;

private:
    static int clamp(int value, int minimum, int maximum);
    static int unit_index(int unit);

    int bpm_ = 120;
    int beats_per_measure_ = 4;
    int beat_unit_ = 4;
    Subdivision subdivision_ = Subdivision::None;
    bool playing_ = false;
    bool accent_ = true;
    bool sound_enabled_ = true;
    int volume_ = 70;
    int step_ = 0;

    std::array<uint32_t, 5> tap_times_{};
    std::size_t tap_count_ = 0;
};

} // namespace model
