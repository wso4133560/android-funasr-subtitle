// Links against the APK's actual native library, so baseline and optimized
// measurements exercise the shipped kernels rather than a separate ASR build.
#include "engine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>

using Timer = std::chrono::steady_clock;
static double elapsed(Timer::time_point start) {
    return std::chrono::duration<double, std::milli>(Timer::now() - start).count();
}
struct AbortAfter {
    Timer::time_point started;
    double milliseconds;
};
static bool abort_after(void* data) {
    const auto* request = static_cast<const AbortAfter*>(data);
    return elapsed(request->started) >= request->milliseconds;
}
static void check_vad(const std::vector<float>& actual, const std::vector<float>* expected = nullptr) {
    if(actual.empty()) throw std::runtime_error("VAD returned no frames");
    if(expected && actual.size()!=expected->size())
        throw std::runtime_error("VAD frame count changed after reuse");
    for(size_t i=0;i<actual.size();++i){
        if(!std::isfinite(actual[i]) || actual[i]<0.0f || actual[i]>1.0f)
            throw std::runtime_error("VAD returned an invalid probability");
        if(expected && std::abs(actual[i]-(*expected)[i])>1e-6f)
            throw std::runtime_error("VAD probabilities changed after reuse");
    }
}
static uint32_t u32(std::istream& in) {
    unsigned char b[4]{};
    if (!in.read(reinterpret_cast<char*>(b), 4)) throw std::runtime_error("Truncated WAV");
    return b[0] | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}
static std::vector<float> read_wav(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (u32(in) != 0x46464952) throw std::runtime_error("Expected RIFF");
    u32(in);
    if (u32(in) != 0x45564157) throw std::runtime_error("Expected WAVE");
    bool format_ok = false;
    while (in.peek() != EOF) {
        const auto tag = u32(in), size = u32(in);
        const auto next = in.tellg() + std::streamoff(size + (size & 1));
        if (tag == 0x20746d66 && size >= 16) {
            const auto format_channels = u32(in), rate = u32(in);
            u32(in);
            const auto block_bits = u32(in);
            format_ok = format_channels == 0x00010001 && rate == 16000 && block_bits == 0x00100002;
        } else if (tag == 0x61746164) {
            if (!format_ok || size % 2 || size > 16000 * 30 * 2) {
                throw std::runtime_error("Expected <=30s mono 16kHz PCM16 WAV");
            }
            std::vector<int16_t> pcm(size / 2);
            if (!in.read(reinterpret_cast<char*>(pcm.data()), size)) throw std::runtime_error("Truncated PCM");
            std::vector<float> samples(pcm.size());
            for (size_t i = 0; i < pcm.size(); ++i) samples[i] = pcm[i] / 32768.0f;
            return samples;
        }
        in.seekg(next);
    }
    throw std::runtime_error("Missing WAV data");
}

int main(int argc, char** argv) {
    if (argc < 6 || argc > 9) {
        std::fprintf(stderr, "Usage: device_benchmark MODEL VAD WAV THREADS REPEATS [ABORT_AFTER_MS] [BACKEND] [--check-lifecycle]\n");
        return 2;
    }
    try {
        const int threads = std::stoi(argv[4]), repeats = std::stoi(argv[5]);
        if (threads < 1 || threads > 16 || repeats < 1 || repeats > 100) return 2;
        auto samples = read_wav(argv[3]);
        auto start = Timer::now();
        bool has_abort = false;
        bool check_lifecycle = false;
        double abort_ms = 0;
        std::string backend = "cpu";
        for(int i=6;i<argc;++i){
            const std::string arg=argv[i];
            if(arg=="--check-lifecycle") check_lifecycle=true;
            else if(arg=="cpu" || arg=="vulkan") backend=arg;
            else {
                size_t consumed=0;
                abort_ms=std::stod(arg,&consumed);
                if(has_abort || consumed!=arg.size() || abort_ms<0 || abort_ms>60000)
                    throw std::runtime_error("Invalid abort argument");
                has_abort=true;
            }
        }
        if(has_abort && backend!="cpu")
            throw std::runtime_error("In-flight cancellation is only supported by CPU");
        SenseVoice asr(argv[1], threads, backend);
        SpeechVad vad(argv[2], 2);
        std::printf("load_ms\t%.3f\n", elapsed(start));
        std::printf("iteration\taudio_s\tthreads\tasr_ms\trtf\tvad_1s_ms\ttext\n");
        std::fflush(stdout);
        std::vector<float> vad_audio(samples.begin(), samples.begin() + std::min<size_t>(16000, samples.size()));
        if (has_abort) {
            AbortAfter request{Timer::now(), abort_ms};
            start = Timer::now();
            bool did_abort = false;
            auto aborted = asr.transcribe(samples, abort_after, &request, &did_abort);
            std::printf("abort_test_ms\t%.3f\tempty=%d\n", elapsed(start), aborted.empty());
            if (!aborted.empty() || !did_abort) throw std::runtime_error("Abort test did not abort");
            // A cancelled graph must not poison the next graph on the same backend.
        }
        std::string first_result;
        std::vector<float> first_vad;
        for (int i = 0; i < repeats; ++i) {
            start = Timer::now();
            auto result = asr.transcribe(samples);
            if(i==0) first_result=result;
            else if(result!=first_result) throw std::runtime_error("Repeated transcription changed");
            const double asr_ms = elapsed(start);
            start = Timer::now();
            auto probabilities=vad.probabilities(vad_audio);
            const double vad_ms = elapsed(start), seconds = samples.size() / 16000.0;
            check_vad(probabilities,i==0 ? nullptr : &first_vad);
            if(i==0) first_vad=std::move(probabilities);
            for (char& ch : result) if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
            std::printf("%d\t%.3f\t%d\t%.3f\t%.4f\t%.3f\t%s\n",
                        i, seconds, threads, asr_ms, asr_ms / (seconds * 1000), vad_ms, result.c_str());
            std::fflush(stdout);
        }
        if(check_lifecycle){
            // Exercise resizing both ways on the same instance, then prove the
            // original utterance survives buffer reuse and an earlier abort.
            for(size_t seconds : {1u,3u,8u,2u}){
                std::vector<float> window(samples.begin(),samples.begin()+std::min(samples.size(),seconds*16000));
                asr.transcribe(window);
                check_vad(vad.probabilities(window));
            }
            if(asr.transcribe(samples)!=first_result)
                throw std::runtime_error("Audio window resizing changed the full transcription");
            check_vad(vad.probabilities(vad_audio),&first_vad);
            std::printf("lifecycle_check\tpassed\n");
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
