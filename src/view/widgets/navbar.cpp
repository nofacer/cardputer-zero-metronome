/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "navbar.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

namespace view::widgets {
namespace {

struct IconSpec {
    const char* text;
    lv_event_cb_t click_cb;
};

// CP0 key spec: icon centers at x = 49, 105, 161, 217, 273 and y = 153.
// The nav bar starts at y = 140, so a 22px icon is placed at y = 2 in the bar.
constexpr int32_t kIconSize = 22;
constexpr std::array<int32_t, 5> kNavButtonLeft = {38, 94, 150, 206, 262};

struct ShortcutSpec {
    const char* key;
    const char* action;
};

constexpr std::array<ShortcutSpec, 3> kSettingsShortcuts = {{
    {"ESC", "Back"},
    {"Z/", "Prev"},
    {"C/", "Next"},
}};

} // namespace

NavBar::NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

NavBar::~NavBar() {
    for (std::size_t i = 0; i < icon_buttons_.size(); ++i) {
        if (icon_buttons_[i]) {
            platform::unregister_nav_button(i, icon_buttons_[i]->root());
        }
    }

    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
    if (theme_observer_) {
        lv_observer_remove(theme_observer_);
        theme_observer_ = nullptr;
    }
    if (playing_observer_) {
        lv_observer_remove(playing_observer_);
        playing_observer_ = nullptr;
    }
}

void NavBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kNavBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    create_icon_buttons();
    create_shortcut_hints();
    page_observer_ = reactive::observe_obj(core_obj_, view_model_.current_page_subject(), update_icons_cb, this);
    theme_observer_ = reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), update_icons_cb, this);
    playing_observer_ = reactive::observe_obj(core_obj_, view_model_.playing_subject(), update_icons_cb, this);
    update_icon_buttons();
}

void NavBar::create_shortcut_hints() {
    shortcut_bar_ = lv_obj_create(core_obj_);
    lv_obj_remove_style_all(shortcut_bar_);
    lv_obj_set_size(shortcut_bar_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(shortcut_bar_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_flex_flow(shortcut_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(shortcut_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(shortcut_bar_, 10, 0);
    lv_obj_clear_flag(shortcut_bar_, LV_OBJ_FLAG_SCROLLABLE);

    text_font_ = assets_.load_standard_font(12, app::StandardFontWeight::Regular);
    if (!text_font_) {
        text_font_ = &lv_font_montserrat_12;
    }

    for (std::size_t index = 0; index < kSettingsShortcuts.size(); ++index) {
        const auto& shortcut = kSettingsShortcuts[index];
        auto* item = lv_obj_create(shortcut_bar_);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_PCT(100));
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 3, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        auto* key = lv_label_create(item);
        lv_label_set_text(key, shortcut.key);
        lv_obj_set_style_text_font(key, text_font_, 0);
        shortcut_key_labels_[index] = key;

        auto* label = lv_label_create(item);
        lv_label_set_text(label, shortcut.action);
        lv_obj_set_style_text_font(label, text_font_, 0);
        reactive::bind_theme(label, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);
    }
}

void NavBar::create_icon_buttons() {
    const auto light_color = view::palette(false).text;
    const auto dark_color = view::palette(true).text;
    icon_font_ = assets_.load_font("Phosphor-Fill.ttf", 18);
    if (!icon_font_) {
        icon_font_ = &lv_font_montserrat_14;
    }
    text_font_ = assets_.load_standard_font(12, app::StandardFontWeight::Regular);
    if (!text_font_) {
        text_font_ = &lv_font_montserrat_12;
    }

    for (std::size_t i = 0; i < icon_buttons_.size(); ++i) {
        icon_buttons_[i] = std::make_unique<IconButton>(core_obj_,
                                                        view_model_,
                                                        kIconSize,
                                                        kIconSize,
                                                        "",
                                                        icon_font_,
                                                        light_color,
                                                        dark_color,
                                                        nullptr,
                                                        &view_model_,
                                                        true);
        icon_buttons_[i]->build();
        lv_obj_align(icon_buttons_[i]->root(), LV_ALIGN_TOP_LEFT, kNavButtonLeft[i], 2);
        platform::register_nav_button(i, icon_buttons_[i]->root());
    }
}

void NavBar::update_icon_buttons() {
    const bool is_settings_page = view_model_.current_page() == model::AppPage::Settings;
    const bool is_dark_mode = view_model_.is_dark_mode();
    const bool playing = lv_subject_get_int(view_model_.playing_subject()) != 0;
    const auto colors = view::palette(is_dark_mode);
    const char* theme_icon = is_dark_mode ? ICON_SUN : ICON_MOON;
    const char* play_icon = playing ? "II" : "GO";

    /* Buttons shown on the metronome page (keys 4..8). */
    const std::array<IconSpec, 5> home_icons = {{
        {ICON_SIGN_OUT, request_quit_cb},
        {play_icon, toggle_playing_cb},
        {theme_icon, toggle_theme_cb},
        {"TAP", tap_tempo_cb},
        {ICON_SQUARE_ARROW_RIGHT, toggle_page_cb},
    }};
    /* Hidden targets for the settings-page shortcut keys. */
    const std::array<IconSpec, 5> settings_icons = {{
        {"", nullptr},
        {"", decrement_setting_cb},
        {"", nullptr},
        {"", increment_setting_cb},
        {"", nullptr},
    }};

    platform::set_nav_shortcut_mode(is_settings_page);
    for (auto* key_label : shortcut_key_labels_) {
        if (key_label) {
            lv_obj_set_style_text_color(key_label, colors.primary, 0);
        }
    }
    if (shortcut_bar_) {
        if (is_settings_page) {
            lv_obj_remove_flag(shortcut_bar_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(shortcut_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const auto& icons = is_settings_page ? settings_icons : home_icons;
    for (std::size_t i = 0; i < icon_buttons_.size(); ++i) {
        auto& button = icon_buttons_[i];
        if (!button) {
            continue;
        }

        button->set_text(icons[i].text);
        button->set_font(icons[i].text[0] >= 'A' && icons[i].text[0] <= 'Z' ? text_font_ : icon_font_);
        if (button->root()) {
            if (is_settings_page) {
                lv_obj_add_flag(button->root(), LV_OBJ_FLAG_HIDDEN);
            }
            else {
                lv_obj_remove_flag(button->root(), LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_remove_event_cb(button->root(), toggle_theme_cb);
            lv_obj_remove_event_cb(button->root(), toggle_page_cb);
            lv_obj_remove_event_cb(button->root(), toggle_playing_cb);
            lv_obj_remove_event_cb(button->root(), tap_tempo_cb);
            lv_obj_remove_event_cb(button->root(), toggle_sound_cb);
            lv_obj_remove_event_cb(button->root(), request_quit_cb);
            lv_obj_remove_event_cb(button->root(), increment_setting_cb);
            lv_obj_remove_event_cb(button->root(), decrement_setting_cb);
            if (icons[i].click_cb) {
                lv_obj_add_event_cb(button->root(), icons[i].click_cb, LV_EVENT_CLICKED, &view_model_);
                button->set_enabled(true);
            }
            else {
                button->set_enabled(false);
            }
        }
    }
}

void NavBar::toggle_theme_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->toggle_dark_mode();
    }
}

void NavBar::toggle_page_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->toggle_page();
    }
}

void NavBar::toggle_playing_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->toggle_playing();
    }
}

void NavBar::tap_tempo_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->tap_tempo();
    }
}

void NavBar::toggle_sound_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->toggle_sound();
    }
}

void NavBar::request_quit_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->request_quit();
    }
}

void NavBar::increment_setting_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->adjust_setting(1);
    }
}

void NavBar::decrement_setting_cb(lv_event_t* event) {
    auto* view_model = static_cast<viewmodel::BaseViewModel*>(lv_event_get_user_data(event));
    if (view_model) {
        view_model->adjust_setting(-1);
    }
}

void NavBar::update_icons_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* nav_bar = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (nav_bar) {
        nav_bar->update_icon_buttons();
    }
}

} // namespace view::widgets
