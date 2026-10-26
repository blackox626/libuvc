#ifndef LIBUVC_MP4_RECORDER_HPP
#define LIBUVC_MP4_RECORDER_HPP
#include <opencv2/videoio.hpp>
#include <chrono>
#include <string>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include "microphone-recorder.hpp"

// Constant-rate output follows elapsed time: repeat the latest frame when
// capture is slower, rather than making the recorded video play too fast.
class Mp4Recorder {
    cv::VideoWriter writer;
    MicrophoneRecorder microphone;
    std::string videoPath;
    std::chrono::steady_clock::time_point startTime;
    double fps = 30;
    bool withAudio = false;
    long long written = 0;
public:
    std::string path;
    ~Mp4Recorder() {
        try { stop(); }
        catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); }
    }
    bool active() const { return writer.isOpened(); }
    void start(const cv::Size &size, double rate, bool audio = true) {
        if (mkdir("recordings", 0755) != 0 && errno != EEXIST)
            throw std::runtime_error(std::string("Cannot create recordings: ") + std::strerror(errno));
#ifdef __APPLE__
        if (geteuid() == 0) {
            struct stat console;
            if (stat("/dev/console", &console) == 0 && console.st_uid != 0)
                chown("recordings", console.st_uid, console.st_gid);
        }
#endif
        const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        path = "recordings/camera-" + std::to_string(stamp) + ".mp4";
        withAudio = audio;
        videoPath = withAudio ? path + ".video.mp4" : path;
        fps = rate;
#ifdef __APPLE__
        const int backend = cv::CAP_AVFOUNDATION;
#else
        const int backend = cv::CAP_ANY;
#endif
        if (!writer.open(videoPath, backend, cv::VideoWriter::fourcc('a','v','c','1'), fps, size))
            throw std::runtime_error("Cannot open H.264 MP4 encoder: " + path);
        try { if (withAudio) microphone.start(path); }
        catch (...) { writer.release(); throw; }
        startTime = std::chrono::steady_clock::now();
        written = 0;
    }
    void write(const cv::Mat &frame) {
        if (!active()) return;
        if (withAudio && !microphone.running()) throw std::runtime_error("Microphone capture stopped unexpectedly");
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - startTime).count();
        const auto target = static_cast<long long>(seconds * fps) + 1;
        while (written < target) { writer.write(frame); ++written; }
    }
    void stop() {
        if (!active()) return;
        writer.release();
        if (withAudio) microphone.mux(videoPath, path);
    }
};
#endif
