#pragma once
#include <memory>
#include <string>
#include <vector>

class SenseVoice {
public:
    using AbortCallback = bool (*)(void*);
    SenseVoice(const std::string& path, int threads);
    ~SenseVoice();
    std::string transcribe(const std::vector<float>& samples);
    std::string transcribe(const std::vector<float>& samples,
                           AbortCallback abort_callback, void* abort_data);
    SenseVoice(const SenseVoice&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class SpeechVad {
public:
    SpeechVad(const std::string& path, int threads);
    ~SpeechVad();
    // One speech probability per 10 ms, using the native FSMN graph.
    std::vector<float> probabilities(const std::vector<float>& samples);
    SpeechVad(const SpeechVad&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
