/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"
#include "metronome_model.h"
#include "metronome_audio.h"

#include "lvgl.h"

#include <string>

namespace viewmodel {

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    lv_subject_t* title_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* quit_requested_subject();

    lv_subject_t* bpm_subject();
    lv_subject_t* beats_per_measure_subject();
    lv_subject_t* beat_unit_subject();
    lv_subject_t* subdivision_subject();
    lv_subject_t* accent_subject();
    lv_subject_t* sound_subject();
    lv_subject_t* volume_subject();
    lv_subject_t* playing_subject();
    lv_subject_t* beat_subject();
    lv_subject_t* sub_step_subject();
    lv_subject_t* pulse_subject();
    lv_subject_t* settings_selection_subject();
    lv_subject_t* config_revision_subject();

    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    model::AppPage current_page() const;
    void show_home_page();
    void show_settings_page();
    void toggle_page();
    void request_quit();

    void toggle_playing();
    void set_playing(bool playing);
    void adjust_bpm(int delta);
    void set_bpm(int bpm);
    void tap_tempo();

    void set_beats_per_measure(int beats);
    void set_beat_unit(int unit);
    void set_subdivision_index(int index);
    void set_accent(bool enabled);
    void set_sound_enabled(bool enabled);
    void set_volume(int volume);
    void toggle_accent();
    void toggle_sound();
    void adjust_volume(int delta);
    void select_setting(int index);
    void cycle_setting(int delta);
    void adjust_setting(int delta);

    bool initialize_audio(std::string& error);
    bool audio_available() const;

    int bpm() const;
    int beats_per_measure() const;
    int beat_unit() const;
    int subdivision_index() const;
    bool accent() const;
    bool sound_enabled() const;
    int volume() const;
    bool playing() const;

    void mark_config_changed();

private:
    void publish_all();
    void publish_metronome();
    void sync_audio();
    void restart_clock();

    static void metronome_timer_cb(lv_timer_t* timer);
    void tick_metronome();

    model::BaseModel model_;
    model::MetronomeModel metronome_;
    platform::audio::ClickSynth click_;
    lv_timer_t* metronome_timer_{nullptr};
    uint32_t last_step_tick_{0};

    reactive::StringSubject<32> title_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::BoolSubject quit_requested_subject_;

    reactive::IntSubject bpm_subject_;
    reactive::IntSubject beats_per_measure_subject_;
    reactive::IntSubject beat_unit_subject_;
    reactive::IntSubject subdivision_subject_;
    reactive::BoolSubject accent_subject_;
    reactive::BoolSubject sound_subject_;
    reactive::IntSubject volume_subject_;
    reactive::BoolSubject playing_subject_;
    reactive::IntSubject beat_subject_;
    reactive::IntSubject sub_step_subject_;
    reactive::IntSubject pulse_subject_;
    reactive::IntSubject settings_selection_subject_;
    reactive::IntSubject config_revision_subject_;
};

} // namespace viewmodel
