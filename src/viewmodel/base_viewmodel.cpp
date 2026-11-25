/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include <algorithm>

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model_.app_title()),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(page_to_int(model_.current_page())),
      quit_requested_subject_(false),
      bpm_subject_(metronome_.bpm()),
      beats_per_measure_subject_(metronome_.beats_per_measure()),
      beat_unit_subject_(metronome_.beat_unit()),
      subdivision_subject_(metronome_.subdivision_index()),
      accent_subject_(metronome_.accent()),
      sound_subject_(metronome_.sound_enabled()),
      volume_subject_(metronome_.volume()),
      playing_subject_(false),
      beat_subject_(0),
      sub_step_subject_(0),
      pulse_subject_(0),
      settings_selection_subject_(0),
      config_revision_subject_(0) {
    metronome_timer_ = lv_timer_create(&BaseViewModel::metronome_timer_cb, 5, this);
    if (metronome_timer_) {
        lv_timer_pause(metronome_timer_);
    }
}

BaseViewModel::~BaseViewModel() {
    if (metronome_timer_) {
        lv_timer_delete(metronome_timer_);
        metronome_timer_ = nullptr;
    }
}

lv_subject_t* BaseViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* BaseViewModel::dark_mode_subject() {
    return dark_mode_subject_.native();
}

lv_subject_t* BaseViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* BaseViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

lv_subject_t* BaseViewModel::bpm_subject() {
    return bpm_subject_.native();
}

lv_subject_t* BaseViewModel::beats_per_measure_subject() {
    return beats_per_measure_subject_.native();
}

lv_subject_t* BaseViewModel::beat_unit_subject() {
    return beat_unit_subject_.native();
}

lv_subject_t* BaseViewModel::subdivision_subject() {
    return subdivision_subject_.native();
}

lv_subject_t* BaseViewModel::accent_subject() {
    return accent_subject_.native();
}

lv_subject_t* BaseViewModel::sound_subject() {
    return sound_subject_.native();
}

lv_subject_t* BaseViewModel::volume_subject() {
    return volume_subject_.native();
}

lv_subject_t* BaseViewModel::playing_subject() {
    return playing_subject_.native();
}

lv_subject_t* BaseViewModel::beat_subject() {
    return beat_subject_.native();
}

lv_subject_t* BaseViewModel::sub_step_subject() {
    return sub_step_subject_.native();
}

lv_subject_t* BaseViewModel::pulse_subject() {
    return pulse_subject_.native();
}

lv_subject_t* BaseViewModel::settings_selection_subject() {
    return settings_selection_subject_.native();
}

lv_subject_t* BaseViewModel::config_revision_subject() {
    return config_revision_subject_.native();
}

bool BaseViewModel::is_dark_mode() const {
    return model_.dark_mode();
}

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    publish_all();
}

void BaseViewModel::toggle_dark_mode() {
    model_.toggle_dark_mode();
    publish_all();
    mark_config_changed();
}

model::AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

void BaseViewModel::show_home_page() {
    model_.set_current_page(model::AppPage::Home);
    publish_all();
}

void BaseViewModel::show_settings_page() {
    model_.set_current_page(model::AppPage::Settings);
    publish_all();
}

void BaseViewModel::toggle_page() {
    model_.toggle_page();
    publish_all();
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::toggle_playing() {
    set_playing(!metronome_.playing());
}

void BaseViewModel::set_playing(bool playing) {
    if (metronome_.playing() == playing) {
        return;
    }

    metronome_.set_playing(playing);
    if (playing) {
        metronome_.reset_position();
        beat_subject_.set(0);
        sub_step_subject_.set(0);
        restart_clock();
    }
    playing_subject_.set(playing);
    pulse_subject_.set(pulse_subject_.value() + 1);

    // Give immediate audible feedback so the start is never silent for a full beat.
    if (playing && metronome_.sound_enabled()) {
        click_.play_click(metronome_.accent(), false);
    }

    if (metronome_timer_) {
        if (playing) {
            lv_timer_resume(metronome_timer_);
            lv_timer_reset(metronome_timer_);
        }
        else {
            lv_timer_pause(metronome_timer_);
        }
    }
}

void BaseViewModel::adjust_bpm(int delta) {
    metronome_.adjust_bpm(delta);
    publish_metronome();
    restart_clock();
    mark_config_changed();
}

void BaseViewModel::set_bpm(int bpm) {
    metronome_.set_bpm(bpm);
    publish_metronome();
    restart_clock();
    mark_config_changed();
}

void BaseViewModel::tap_tempo() {
    if (metronome_.tap(lv_tick_get())) {
        publish_metronome();
        restart_clock();
        mark_config_changed();
    }
}

void BaseViewModel::set_beats_per_measure(int beats) {
    metronome_.set_beats_per_measure(beats);
    publish_metronome();
    restart_clock();
}

void BaseViewModel::set_beat_unit(int unit) {
    metronome_.set_beat_unit(unit);
    publish_metronome();
}

void BaseViewModel::set_subdivision_index(int index) {
    metronome_.set_subdivision_index(index);
    publish_metronome();
    restart_clock();
}

void BaseViewModel::set_accent(bool enabled) {
    metronome_.set_accent(enabled);
    publish_metronome();
}

void BaseViewModel::set_sound_enabled(bool enabled) {
    metronome_.set_sound_enabled(enabled);
    publish_metronome();
    sync_audio();
}

void BaseViewModel::set_volume(int volume) {
    metronome_.set_volume(volume);
    publish_metronome();
    sync_audio();
}

void BaseViewModel::toggle_accent() {
    metronome_.set_accent(!metronome_.accent());
    publish_metronome();
    mark_config_changed();
}

void BaseViewModel::toggle_sound() {
    metronome_.set_sound_enabled(!metronome_.sound_enabled());
    publish_metronome();
    sync_audio();
    if (metronome_.sound_enabled()) {
        click_.play_click(false, false);
    }
    mark_config_changed();
}

void BaseViewModel::adjust_volume(int delta) {
    metronome_.set_volume(metronome_.volume() + delta);
    publish_metronome();
    sync_audio();
    if (metronome_.sound_enabled()) {
        click_.play_click(false, false);
    }
    mark_config_changed();
}

void BaseViewModel::select_setting(int index) {
    settings_selection_subject_.set(std::clamp(index, 0, 6));
}

void BaseViewModel::cycle_setting(int delta) {
    const int current = settings_selection_subject_.value();
    select_setting((current + delta + 7) % 7);
}

void BaseViewModel::adjust_setting(int delta) {
    switch (settings_selection_subject_.value()) {
        case 0:
            metronome_.adjust_beats_per_measure(delta);
            break;
        case 1:
            metronome_.adjust_beat_unit(delta);
            break;
        case 2:
            metronome_.cycle_subdivision(delta);
            break;
        case 3:
            metronome_.set_accent(!metronome_.accent());
            break;
        case 4:
            metronome_.set_sound_enabled(!metronome_.sound_enabled());
            sync_audio();
            if (metronome_.sound_enabled()) {
                click_.play_click(false, false);
            }
            break;
        case 5:
            metronome_.set_volume(metronome_.volume() + delta * 5);
            sync_audio();
            if (metronome_.sound_enabled()) {
                click_.play_click(false, false);
            }
            break;
        case 6:
        default:
            toggle_dark_mode();
            return;
    }

    publish_metronome();
    mark_config_changed();
}

bool BaseViewModel::initialize_audio(std::string& error) {
    const bool initialized = click_.initialize(error);
    if (initialized) {
        sync_audio();
    }
    return initialized;
}

bool BaseViewModel::audio_available() const {
    return click_.available();
}

int BaseViewModel::bpm() const {
    return metronome_.bpm();
}

int BaseViewModel::beats_per_measure() const {
    return metronome_.beats_per_measure();
}

int BaseViewModel::beat_unit() const {
    return metronome_.beat_unit();
}

int BaseViewModel::subdivision_index() const {
    return metronome_.subdivision_index();
}

bool BaseViewModel::accent() const {
    return metronome_.accent();
}

bool BaseViewModel::sound_enabled() const {
    return metronome_.sound_enabled();
}

int BaseViewModel::volume() const {
    return metronome_.volume();
}

bool BaseViewModel::playing() const {
    return metronome_.playing();
}

void BaseViewModel::mark_config_changed() {
    config_revision_subject_.set(config_revision_subject_.value() + 1);
}

void BaseViewModel::publish_all() {
    title_subject_.set(model_.current_page() == model::AppPage::Home ? model_.app_title()
                                                                     : "Settings");
    dark_mode_subject_.set(model_.dark_mode());
    current_page_subject_.set(page_to_int(model_.current_page()));
    publish_metronome();
}

void BaseViewModel::publish_metronome() {
    bpm_subject_.set(metronome_.bpm());
    beats_per_measure_subject_.set(metronome_.beats_per_measure());
    beat_unit_subject_.set(metronome_.beat_unit());
    subdivision_subject_.set(metronome_.subdivision_index());
    accent_subject_.set(metronome_.accent());
    sound_subject_.set(metronome_.sound_enabled());
    volume_subject_.set(metronome_.volume());
    playing_subject_.set(metronome_.playing());
}

void BaseViewModel::sync_audio() {
    click_.set_volume(metronome_.sound_enabled() ? metronome_.volume() : 0);
}

void BaseViewModel::restart_clock() {
    last_step_tick_ = lv_tick_get();
}

void BaseViewModel::metronome_timer_cb(lv_timer_t* timer) {
    auto* view_model = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (view_model) {
        view_model->tick_metronome();
    }
}

void BaseViewModel::tick_metronome() {
    if (!metronome_.playing()) {
        return;
    }

    const uint32_t now = lv_tick_get();
    const uint32_t interval = metronome_.step_interval_ms();
    int catch_up = 0;
    while ((now - last_step_tick_) >= interval && catch_up++ < 4) {
        last_step_tick_ += interval;
        const auto step = metronome_.advance();

        beat_subject_.set(step.beat);
        sub_step_subject_.set(step.sub_step);
        pulse_subject_.set(pulse_subject_.value() + 1);

        if (metronome_.sound_enabled()) {
            click_.play_click(metronome_.accent() && step.downbeat, !step.main_beat);
        }
    }
}

} // namespace viewmodel
