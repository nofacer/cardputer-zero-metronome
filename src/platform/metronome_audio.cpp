/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "metronome_audio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#if USE_DESKTOP
#include <SDL.h>
#elif defined(APP_USE_ALSA) && APP_USE_ALSA
#include <alsa/asoundlib.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
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
#elif defined(APP_USE_ALSA) && APP_USE_ALSA
    snd_pcm_t* pcm{nullptr};
    std::vector<int16_t> normal_click;
    std::vector<int16_t> strong_click;
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

            const auto& samples = accent_click ? strong_click : normal_click;
            int current_volume = volume.load(std::memory_order_relaxed);
            if (soft_click) {
                current_volume = current_volume * 45 / 100;
            }
            std::vector<int16_t> framed(samples.size());
            std::transform(samples.begin(), samples.end(), framed.begin(), [&](int16_t sample) {
                return scale_sample(sample, current_volume);
            });

            const snd_pcm_sframes_t written =
                snd_pcm_writei(pcm, framed.data(), static_cast<snd_pcm_uframes_t>(framed.size()));
            if (written == -EPIPE) {
                snd_pcm_prepare(pcm);
                snd_pcm_writei(pcm, framed.data(), static_cast<snd_pcm_uframes_t>(framed.size()));
            }
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
#elif defined(APP_USE_ALSA) && APP_USE_ALSA
    if (impl_->pcm) {
        return true;
    }

    const int result = snd_pcm_open(&impl_->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (result < 0 || !impl_->pcm) {
        error = std::string("ALSA open failed: ") + snd_strerror(result);
        impl_->pcm = nullptr;
        return false;
    }

    const int params_result = snd_pcm_set_params(impl_->pcm,
                                                 SND_PCM_FORMAT_S16_LE,
                                                 SND_PCM_ACCESS_RW_INTERLEAVED,
                                                 1,
                                                 kSampleRate,
                                                 1,
                                                 50000);
    if (params_result < 0) {
        error = std::string("ALSA setup failed: ") + snd_strerror(params_result);
        snd_pcm_close(impl_->pcm);
        impl_->pcm = nullptr;
        return false;
    }

    impl_->normal_click = make_click(kSampleRate, 880.0F, 45.0F);
    impl_->strong_click = make_click(kSampleRate, 1320.0F, 55.0F);
    impl_->worker = std::thread(&Impl::run, impl_.get());
    return true;
#else
    error = "audio output is not enabled in this build";
    return false;
#endif
}

bool ClickSynth::available() const {
#if USE_DESKTOP
    return impl_->device != 0;
#elif defined(APP_USE_ALSA) && APP_USE_ALSA
    return impl_->pcm != nullptr;
#else
    return false;
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
#elif defined(APP_USE_ALSA) && APP_USE_ALSA
    if (!impl_->pcm) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->pending.emplace(accent, soft);
    }
    impl_->wakeup.notify_one();
#else
    (void)accent;
    (void)soft;
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
#elif defined(APP_USE_ALSA) && APP_USE_ALSA
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stop = true;
    }
    impl_->wakeup.notify_all();
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    if (impl_->pcm) {
        snd_pcm_close(impl_->pcm);
        impl_->pcm = nullptr;
    }
#endif
}

} // namespace platform::audio
