/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "application_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace app {
namespace {

std::string trim(std::string value) {
    const auto is_not_space = [](unsigned char character) {
        return !std::isspace(character);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), is_not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), is_not_space).base(), value.end());
    return value;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool parse_bool(const std::string& value, bool& result) {
    const auto normalized = lowercase(trim(value));
    if (normalized == "yes" || normalized == "true" || normalized == "1") {
        result = true;
        return true;
    }
    if (normalized == "no" || normalized == "false" || normalized == "0") {
        result = false;
        return true;
    }
    return false;
}

bool parse_int(const std::string& value, int& result) {
    const auto normalized = trim(value);
    if (normalized.empty()) {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(normalized.c_str(), &end, 10);
    if (end == normalized.c_str() || *end != '\0' || parsed < -2147483647L || parsed > 2147483647L) {
        return false;
    }

    result = static_cast<int>(parsed);
    return true;
}

bool is_section(const std::string& line, const std::string& expected) {
    const auto normalized = trim(line);
    return normalized.size() >= 2 && normalized.front() == '[' && normalized.back() == ']' &&
           lowercase(trim(normalized.substr(1, normalized.size() - 2))) == expected;
}

bool is_key(const std::string& line, const std::string& expected) {
    const auto separator = line.find('=');
    return separator != std::string::npos &&
           lowercase(trim(line.substr(0, separator))) == expected;
}

} // namespace

bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error) {
    error.clear();
    std::ifstream input(path);
    if (!input) {
        error = "could not open file";
        return false;
    }

    std::string section;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            section = lowercase(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const auto key = lowercase(trim(line.substr(0, separator)));
        const auto value = line.substr(separator + 1);
        if (section != "application") {
            continue;
        }

        if (key == "dark_mode") {
            if (!parse_bool(value, config.dark_mode)) {
                error = "invalid dark_mode value at line " + std::to_string(line_number);
                return false;
            }
        }
        else if (key == "bpm") {
            if (!parse_int(value, config.bpm)) {
                error = "invalid bpm value at line " + std::to_string(line_number);
                return false;
            }
        }
        else if (key == "beats_per_measure") {
            if (!parse_int(value, config.beats_per_measure)) {
                error = "invalid beats_per_measure value at line " + std::to_string(line_number);
                return false;
            }
        }
        else if (key == "beat_unit") {
            if (!parse_int(value, config.beat_unit)) {
                error = "invalid beat_unit value at line " + std::to_string(line_number);
                return false;
            }
        }
        else if (key == "subdivision") {
            if (!parse_int(value, config.subdivision)) {
                error = "invalid subdivision value at line " + std::to_string(line_number);
                return false;
            }
        }
        else if (key == "accent") {
            if (!parse_bool(value, config.accent)) {
                error = "invalid accent value at line " + std::to_string(line_number);
                return false;
            }
        }
        else if (key == "sound") {
            if (!parse_bool(value, config.sound)) {
                error = "invalid sound value at line " + std::to_string(line_number);
                return false;
            }
        }
        else if (key == "volume") {
            if (!parse_int(value, config.volume)) {
                error = "invalid volume value at line " + std::to_string(line_number);
                return false;
            }
        }
    }

    return true;
}

bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::string& error) {
    error.clear();

    const std::filesystem::path config_path(path);
    const auto parent = config_path.parent_path();
    if (!parent.empty()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) {
            error = "could not create parent directory: " + filesystem_error.message();
            return false;
        }
    }

    std::vector<std::string> lines;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    const auto yes_no = [](bool value) { return std::string(value ? "yes" : "no"); };
    const std::vector<std::pair<std::string, std::string>> values = {
        {"dark_mode", yes_no(config.dark_mode)},
        {"bpm", std::to_string(config.bpm)},
        {"beats_per_measure", std::to_string(config.beats_per_measure)},
        {"beat_unit", std::to_string(config.beat_unit)},
        {"subdivision", std::to_string(config.subdivision)},
        {"accent", yes_no(config.accent)},
        {"sound", yes_no(config.sound)},
        {"volume", std::to_string(config.volume)},
    };

    std::vector<std::string> updated;
    std::vector<bool> written(values.size(), false);
    bool in_application_section = false;
    bool found_application_section = false;

    const auto append_missing = [&]() {
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (written[i]) {
                continue;
            }
            updated.push_back(values[i].first + "=" + values[i].second);
            written[i] = true;
        }
    };

    for (const auto& current_line : lines) {
        const auto normalized = trim(current_line);
        const bool section_header = normalized.size() >= 2 && normalized.front() == '[' &&
                                    normalized.back() == ']';
        if (section_header) {
            if (in_application_section) {
                append_missing();
            }
            in_application_section = is_section(current_line, "application");
            found_application_section = found_application_section || in_application_section;
        }

        if (in_application_section) {
            bool replaced = false;
            for (std::size_t i = 0; i < values.size() && !replaced; ++i) {
                if (!is_key(current_line, values[i].first)) {
                    continue;
                }
                if (!written[i]) {
                    updated.push_back(values[i].first + "=" + values[i].second);
                    written[i] = true;
                }
                replaced = true;
            }
            if (replaced) {
                continue;
            }
        }

        updated.push_back(current_line);
    }

    if (in_application_section) {
        append_missing();
    }
    if (!found_application_section) {
        if (!updated.empty() && !updated.back().empty()) {
            updated.emplace_back();
        }
        updated.emplace_back("[application]");
        append_missing();
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        error = "could not open file for writing";
        return false;
    }
    for (const auto& updated_line : updated) {
        output << updated_line << '\n';
    }
    if (!output) {
        error = "failed while writing file";
        return false;
    }

    return true;
}

} // namespace app
