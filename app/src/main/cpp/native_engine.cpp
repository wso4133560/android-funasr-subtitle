#include "audio_ring.h"
#include "engine.h"
#include "segmenter.h"

#include <jni.h>
#include <android/log.h>

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

struct PreviewAbort {
    const std::atomic<uint64_t>* final_epoch;
    uint64_t expected_epoch;
};

static bool abort_preview_if_final_waiting(void* data) {
    const auto* request = static_cast<const PreviewAbort*>(data);
    return request->final_epoch->load(std::memory_order_acquire) != request->expected_epoch;
}

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
    std::atomic<size_t> capture_drops_{0};
    std::atomic<uint64_t> final_epoch_{0};
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
        else capture_drops_.fetch_add(1, std::memory_order_relaxed);
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
                        size_t final_drops = 0;
                        uint64_t preview_epoch = 0;
                        {
                            std::unique_lock lock(queue_mutex);
                            queue_cv.wait_for(lock, std::chrono::milliseconds(50),
                                              [&] { return finished || !jobs.empty() || stop_.load(); });
                            if (finished || stop_.load()) break;
                            job = jobs.pop();
                            final_drops = jobs.drops;
                            if (!job) continue;
                            // Capture the task's generation while holding the
                            // same lock used to enqueue finals. A final that is
                            // queued immediately after pop is then still seen
                            // by the abort callback through final_epoch_.
                            preview_epoch = job->final_epoch;
                        }
                        const auto before = Clock::now();
                        const double queue_ms = std::chrono::duration<double, std::milli>(
                                before - job->queued_at).count();
                        // Optimized kernels can process the full context. Keep the
                        // sentence prefix in previews as well as final results.
                        PreviewAbort abort_request{&final_epoch_, preview_epoch};
                        bool inference_aborted = false;
                        auto text = job->final
                                ? engine.transcribe(job->audio)
                                : engine.transcribe(job->audio, abort_preview_if_final_waiting,
                                                    &abort_request, &inference_aborted);
                        const double compute_ms = std::chrono::duration<double, std::milli>(
                                Clock::now() - before).count();
                        const bool cancelled = !job->final && inference_aborted;
                        __android_log_print(ANDROID_LOG_INFO, "FunASRPerf",
                                "asr id=%llu final=%d cancelled=%d audio_ms=%.2f queue_ms=%.2f compute_ms=%.2f capture_drops=%zu final_drops=%zu",
                                static_cast<unsigned long long>(job->id), job->final,
                                cancelled, job->audio.size() / 16.0, queue_ms, compute_ms,
                                capture_drops_.load(std::memory_order_relaxed), final_drops);
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
                        std::lock_guard lock(queue_mutex);
                        event.queued_at = Clock::now();
                        if (event.final) {
                            event.final_epoch = final_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
                        } else {
                            event.final_epoch = final_epoch_.load(std::memory_order_acquire);
                        }
                        // SegmentQueue retains only the latest waiting preview and
                        // prioritizes finals; no extra delay until the next preview.
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
