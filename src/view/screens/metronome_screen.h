/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <memory>

namespace view::widgets {
class BeatIndicator;
}

namespace screen {

class MetronomeScreen : public BaseScreen {
public:
    MetronomeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~MetronomeScreen() override;

private:
    void build_content(lv_obj_t* content) override;

    std::unique_ptr<view::widgets::BeatIndicator> beat_indicator_;
};

} // namespace screen
