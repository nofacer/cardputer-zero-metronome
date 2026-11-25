/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "beat_indicator.h"

#include "bindings.h"
#include "theme.h"

#include <algorithm>

namespace view::widgets {
namespace {

constexpr int32_t kDotSize = 14;
constexpr int32_t kMaxDots = model::MetronomeModel::kMaxBeatsPerMeasure;

} // namespace

BeatIndicator::BeatIndicator(lv_obj_t* parent, viewmodel::BaseViewModel& view_model)
    : BaseWidgets(parent), view_model_(view_model) {}

BeatIndicator::~BeatIndicator() = default;

void BeatIndicator::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_SIZE_CONTENT, kDotSize + 6);
    lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(core_obj_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(core_obj_, 5, 0);
    lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < kMaxDots; ++i) {
        auto* dot = lv_obj_create(core_obj_);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, kDotSize, kDotSize);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        dots_[static_cast<std::size_t>(i)] = dot;
    }

    reactive::observe_obj(core_obj_, view_model_.pulse_subject(), refresh_cb, this);
    reactive::observe_obj(core_obj_, view_model_.beat_subject(), refresh_cb, this);
    reactive::observe_obj(core_obj_, view_model_.sub_step_subject(), refresh_cb, this);
    reactive::observe_obj(core_obj_, view_model_.beats_per_measure_subject(), refresh_cb, this);
    reactive::observe_obj(core_obj_, view_model_.accent_subject(), refresh_cb, this);
    reactive::observe_obj(core_obj_, view_model_.playing_subject(), refresh_cb, this);
    reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), refresh_cb, this);
    refresh();
}

void BeatIndicator::refresh_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* indicator = static_cast<BeatIndicator*>(lv_observer_get_user_data(observer));
    if (indicator) {
        indicator->refresh();
    }
}

void BeatIndicator::refresh() {
    if (!core_obj_) {
        return;
    }

    const auto colors = view::palette(view_model_.is_dark_mode());
    const int count = std::min(lv_subject_get_int(view_model_.beats_per_measure_subject()), kMaxDots);
    const int current = lv_subject_get_int(view_model_.beat_subject());
    const bool playing = lv_subject_get_int(view_model_.playing_subject()) != 0;
    const bool accent = lv_subject_get_int(view_model_.accent_subject()) != 0;
    const bool subdivision_tick = lv_subject_get_int(view_model_.sub_step_subject()) != 0;

    for (int i = 0; i < kMaxDots; ++i) {
        auto* dot = dots_[static_cast<std::size_t>(i)];
        if (!dot) {
            continue;
        }
        if (i >= count) {
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
        const bool active = playing && i == current;
        const lv_color_t highlight =
            active && i == 0 && accent ? colors.warning : colors.primary;

        lv_obj_set_style_bg_color(dot, highlight, 0);
        lv_obj_set_style_bg_opa(dot,
                                active ? (subdivision_tick ? LV_OPA_50 : LV_OPA_COVER)
                                       : LV_OPA_TRANSP,
                                0);
        lv_obj_set_style_border_width(dot, active ? 2 : 1, 0);
        lv_obj_set_style_border_color(dot, active ? highlight : colors.border, 0);
        lv_obj_set_style_transform_scale(dot, active ? (subdivision_tick ? 110 : 125) : 100, 0);
    }
}

} // namespace view::widgets
