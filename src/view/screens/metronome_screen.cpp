/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "metronome_screen.h"

#include "asset_manager.h"
#include "beat_indicator.h"
#include "bindings.h"

namespace screen {
namespace {

const char* subdivision_name(int index) {
    switch (index) {
        case 1:
            return "1/8";
        case 2:
            return "1/12";
        case 3:
            return "1/16";
        case 0:
        default:
            return "NONE";
    }
}

struct InfoBinding {
    viewmodel::BaseViewModel* view_model;
};

void cleanup_info_binding(lv_event_t* event) {
    delete static_cast<InfoBinding*>(lv_event_get_user_data(event));
}

void refresh_info(lv_obj_t* label, viewmodel::BaseViewModel* view_model) {
    if (!label || !view_model) {
        return;
    }

    const int subdivision = lv_subject_get_int(view_model->subdivision_subject());
    const bool accent = lv_subject_get_int(view_model->accent_subject()) != 0;
    const bool sound = lv_subject_get_int(view_model->sound_subject()) != 0;
    lv_label_set_text_fmt(label,
                          "%d/%d  %s  %s%s",
                          lv_subject_get_int(view_model->beats_per_measure_subject()),
                          lv_subject_get_int(view_model->beat_unit_subject()),
                          subdivision_name(subdivision),
                          accent ? "ACCENT" : "FLAT",
                          sound ? "" : "  MUTE");
}

void info_observer(lv_observer_t* observer, lv_subject_t*) {
    auto* label = lv_observer_get_target_obj(observer);
    auto* binding = static_cast<InfoBinding*>(lv_observer_get_user_data(observer));
    if (binding) {
        refresh_info(label, binding->view_model);
    }
}

} // namespace

MetronomeScreen::MetronomeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

MetronomeScreen::~MetronomeScreen() = default;

void MetronomeScreen::build_content(lv_obj_t* content) {
    const lv_font_t* small_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    const lv_font_t* medium_font = assets().load_standard_font(14);
    const lv_font_t* big_font = assets().load_standard_font(28);
    if (!small_font) small_font = &lv_font_montserrat_12;
    if (!medium_font) medium_font = &lv_font_montserrat_14;
    if (!big_font) big_font = &lv_font_montserrat_28;

    auto* status = lv_label_create(content);
    lv_label_set_text(status, "STOPPED");
    reactive::bind_bool_text(status, view_model().playing_subject(), "STOPPED", "PLAYING");
    reactive::bind_theme(status, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_set_style_text_font(status, small_font, 0);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 10, 5);

    auto* bpm_row = lv_obj_create(content);
    lv_obj_remove_style_all(bpm_row);
    lv_obj_set_size(bpm_row, 180, 38);
    lv_obj_align(bpm_row, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_flex_flow(bpm_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bpm_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bpm_row, 6, 0);
    lv_obj_clear_flag(bpm_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bpm_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    auto* bpm = lv_label_create(bpm_row);
    lv_label_bind_text(bpm, view_model().bpm_subject(), "%d");
    lv_obj_set_style_text_font(bpm, big_font, 0);
    reactive::bind_theme(bpm, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* bpm_name = lv_label_create(bpm_row);
    lv_label_set_text(bpm_name, "BPM");
    lv_obj_set_style_text_font(bpm_name, small_font, 0);
    reactive::bind_theme(bpm_name, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    beat_indicator_ = std::make_unique<view::widgets::BeatIndicator>(content, view_model());
    beat_indicator_->build();
    lv_obj_align(beat_indicator_->root(), LV_ALIGN_TOP_MID, 0, 54);

    auto* info = lv_label_create(content);
    lv_obj_set_width(info, 300);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(info, medium_font, 0);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 78);
    auto* binding = new InfoBinding{&view_model()};
    lv_obj_add_event_cb(info, cleanup_info_binding, LV_EVENT_DELETE, binding);
    lv_subject_add_observer_obj(view_model().beats_per_measure_subject(), info_observer, info, binding);
    lv_subject_add_observer_obj(view_model().beat_unit_subject(), info_observer, info, binding);
    lv_subject_add_observer_obj(view_model().subdivision_subject(), info_observer, info, binding);
    lv_subject_add_observer_obj(view_model().accent_subject(), info_observer, info, binding);
    lv_subject_add_observer_obj(view_model().sound_subject(), info_observer, info, binding);
    refresh_info(info, &view_model());
    reactive::bind_theme(info, view_model().dark_mode_subject(), reactive::ThemeRole::Text);

    auto* hints = lv_label_create(content);
    lv_label_set_text(hints, "SPACE Play  F/X +/-10  Z/C +/-1  T Tap");
    lv_obj_set_width(hints, 310);
    lv_obj_set_style_text_align(hints, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hints, small_font, 0);
    lv_obj_align(hints, LV_ALIGN_BOTTOM_MID, 0, -2);
    reactive::bind_theme(hints, view_model().dark_mode_subject(), reactive::ThemeRole::Text);
}

} // namespace screen
