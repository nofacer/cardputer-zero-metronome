/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "metronome_audio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#if USE_DESKTOP
#include <SDL.h>
#else
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#define MINIAUDIO_IMPLEMENTATION
#include "vendor/miniaudio.h"
#endif

namespace platform::audio {
namespace {

constexpr int kSampleRate = 44100;

std::vector<int16_t> make_click(int sample_rate, float frequency, float duration_ms) {
    const auto count = static_cast<std::size_t>(sample_rate * duration_ms / 1000.0F);
    std::vector<int16_t> samples(count);
    constexpr float kPi = 3.14159265358979323846F;

    for (std::size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
        const float envelope = std::exp(-t * 75.0F);
        const float body = std::sin(2.0F * kPi * frequency * t) * envelope;
        const float transient =
            std::sin(2.0F * kPi * frequency * 2.6F * t) * std::exp(-t * 280.0F) * 0.35F;
        const float value = std::clamp((body + transient) * 0.72F, -1.0F, 1.0F);
        samples[i] = static_cast<int16_t>(value * 32767.0F);
    }
    return samples;
}

int16_t scale_sample(int16_t sample, int volume) {
    const float gain = static_cast<float>(std::clamp(volume, 0, 100)) / 100.0F;
    return static_cast<int16_t>(static_cast<float>(sample) * gain);
}

#if !USE_DESKTOP
std::vector<float> to_stereo_float(const std::vector<int16_t>& samples) {
    std::vector<float> frames(samples.size() * 2U);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float value = static_cast<float>(samples[i]) / 32768.0F;
        frames[i * 2U] = value;
        frames[i * 2U + 1U] = value;
    }
    return frames;
}
#endif

} // namespace

struct ClickSynth::Impl {
#if USE_DESKTOP
    SDL_AudioDeviceID device{0};
    bool audio_subsystem_owned{false};
    std::atomic<uint32_t> sequence{0};
    std::atomic<bool> accent{false};
    std::atomic<bool> soft{false};
    std::atomic<int> volume{70};
    std::vector<int16_t> normal_click;
    std::vector<int16_t> strong_click;
    uint32_t last_sequence{0};
    std::size_t position{0};
    const std::vector<int16_t>* current{nullptr};

    static void audio_callback(void* user_data, Uint8* stream, int length) {
        auto* impl = static_cast<Impl*>(user_data);
        auto* output = reinterpret_cast<int16_t*>(stream);
        const auto frames = static_cast<std::size_t>(length) / sizeof(int16_t);
        std::fill(output, output + frames, int16_t{0});

        const uint32_t requested = impl->sequence.load(std::memory_order_acquire);
        if (requested != impl->last_sequence) {
            impl->last_sequence = requested;
            impl->current = impl->accent.load(std::memory_order_relaxed) ? &impl->strong_click
                                                                         : &impl->normal_click;
            impl->position = 0;
        }

        if (!impl->current || impl->position >= impl->current->size()) {
            return;
        }

        int volume = impl->volume.load(std::memory_order_relaxed);
        if (impl->soft.load(std::memory_order_relaxed)) {
            volume = volume * 45 / 100;
        }
        const auto& samples = *impl->current;
        for (std::size_t i = 0; i < frames && impl->position < samples.size(); ++i) {
            output[i] = scale_sample(samples[impl->position++], volume);
        }
    }
#else
    // CardputerZero's speaker is owned by pipewire-pulse; like the official
    // launcher, use miniaudio's PulseAudio backend instead of raw ALSA devices.
    ma_context context{};
    ma_engine engine{};
    ma_audio_buffer normal_buffer{};
    ma_audio_buffer strong_buffer{};
    ma_sound normal_sound{};
    ma_sound strong_sound{};
    std::vector<float> normal_pcm;
    std::vector<float> strong_pcm;
    bool context_ready{false};
    bool engine_ready{false};
    bool normal_sound_ready{false};
    bool strong_sound_ready{false};
    bool normal_buffer_ready{false};
    bool strong_buffer_ready{false};
    std::chrono::steady_clock::time_point warmup_deadline{};

    std::atomic<int> volume{70};
    std::mutex mutex;
    std::condition_variable wakeup;
    std::queue<std::pair<bool, bool>> pending;
    std::thread worker;
    bool stop{false};

    void run() {
        for (;;) {
            bool accent_click = false;
            bool soft_click = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wakeup.wait(lock, [this] { return stop || !pending.empty(); });
                if (stop && pending.empty()) {
                    return;
                }
                accent_click = pending.front().first;
                soft_click = pending.front().second;
                pending.pop();
            }

            if (warmup_deadline != std::chrono::steady_clock::time_point{}) {
                std::this_thread::sleep_until(warmup_deadline);
                warmup_deadline = {};
            }

            auto& sound = accent_click ? strong_sound : normal_sound;
            const bool sound_ready = accent_click ? strong_sound_ready : normal_sound_ready;
            if (!sound_ready) {
                continue;
            }

            float gain = static_cast<float>(std::clamp(volume.load(std::memory_order_relaxed), 0, 100)) /
                         100.0F;
            if (soft_click) {
                gain *= 0.45F;
            }
            ma_sound_set_volume(&sound, gain);
            ma_sound_seek_to_pcm_frame(&sound, 0);
            ma_sound_start(&sound);
        }
    }
#endif
};

ClickSynth::ClickSynth()
    : impl_(std::make_unique<Impl>()) {}

ClickSynth::~ClickSynth() {
    shutdown();
}

bool ClickSynth::initialize(std::string& error) {
#if USE_DESKTOP
    if (impl_->device != 0) {
        return true;
    }

    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            error = std::string("SDL audio init failed: ") + SDL_GetError();
            return false;
        }
        impl_->audio_subsystem_owned = true;
    }

    SDL_AudioSpec desired{};
    desired.freq = kSampleRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = 512;
    desired.callback = &Impl::audio_callback;
    desired.userdata = impl_.get();

    SDL_AudioSpec obtained{};
    impl_->device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (impl_->device == 0) {
        error = std::string("SDL audio device failed: ") + SDL_GetError();
        return false;
    }

    impl_->normal_click = make_click(obtained.freq, 880.0F, 45.0F);
    impl_->strong_click = make_click(obtained.freq, 1320.0F, 55.0F);
    SDL_PauseAudioDevice(impl_->device, 0);
    return true;
#else
    if (impl_->engine_ready) {
        return true;
    }

    ma_backend backends[] = {ma_backend_pulseaudio};
    ma_result result = ma_context_init(backends, 1, nullptr, &impl_->context);
    if (result != MA_SUCCESS) {
        error = std::string("PulseAudio context init failed: ") + ma_result_description(result);
        return false;
    }
    impl_->context_ready = true;

    ma_engine_config config = ma_engine_config_init();
    config.pContext = &impl_->context;
    config.channels = 2;
    config.sampleRate = kSampleRate;
    config.periodSizeInMilliseconds = 20;
    result = ma_engine_init(&config, &impl_->engine);
    if (result != MA_SUCCESS) {
        error = std::string("PulseAudio engine init failed: ") + ma_result_description(result);
        ma_context_uninit(&impl_->context);
        impl_->context_ready = false;
        return false;
    }
    impl_->engine_ready = true;

    impl_->normal_pcm = to_stereo_float(make_click(kSampleRate, 880.0F, 45.0F));
    impl_->strong_pcm = to_stereo_float(make_click(kSampleRate, 1320.0F, 55.0F));

    auto make_sound = [&](std::vector<float>& pcm,
                          ma_audio_buffer& buffer,
                          bool& buffer_ready,
                          ma_sound& sound,
                          bool& sound_ready) {
        ma_audio_buffer_config buffer_config = ma_audio_buffer_config_init(
            ma_format_f32, 2, pcm.size() / 2U, pcm.data(), nullptr);
        buffer_config.sampleRate = kSampleRate;
        if (ma_audio_buffer_init(&buffer_config, &buffer) != MA_SUCCESS) {
            return;
        }
        buffer_ready = true;
        if (ma_sound_init_from_data_source(&impl_->engine,
                                           reinterpret_cast<ma_data_source*>(&buffer),
                                           MA_SOUND_FLAG_NO_SPATIALIZATION,
                                           nullptr,
                                           &sound) == MA_SUCCESS) {
            sound_ready = true;
        }
    };

    make_sound(impl_->normal_pcm,
               impl_->normal_buffer,
               impl_->normal_buffer_ready,
               impl_->normal_sound,
               impl_->normal_sound_ready);
    make_sound(impl_->strong_pcm,
               impl_->strong_buffer,
               impl_->strong_buffer_ready,
               impl_->strong_sound,
               impl_->strong_sound_ready);

    if (!impl_->normal_sound_ready && !impl_->strong_sound_ready) {
        error = "PulseAudio sound initialization failed";
        shutdown();
        return false;
    }

    // The first frames after a PulseAudio stream opens can be swallowed while
    // the sink settles (same warmup the official launcher uses).
    impl_->warmup_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    impl_->worker = std::thread(&Impl::run, impl_.get());
    return true;
#endif
}

bool ClickSynth::available() const {
#if USE_DESKTOP
    return impl_->device != 0;
#else
    return impl_->engine_ready;
#endif
}

void ClickSynth::set_volume(int volume) {
    impl_->volume.store(std::clamp(volume, 0, 100), std::memory_order_relaxed);
}

void ClickSynth::play_click(bool accent, bool soft) {
#if USE_DESKTOP
    if (impl_->device == 0) {
        return;
    }
    impl_->accent.store(accent, std::memory_order_relaxed);
    impl_->soft.store(soft, std::memory_order_relaxed);
    impl_->sequence.fetch_add(1, std::memory_order_release);
#else
    if (!impl_->engine_ready) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->pending.emplace(accent, soft);
    }
    impl_->wakeup.notify_one();
#endif
}

void ClickSynth::shutdown() {
#if USE_DESKTOP
    if (impl_->device != 0 && SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_PauseAudioDevice(impl_->device, 1);
        SDL_CloseAudioDevice(impl_->device);
        impl_->device = 0;
    }
    if (impl_->audio_subsystem_owned && SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        impl_->audio_subsystem_owned = false;
    }
#else
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stop = true;
    }
    impl_->wakeup.notify_all();
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }

    if (impl_->strong_sound_ready) {
        ma_sound_uninit(&impl_->strong_sound);
        impl_->strong_sound_ready = false;
    }
    if (impl_->normal_sound_ready) {
        ma_sound_uninit(&impl_->normal_sound);
        impl_->normal_sound_ready = false;
    }
    if (impl_->strong_buffer_ready) {
        ma_audio_buffer_uninit(&impl_->strong_buffer);
        impl_->strong_buffer_ready = false;
    }
    if (impl_->normal_buffer_ready) {
        ma_audio_buffer_uninit(&impl_->normal_buffer);
        impl_->normal_buffer_ready = false;
    }
    if (impl_->engine_ready) {
        ma_engine_uninit(&impl_->engine);
        impl_->engine_ready = false;
    }
    if (impl_->context_ready) {
        ma_context_uninit(&impl_->context);
        impl_->context_ready = false;
    }
#endif
}

} // namespace platform::audio
