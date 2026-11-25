/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"

#include <array>

namespace view::widgets {

class BeatIndicator : public BaseWidgets {
public:
    BeatIndicator(lv_obj_t* parent, viewmodel::BaseViewModel& view_model);
    ~BeatIndicator() override;

    void build() override;

private:
    static void refresh_cb(lv_observer_t* observer, lv_subject_t* subject);
    void refresh();

    viewmodel::BaseViewModel& view_model_;
    std::array<lv_obj_t*, model::MetronomeModel::kMaxBeatsPerMeasure> dots_{};
};

} // namespace view::widgets
