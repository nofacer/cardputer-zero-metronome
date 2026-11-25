/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "metronome_model.h"

#include <algorithm>
#include <cstddef>

namespace model {
namespace {

constexpr std::array<int, 4> kBeatUnits = {2, 4, 8, 16};

int factor_for(Subdivision subdivision) {
    switch (subdivision) {
        case Subdivision::Eighths:
            return 2;
        case Subdivision::Triplets:
            return 3;
        case Subdivision::Sixteenths:
            return 4;
        case Subdivision::None:
        default:
            return 1;
    }
}

} // namespace

int MetronomeModel::clamp(int value, int minimum, int maximum) {
    return std::min(maximum, std::max(minimum, value));
}

int MetronomeModel::unit_index(int unit) {
    const auto it = std::find(kBeatUnits.begin(), kBeatUnits.end(), unit);
    return it == kBeatUnits.end() ? 1 : static_cast<int>(it - kBeatUnits.begin());
}

int MetronomeModel::bpm() const {
    return bpm_;
}

void MetronomeModel::set_bpm(int bpm) {
    bpm_ = clamp(bpm, kMinBpm, kMaxBpm);
}

void MetronomeModel::adjust_bpm(int delta) {
    set_bpm(bpm_ + delta);
}

int MetronomeModel::beats_per_measure() const {
    return beats_per_measure_;
}

void MetronomeModel::set_beats_per_measure(int beats) {
    beats_per_measure_ = clamp(beats, 1, kMaxBeatsPerMeasure);
    step_ %= beats_per_measure_ * factor_for(subdivision_);
}

void MetronomeModel::adjust_beats_per_measure(int delta) {
    set_beats_per_measure(beats_per_measure_ + delta);
}

int MetronomeModel::beat_unit() const {
    return beat_unit_;
}

void MetronomeModel::set_beat_unit(int unit) {
    beat_unit_ = kBeatUnits[static_cast<std::size_t>(unit_index(unit))];
}

void MetronomeModel::adjust_beat_unit(int delta) {
    const int next = clamp(unit_index(beat_unit_) + delta, 0, static_cast<int>(kBeatUnits.size()) - 1);
    beat_unit_ = kBeatUnits[static_cast<std::size_t>(next)];
}

Subdivision MetronomeModel::subdivision() const {
    return subdivision_;
}

int MetronomeModel::subdivision_index() const {
    return static_cast<int>(subdivision_);
}

void MetronomeModel::set_subdivision_index(int index) {
    subdivision_ = static_cast<Subdivision>(clamp(index, 0, 3));
    step_ %= beats_per_measure_ * factor_for(subdivision_);
}

void MetronomeModel::cycle_subdivision(int delta) {
    set_subdivision_index(static_cast<int>(subdivision_) + delta);
}

int MetronomeModel::subdivision_factor() const {
    return factor_for(subdivision_);
}

bool MetronomeModel::playing() const {
    return playing_;
}

void MetronomeModel::set_playing(bool playing) {
    playing_ = playing;
}

void MetronomeModel::toggle_playing() {
    playing_ = !playing_;
}

bool MetronomeModel::accent() const {
    return accent_;
}

void MetronomeModel::set_accent(bool enabled) {
    accent_ = enabled;
}

bool MetronomeModel::sound_enabled() const {
    return sound_enabled_;
}

void MetronomeModel::set_sound_enabled(bool enabled) {
    sound_enabled_ = enabled;
}

int MetronomeModel::volume() const {
    return volume_;
}

void MetronomeModel::set_volume(int volume) {
    volume_ = clamp(volume, 0, 100);
}

int MetronomeModel::current_beat() const {
    return step_ / factor_for(subdivision_);
}

int MetronomeModel::current_sub_step() const {
    return step_ % factor_for(subdivision_);
}

void MetronomeModel::reset_position() {
    step_ = 0;
}

BeatEvent MetronomeModel::advance() {
    const int factor = factor_for(subdivision_);
    const int total_steps = beats_per_measure_ * factor;
    const int beat = step_ / factor;
    const int sub_step = step_ % factor;
    const BeatEvent event{beat, sub_step, sub_step == 0, step_ == 0};

    step_ = (step_ + 1) % total_steps;
    return event;
}

bool MetronomeModel::tap(uint32_t now_ms) {
    if (tap_count_ != 0 && now_ms - tap_times_[tap_count_ - 1] > 3000U) {
        tap_count_ = 0;
    }

    if (tap_count_ == tap_times_.size()) {
        std::copy(tap_times_.begin() + 1, tap_times_.end(), tap_times_.begin());
        tap_count_ = tap_times_.size() - 1;
    }
    tap_times_[tap_count_++] = now_ms;

    if (tap_count_ < 2) {
        return false;
    }

    uint32_t total = 0;
    const std::size_t intervals = tap_count_ - 1;
    for (std::size_t i = 1; i < tap_count_; ++i) {
        total += tap_times_[i] - tap_times_[i - 1];
    }

    const uint32_t average = total / static_cast<uint32_t>(intervals);
    if (average == 0) {
        return false;
    }

    const int measured = (60000 + static_cast<int>(average) / 2) / static_cast<int>(average);
    set_bpm(measured);
    return true;
}

uint32_t MetronomeModel::step_interval_ms() const {
    const uint32_t ticks = static_cast<uint32_t>(bpm_) * static_cast<uint32_t>(subdivision_factor());
    const uint32_t interval = (60000U + ticks / 2U) / ticks;
    return std::max<uint32_t>(1U, interval);
}

} // namespace model
