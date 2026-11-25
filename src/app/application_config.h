/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

namespace app {

struct ApplicationConfig {
    bool dark_mode = false;
    int bpm = 120;
    int beats_per_measure = 4;
    int beat_unit = 4;
    int subdivision = 0;
    bool accent = true;
    bool sound = true;
    int volume = 70;
};

bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error);
bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::string& error);

} // namespace app
