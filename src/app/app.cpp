/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "help_popup.h"
#include "logger.h"
#include "linux_input.h"
#include "screenshot_service.h"
#include "screen_manager.h"
#include "base_viewmodel.h"
#include "theme.h"
#include "toast.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#if USE_DESKTOP
#include "desktop_simulator_frame.h"
#endif

#if !USE_DESKTOP
#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif

#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

#ifndef APP_CONFIG_FILE
#define APP_CONFIG_FILE "metronome.conf"
#endif

namespace app {
namespace {

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

struct ConfigPersistence {
    std::string config_path;
    viewmodel::BaseViewModel* view_model{nullptr};
};

std::string writable_config_path() {
#if USE_DESKTOP
    return APP_CONFIG_FILE;
#else
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path root(xdg_config_home);
        if (!root.empty() && root.is_absolute()) {
            return (root / "metronome" / "metronome.conf").string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path root(home);
        if (!root.empty() && root.is_absolute()) {
            return (root / ".config" / "metronome" / "metronome.conf").string();
        }
    }
    return APP_CONFIG_FILE;
#endif
}

void persist_config_observer(lv_observer_t* observer, lv_subject_t*) {
    auto* persistence = static_cast<ConfigPersistence*>(lv_observer_get_user_data(observer));
    if (!persistence || !persistence->view_model) {
        return;
    }

    auto* view_model = persistence->view_model;
    ApplicationConfig config;
    config.dark_mode = view_model->is_dark_mode();
    config.bpm = view_model->bpm();
    config.beats_per_measure = view_model->beats_per_measure();
    config.beat_unit = view_model->beat_unit();
    config.subdivision = view_model->subdivision_index();
    config.accent = view_model->accent();
    config.sound = view_model->sound_enabled();
    config.volume = view_model->volume();

    std::string error;
    if (save_application_config(persistence->config_path, config, error)) {
        LOG_INFO("saved config: {} (bpm={})", persistence->config_path, config.bpm);
    } else {
        LOG_WARN("failed to save config {}: {}", persistence->config_path, error);
    }
}

class ShortcutController {
public:
    ShortcutController(viewmodel::BaseViewModel& view_model, AssetManager& assets)
        : view_model_(view_model),
          toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), view_model, assets)),
          help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), view_model, assets)) {
        toast_->build();
        help_popup_->build();
        platform::set_global_key_listener(global_key_listener, this);
        platform::set_key_release_listener(key_release_listener, this);
    }

    ~ShortcutController() {
        platform::clear_key_release_listener(key_release_listener, this);
        platform::clear_global_key_listener(global_key_listener, this);
    }

private:
    bool handle_key(uint32_t key, bool long_pressed) {
        if (key == platform::kKeyPrintScreen) {
            if (!long_pressed) {
                const auto result = platform::screenshot::capture_active_screen();
                if (result.success) {
                    LOG_INFO("screenshot saved: {}", result.path);
                    toast_->show_highlighted("Saved to ",
                                             result.path,
                                             "",
                                             view::widgets::ToastTone::Success,
                                             5200,
                                             true);
                }
                else {
                    LOG_WARN("screenshot failed: {}", result.error);
                    toast_->show("Screenshot failed: " + result.error,
                                 view::widgets::ToastTone::Error,
                                 3200);
                }
            }
            return true;
        }

        if (key == platform::kKeyHelp) {
            if (!long_pressed) {
                toast_->hide();
                if (help_popup_->visible()) {
                    help_popup_->hide();
                }
                else {
                    help_popup_->show(view_model_.current_page());
                }
            }
            return true;
        }

        if (help_popup_->visible()) {
            if (key == LV_KEY_ESC && !long_pressed) {
                help_popup_->hide();
            }
            return true;
        }

        const bool is_home_page = view_model_.current_page() == model::AppPage::Home;
        if (is_home_page) {
            if (key == ' ' || key == LV_KEY_ENTER) {
                if (!long_pressed) {
                    view_model_.toggle_playing();
                }
                return true;
            }
            if (key == 't' || key == 'T') {
                if (!long_pressed) {
                    view_model_.tap_tempo();
                }
                return true;
            }
            if (key == 'm' || key == 'M') {
                if (!long_pressed) {
                    view_model_.toggle_sound();
                }
                return true;
            }
            if (key == 'a' || key == 'A') {
                if (!long_pressed) {
                    view_model_.toggle_accent();
                }
                return true;
            }
            // CP0 key spec: F/Z/X/C act as arrow keys outside of text input.
            const bool up = key == LV_KEY_UP || key == 'f' || key == 'F';
            const bool down = key == LV_KEY_DOWN || key == 'x' || key == 'X';
            const bool left = key == LV_KEY_LEFT || key == 'z' || key == 'Z';
            const bool right = key == LV_KEY_RIGHT || key == 'c' || key == 'C';
            if (up || down || left || right) {
                if (!long_pressed) {
                    const int step = up || down ? 10 : 1;
                    view_model_.adjust_bpm((up || right) ? step : -step);
                }
                return true;
            }
        }
        else {
            if (key >= '1' && key <= '7') {
                if (!long_pressed) {
                    view_model_.select_setting(static_cast<int>(key - '1'));
                }
                return true;
            }
            if (key == LV_KEY_UP || key == LV_KEY_PREV || key == 'f' || key == 'F') {
                if (!long_pressed) {
                    view_model_.cycle_setting(-1);
                }
                return true;
            }
            if (key == LV_KEY_DOWN || key == LV_KEY_NEXT || key == 'x' || key == 'X') {
                if (!long_pressed) {
                    view_model_.cycle_setting(1);
                }
                return true;
            }
        }

        const bool is_escape = key == LV_KEY_ESC;
        const bool is_home_exit_key =
            key == '4' &&
            ((!long_pressed && is_home_page) ||
             (long_pressed && exit_key_pressed_ == key));
        if (!is_escape && !is_home_exit_key) {
            return false;
        }

        if (long_pressed) {
            if (exit_key_pressed_ == key && exit_armed_) {
                toast_->hide();
                view_model_.request_quit();
            }
        }
        else {
            exit_key_pressed_ = key;
            exit_armed_ = view_model_.current_page() == model::AppPage::Home;
            if (exit_armed_) {
                toast_->show_persistent_highlighted("Hold ",
                                                    "ESC",
                                                    " to exit",
                                                    view::widgets::ToastTone::Warning);
            }
        }
        return true;
    }

    void handle_release(uint32_t key) {
        if (key != exit_key_pressed_) {
            return;
        }

        if (exit_armed_) {
            toast_->hide();
        } else if (key == LV_KEY_ESC &&
                   view_model_.current_page() == model::AppPage::Settings) {
            view_model_.show_home_page();
        }
        exit_key_pressed_ = 0;
        exit_armed_ = false;
    }

    static bool global_key_listener(uint32_t key,
                                    const char*,
                                    bool long_pressed,
                                    void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        return controller && controller->handle_key(key, long_pressed);
    }

    static void key_release_listener(uint32_t key, const char*, void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        if (controller) {
            controller->handle_release(key);
        }
    }

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    uint32_t exit_key_pressed_{0};
    bool exit_armed_{false};
};

#if !USE_DESKTOP
lv_display_t* init_device_display() {
#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#else
    auto* display = lv_linux_fbdev_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_fbdev_set_file(display, APP_FRAMEBUFFER_DEVICE) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#endif
}
#endif

} // namespace

int Application::run() {
    logger::Logger::init();
    logger::Logger::set_tag("metronome");

    lv_init();

    AssetManager assets;
    for (const auto& root : assets.roots()) {
        LOG_INFO("asset root: {}", root.string());
    }

    viewmodel::BaseViewModel view_model;
    const std::string user_config_path = writable_config_path();
    std::string loaded_config_path = APP_CONFIG_FILE;
    if (user_config_path != APP_CONFIG_FILE) {
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_config_path, filesystem_error)) {
            loaded_config_path = user_config_path;
        }
    }

    ApplicationConfig config;
    std::string config_error;
    if (load_application_config(loaded_config_path, config, config_error)) {
        view_model.set_dark_mode(config.dark_mode);
        view_model.set_bpm(config.bpm);
        view_model.set_beats_per_measure(config.beats_per_measure);
        view_model.set_beat_unit(config.beat_unit);
        view_model.set_subdivision_index(config.subdivision);
        view_model.set_accent(config.accent);
        view_model.set_sound_enabled(config.sound);
        view_model.set_volume(config.volume);
        LOG_INFO("loaded config: {} (bpm={}, {}/{})",
                 loaded_config_path,
                 config.bpm,
                 config.beats_per_measure,
                 config.beat_unit);
    } else {
        LOG_WARN("failed to load config {}: {}; using defaults", loaded_config_path, config_error);
    }

#if USE_DESKTOP
    DesktopSimulatorFrame simulator_frame(assets);
    auto* display = simulator_frame.display();
#else
    auto* display = init_device_display();
#endif
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    auto* standard_font = assets.load_standard_font(14);
    view::apply_lvgl_theme(display,
                           view_model.is_dark_mode(),
                           standard_font ? standard_font : LV_FONT_DEFAULT);

#if USE_DESKTOP
    simulator_frame.bind_dark_mode(view_model.dark_mode_subject());
#endif

    std::string audio_error;
    if (!view_model.initialize_audio(audio_error)) {
        LOG_WARN("metronome click audio unavailable: {}", audio_error);
    }

    ScreenManager screen_manager(view_model, assets);
    screen_manager.start();
    ShortcutController shortcuts(view_model, assets);

    ConfigPersistence config_persistence{user_config_path, &view_model};
    auto* config_observer = lv_subject_add_observer(view_model.config_revision_subject(),
                                                    persist_config_observer,
                                                    &config_persistence);

    bool running = true;
    auto* quit_observer = lv_subject_add_observer(view_model.quit_requested_subject(),
                                                  quit_requested_observer,
                                                  &running);

    LOG_INFO("LVGL app started at {}x{}", lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));
    while (running
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        lv_timer_handler();
        lv_delay_ms(5);
    }

    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }
    if (config_observer) {
        lv_observer_remove(config_observer);
    }

    return 0;
}

} // namespace app
