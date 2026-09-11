#include "audio_ring.h"
#include "engine.h"
#include "segmenter.h"

#include <jni.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;

static jint attach_current_thread(JavaVM* vm, JNIEnv** env) {
#if defined(__ANDROID__)
    return vm->AttachCurrentThread(env, nullptr);
#else
    return vm->AttachCurrentThread(reinterpret_cast<void**>(env), nullptr);
#endif
}

class StreamingVad {
    SpeechVad& vad_;
    Segmenter segmenter_;
    std::vector<float> history_ = std::vector<float>(12800, 0.0f);
    std::vector<float> pending_;

public:
    explicit StreamingVad(SpeechVad& vad) : vad_(vad) {}

    std::vector<Segment> accept(const float* samples, size_t count) {
        pending_.insert(pending_.end(), samples, samples + count);
        std::vector<Segment> events;
        constexpr size_t block = 2560;
        constexpr size_t lookahead = 640;
        while (pending_.size() >= block + lookahead) {
            auto context = history_;
            context.insert(context.end(), pending_.begin(), pending_.begin() + block + lookahead);
            auto probabilities = vad_.probabilities(context);
            for (size_t i = 0; i < block / 160; ++i) {
                const size_t position = history_.size() / 160 + i;
                if (position >= probabilities.size()) throw std::runtime_error("VAD frame alignment error");
                if (auto segment = segmenter_.feed(pending_.data() + i * 160, probabilities[position])) {
                    events.push_back(std::move(*segment));
                }
            }
            history_.insert(history_.end(), pending_.begin(), pending_.begin() + block);
            history_.erase(history_.begin(), history_.end() - 12800);
            pending_.erase(pending_.begin(), pending_.begin() + block);
        }
        return events;
    }
};

class NativeSession {
    JavaVM* vm_;
    jobject owner_;
    jmethodID ready_method_;
    jmethodID subtitle_method_;
    jmethodID status_method_;
    jmethodID error_method_;
    std::string model_path_;
    std::string vad_path_;
    int threads_;
    AudioRing<16000 * 10> audio_;
    // AudioRecord delivers the same-sized blocks repeatedly. Keep this buffer
    // alive between JNI calls so the capture path does not allocate on every read.
    std::vector<float> converted_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> inference_busy_{false};
    std::condition_variable audio_cv_;
    std::mutex audio_mutex_;
    std::thread worker_;

public:
    NativeSession(JNIEnv* env, jobject owner, std::string model, std::string vad, int threads)
            : owner_(env->NewGlobalRef(owner)), model_path_(std::move(model)),
              vad_path_(std::move(vad)), threads_(std::clamp(threads, 1, 16)) {
        if (env->GetJavaVM(&vm_) != JNI_OK || !owner_) throw std::runtime_error("JNI initialization failed");
        jclass type = env->GetObjectClass(owner);
        ready_method_ = env->GetMethodID(type, "onNativeReady", "()V");
        subtitle_method_ = env->GetMethodID(type, "onNativeSubtitle", "(JZLjava/lang/String;DDD)V");
        status_method_ = env->GetMethodID(type, "onNativeStatus", "(Ljava/lang/String;)V");
        error_method_ = env->GetMethodID(type, "onNativeError", "(Ljava/lang/String;)V");
        env->DeleteLocalRef(type);
        if (!ready_method_ || !subtitle_method_ || !status_method_ || !error_method_) {
            env->DeleteGlobalRef(owner_);
            owner_ = nullptr;
            throw std::runtime_error("JNI callback methods are missing");
        }
        worker_ = std::thread([this] { run(); });
    }

    ~NativeSession() {
        stop_.store(true, std::memory_order_release);
        audio_cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        JNIEnv* env = nullptr;
        bool attached = false;
        if (vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            if (attach_current_thread(vm_, &env) == JNI_OK) {
                attached = true;
            }
        }
        if (env && owner_) env->DeleteGlobalRef(owner_);
        if (attached) vm_->DetachCurrentThread();
    }

    bool push(const int16_t* samples, size_t count) {
        converted_.resize(count);
        for (size_t i = 0; i < count; ++i) {
            converted_[i] = static_cast<float>(samples[i]) / 32768.0f;
        }
        const bool accepted = audio_.push(converted_.data(), converted_.size());
        if (accepted) audio_cv_.notify_one();
        return accepted;
    }

private:
    void run() {
        JNIEnv* env = nullptr;
        if (attach_current_thread(vm_, &env) != JNI_OK) return;
        try {
            callback_text(env, status_method_, "正在加载 SenseVoice 与 FSMN-VAD");
            SenseVoice engine(model_path_, threads_);
            SpeechVad vad(vad_path_, 2);
            StreamingVad stream(vad);
            std::mutex queue_mutex;
            std::condition_variable queue_cv;
            SegmentQueue jobs;
            bool finished = false;

            std::thread inference([&] {
                JNIEnv* inference_env = nullptr;
                if (attach_current_thread(vm_, &inference_env) != JNI_OK) {
                    return;
                }
                try {
                    std::unordered_map<uint64_t, std::string> last_text;
                    while (!stop_.load(std::memory_order_acquire)) {
                        std::optional<Segment> job;
                        {
                            std::unique_lock lock(queue_mutex);
                            queue_cv.wait_for(lock, std::chrono::milliseconds(50),
                                              [&] { return finished || !jobs.empty() || stop_.load(); });
                            if (finished || stop_.load()) break;
                            job = jobs.pop();
                        }
                        if (!job) continue;
                        inference_busy_.store(true, std::memory_order_release);
                        const auto before = Clock::now();
                        // A partial is a preview. Re-running the complete growing
                        // utterance every second makes the preview cost grow with
                        // sentence length. The final event still uses all samples.
                        const std::vector<float>* input = &job->audio;
                        std::vector<float> partial_audio;
                        constexpr size_t max_partial_samples = 16000 * 6;
                        if (!job->final && job->audio.size() > max_partial_samples) {
                            partial_audio.assign(job->audio.end() - max_partial_samples,
                                                 job->audio.end());
                            input = &partial_audio;
                        }
                        auto text = engine.transcribe(*input);
                        const double compute_ms = std::chrono::duration<double, std::milli>(
                                Clock::now() - before).count();
                        inference_busy_.store(false, std::memory_order_release);
                        if (stop_.load()) break;
                        if (!text.empty()) last_text[job->id] = text;
                        else if (job->final) {
                            auto previous = last_text.find(job->id);
                            if (previous != last_text.end()) text = previous->second;
                        }
                        if (!text.empty()) {
                            callback_subtitle(inference_env, *job, text, compute_ms);
                            if (job->final) last_text.erase(job->id);
                        }
                    }
                } catch (const std::exception& error) {
                    callback_text(inference_env, error_method_, error.what());
                    stop_.store(true);
                }
                vm_->DetachCurrentThread();
            });

            env->CallVoidMethod(owner_, ready_method_);
            clear_java_exception(env);
            callback_text(env, status_method_, "本地识别器已就绪");

            std::array<float, 2560> buffer{};
            auto last_audio = Clock::now();
            while (!stop_.load(std::memory_order_acquire)) {
                size_t count = audio_.pop(buffer.data(), buffer.size());
                if (count) {
                    last_audio = Clock::now();
                } else if (Clock::now() - last_audio > std::chrono::milliseconds(200)) {
                    buffer.fill(0.0f);
                    count = buffer.size();
                    last_audio = Clock::now();
                }

                if (count) {
                    for (auto& event : stream.accept(buffer.data(), count)) {
                        // Once an inference call is running, a preview that is
                        // already obsolete by the time it is consumed only adds
                        // CPU load. Finals are always retained.
                        if (!event.final && inference_busy_.load(std::memory_order_acquire)) {
                            continue;
                        }
                        std::lock_guard lock(queue_mutex);
                        jobs.push(std::move(event));
                        queue_cv.notify_one();
                    }
                } else {
                    std::unique_lock lock(audio_mutex_);
                    audio_cv_.wait_for(lock, std::chrono::milliseconds(5),
                                       [&] { return stop_.load() || !audio_.empty(); });
                }
            }
            {
                std::lock_guard lock(queue_mutex);
                finished = true;
            }
            queue_cv.notify_all();
            if (inference.joinable()) inference.join();
        } catch (const std::exception& error) {
            callback_text(env, error_method_, error.what());
        }
        vm_->DetachCurrentThread();
    }

    void callback_subtitle(JNIEnv* env, const Segment& segment, const std::string& text,
                           double compute_ms) {
        jstring value = env->NewStringUTF(text.c_str());
        env->CallVoidMethod(owner_, subtitle_method_, static_cast<jlong>(segment.id),
                            static_cast<jboolean>(segment.final), value,
                            segment.start, segment.end, compute_ms);
        env->DeleteLocalRef(value);
        clear_java_exception(env);
    }

    void callback_text(JNIEnv* env, jmethodID method, const std::string& text) {
        jstring value = env->NewStringUTF(text.c_str());
        env->CallVoidMethod(owner_, method, value);
        env->DeleteLocalRef(value);
        clear_java_exception(env);
    }

    static void clear_java_exception(JNIEnv* env) {
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
};

static std::string from_java(JNIEnv* env, jstring value) {
    const char* utf8 = env->GetStringUTFChars(value, nullptr);
    if (!utf8) throw std::runtime_error("Cannot read Java string");
    std::string result(utf8);
    env->ReleaseStringUTFChars(value, utf8);
    return result;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_wso4133560_funasrsubtitle_NativeEngine_nativeCreate(
        JNIEnv* env, jobject owner, jstring model, jstring vad, jint threads) {
    try {
        auto session = std::make_unique<NativeSession>(
                env, owner, from_java(env, model), from_java(env, vad), threads);
        return reinterpret_cast<jlong>(session.release());
    } catch (...) {
        return 0;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_wso4133560_funasrsubtitle_NativeEngine_nativePushPcm16(
        JNIEnv* env, jobject, jlong handle, jshortArray samples, jint count) {
    auto* session = reinterpret_cast<NativeSession*>(handle);
    if (!session || !samples || count <= 0 || count > env->GetArrayLength(samples)) return JNI_FALSE;
    std::vector<int16_t> copy(static_cast<size_t>(count));
    env->GetShortArrayRegion(samples, 0, count, reinterpret_cast<jshort*>(copy.data()));
    if (env->ExceptionCheck()) return JNI_FALSE;
    return session->push(copy.data(), copy.size()) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wso4133560_funasrsubtitle_NativeEngine_nativeDestroy(
        JNIEnv*, jobject, jlong handle) {
    delete reinterpret_cast<NativeSession*>(handle);
}
