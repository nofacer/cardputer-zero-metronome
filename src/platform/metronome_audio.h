/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace platform::audio {

// Small, self contained click source for the metronome. Samples are synthesized
// at runtime so no audio asset or decoder is required. Playback is asynchronous:
// play_click() only queues a click and never blocks the LVGL event loop.
class ClickSynth {
public:
    ClickSynth();
    ~ClickSynth();

    ClickSynth(const ClickSynth&) = delete;
    ClickSynth& operator=(const ClickSynth&) = delete;

    bool initialize(std::string& error);
    bool available() const;
    void set_volume(int volume);
    void play_click(bool accent, bool soft = false);
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace platform::audio
