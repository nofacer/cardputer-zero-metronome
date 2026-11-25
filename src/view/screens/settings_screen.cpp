/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"

#include <array>

namespace screen {
namespace {

constexpr std::size_t kRowCount = 7;
constexpr int32_t kRowHeight = 14;

struct RowWidgets {
    viewmodel::BaseViewModel* view_model{nullptr};
    std::size_t index{0};
    lv_obj_t* row{nullptr};
    lv_obj_t* value{nullptr};
};

struct SettingsBinding {
    viewmodel::BaseViewModel* view_model{nullptr};
    std::array<RowWidgets, kRowCount> rows{};
};

void cleanup_binding(lv_event_t* event) {
    delete static_cast<SettingsBinding*>(lv_event_get_user_data(event));
}

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
            return "OFF";
    }
}

void refresh_row(RowWidgets* row) {
    if (!row || !row->view_model || !row->row || !row->value) {
        return;
    }

    auto* view_model = row->view_model;
    switch (row->index) {
        case 0:
            lv_label_set_text_fmt(row->value,
                                  "%d / %d",
                                  lv_subject_get_int(view_model->beats_per_measure_subject()),
                                  lv_subject_get_int(view_model->beat_unit_subject()));
            break;
        case 1:
            lv_label_set_text_fmt(row->value, "1/%d", lv_subject_get_int(view_model->beat_unit_subject()));
            break;
        case 2:
            lv_label_set_text(row->value,
                              subdivision_name(lv_subject_get_int(view_model->subdivision_subject())));
            break;
        case 3:
            lv_label_set_text(row->value,
                              lv_subject_get_int(view_model->accent_subject()) ? "ON" : "OFF");
            break;
        case 4:
            lv_label_set_text(row->value,
                              lv_subject_get_int(view_model->sound_subject()) ? "ON" : "OFF");
            break;
        case 5:
            lv_label_set_text_fmt(row->value, "%d%%", lv_subject_get_int(view_model->volume_subject()));
            break;
        case 6:
        default:
            lv_label_set_text(row->value, view_model->is_dark_mode() ? "DARK" : "LIGHT");
            break;
    }

    const bool selected = lv_subject_get_int(view_model->settings_selection_subject()) ==
                          static_cast<int>(row->index);
    const auto colors = view::palette(view_model->is_dark_mode());
    lv_obj_set_style_bg_color(row->row, selected ? colors.primary : colors.surface, 0);
    lv_obj_set_style_bg_opa(row->row, selected ? LV_OPA_20 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(row->row, selected ? colors.primary : colors.border, 0);
    lv_obj_set_style_border_width(row->row, selected ? 1 : 0, 0);
    lv_obj_set_style_text_color(row->row, selected ? colors.primary : colors.text, 0);
}

void row_observer(lv_observer_t* observer, lv_subject_t*) {
    refresh_row(static_cast<RowWidgets*>(lv_observer_get_user_data(observer)));
}

} // namespace

SettingsScreen::SettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void SettingsScreen::build_content(lv_obj_t* content) {
    const lv_font_t* label_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    const lv_font_t* value_font = assets().load_standard_font(12);
    if (!label_font) label_font = &lv_font_montserrat_12;
    if (!value_font) value_font = &lv_font_montserrat_12;

    static const std::array<const char*, kRowCount> labels = {
        "Beats / bar", "Beat unit", "Subdivide", "Accent", "Click", "Volume", "Theme"};

    auto* binding = new SettingsBinding{&view_model(), {}};
    lv_obj_add_event_cb(content, cleanup_binding, LV_EVENT_DELETE, binding);

    for (std::size_t i = 0; i < kRowCount; ++i) {
        auto* row = lv_obj_create(content);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 300, kRowHeight);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 4 + static_cast<int32_t>(i) * (kRowHeight + 1));
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        auto* label = lv_label_create(row);
        lv_label_set_text(label, labels[i]);
        lv_obj_set_style_text_font(label, label_font, 0);

        auto* value = lv_label_create(row);
        lv_obj_set_style_text_font(value, value_font, 0);

        auto& widgets = binding->rows[i];
        widgets = RowWidgets{&view_model(), i, row, value};

        lv_subject_add_observer_obj(view_model().settings_selection_subject(), row_observer, row, &widgets);
        lv_subject_add_observer_obj(view_model().dark_mode_subject(), row_observer, row, &widgets);
        switch (i) {
            case 0:
            case 1:
                lv_subject_add_observer_obj(view_model().beats_per_measure_subject(), row_observer, row, &widgets);
                lv_subject_add_observer_obj(view_model().beat_unit_subject(), row_observer, row, &widgets);
                break;
            case 2:
                lv_subject_add_observer_obj(view_model().subdivision_subject(), row_observer, row, &widgets);
                break;
            case 3:
                lv_subject_add_observer_obj(view_model().accent_subject(), row_observer, row, &widgets);
                break;
            case 4:
                lv_subject_add_observer_obj(view_model().sound_subject(), row_observer, row, &widgets);
                break;
            case 5:
                lv_subject_add_observer_obj(view_model().volume_subject(), row_observer, row, &widgets);
                break;
            case 6:
            default:
                break;
        }

        refresh_row(&widgets);
    }
}

} // namespace screen
