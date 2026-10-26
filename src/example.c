#include "libuvc/libuvc.h"
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <limits.h>
#include "mp4-recorder.hpp"
#ifdef __APPLE__
int systemCameraIndex();
#endif

static std::mutex frame_mutex;
static cv::Mat latest;
static std::atomic<unsigned> received(0);
static void cb(uvc_frame_t *frame, void *) {
    uvc_frame_t *bgr = uvc_allocate_frame(frame->width * frame->height * 3);
    if (!bgr) return;
    const bool mjpeg = frame->frame_format == UVC_FRAME_FORMAT_MJPEG;
    uvc_error_t decoded = mjpeg ? uvc_any2rgb(frame, bgr) : uvc_any2bgr(frame, bgr);
    if (decoded == UVC_SUCCESS) {
        cv::Mat view(bgr->height, bgr->width, CV_8UC3, bgr->data,
                     bgr->step ? bgr->step : bgr->width * 3);
        if (mjpeg) cv::cvtColor(view, view, cv::COLOR_RGB2BGR);
        std::lock_guard<std::mutex> lock(frame_mutex);
        latest = view.clone();
        ++received;
    }
    uvc_free_frame(bgr);
}

int main(int argc, char **argv) {
#ifdef __APPLE__
    // Finder launches bundles with an unrelated working directory.
    char executable[PATH_MAX];
    uint32_t executableSize = sizeof(executable);
    if (_NSGetExecutablePath(executable, &executableSize) == 0) {
        std::string location(executable);
        const std::string suffix = "/build/UVCExample.app/Contents/MacOS/example";
        if (location.size() >= suffix.size() && location.compare(location.size() - suffix.size(), suffix.size(), suffix) == 0) {
            chdir(location.substr(0, location.size() - suffix.size()).c_str());
            std::freopen("build/system-av-run.log", "a", stdout);
            std::freopen("build/system-av-run.log", "a", stderr);
            setvbuf(stdout, nullptr, _IOLBF, 0);
        }
    }
#endif
    const bool systemAV = argc > 1 && std::strcmp(argv[1], "--system-av") == 0;
    double recordSeconds = 0;
    if (systemAV && argc > 2) {
        if (argc != 4 || std::strcmp(argv[2], "--record-seconds") != 0 ||
            (recordSeconds = std::atof(argv[3])) <= 0 || recordSeconds > 3600) {
            std::fprintf(stderr, "Usage: example --system-av [--record-seconds 1..3600]\n");
            return 1;
        }
    }
    cv::VideoCapture systemCamera;
    double recordingFps = 30;
    // Optional USB vendor/product IDs in hexadecimal; otherwise first UVC device.
    int vid = argc > 1 ? std::strtol(argv[1], nullptr, 16) : 0;
    int pid = argc > 2 ? std::strtol(argv[2], nullptr, 16) : 0;
    uvc_context_t *ctx = nullptr;
    uvc_device_t *dev = nullptr;
    uvc_device_handle_t *handle = nullptr;
    uvc_stream_ctrl_t ctrl = {};
    bool streaming = false;
    bool portrait = false;
    int result = 1;
    if (systemAV) {
#ifdef __APPLE__
        if (geteuid() == 0) {
            std::fprintf(stderr, "Run --system-av without sudo to use camera/microphone permissions.\n");
            return 1;
        }
        const int index = systemCameraIndex();
        if (index < 0 || !systemCamera.open(index, cv::CAP_AVFOUNDATION)) return 1;
        systemCamera.set(cv::CAP_PROP_FRAME_WIDTH, 1920);
        systemCamera.set(cv::CAP_PROP_FRAME_HEIGHT, 1080);
        systemCamera.set(cv::CAP_PROP_FPS, 30);
        portrait = systemCamera.get(cv::CAP_PROP_FRAME_HEIGHT) > systemCamera.get(cv::CAP_PROP_FRAME_WIDTH);
        streaming = true;
#else
        std::fprintf(stderr, "--system-av currently requires macOS.\n");
        return 1;
#endif
    } else {
    uvc_error_t err = uvc_init(&ctx, nullptr);
    if (err == UVC_SUCCESS) err = uvc_find_device(ctx, &dev, vid, pid, nullptr);
    if (err == UVC_SUCCESS) err = uvc_open(dev, &handle);
    if (err < 0) {
        uvc_perror(err, "Open UVC camera");
        if (err == UVC_ERROR_ACCESS)
            std::fprintf(stderr, "macOS USB capture requires root: sudo ./build/example [VID PID]\n");
    } else {
        uvc_print_diag(handle, stderr);
        // Support both landscape and portrait full-HD camera configurations.
        err = uvc_get_stream_ctrl_format_size(handle, &ctrl,
            UVC_FRAME_FORMAT_MJPEG, 1920, 1080, 30);
        if (err == UVC_ERROR_INVALID_MODE) {
            portrait = true;
            err = uvc_get_stream_ctrl_format_size(handle, &ctrl,
                UVC_FRAME_FORMAT_MJPEG, 1080, 1920, 30);
        }
        if (err == UVC_SUCCESS) {
            std::printf("Selected UVC mode: MJPEG %dx%d @ 30 fps; display 1920x1080\n",
                        portrait ? 1080 : 1920, portrait ? 1920 : 1080);
            uvc_print_stream_ctrl(&ctrl, stderr);
        }
        if (err == UVC_SUCCESS) err = uvc_start_streaming(handle, &ctrl, cb, nullptr, 0);
        streaming = err == UVC_SUCCESS;
        if (!streaming) uvc_perror(err, "Start UVC stream");
    }
    if (streaming) recordingFps = 10000000.0 / ctrl.dwFrameInterval;
    }
    if (streaming) {
        try {
            const char *title = systemAV
                ? "USB System AV | S: record with camera microphone | R: rotate | Q: quit"
                : "libuvc USB Preview | S: silent MP4 | R: rotate | Q: quit";
            Mp4Recorder recorder;
            cv::Mat display;
            cv::namedWindow(title, cv::WINDOW_NORMAL);
            cv::resizeWindow(title, 1280, 720);
            int turns = portrait ? 1 : 0;
            bool first = true;
            cv::Mat current;
            bool autoStarted = false;
            auto autoStart = std::chrono::steady_clock::now();
            auto lastFrame = std::chrono::steady_clock::now();
            while (true) {
                cv::Mat frame;
                if (systemAV) {
                    if (!systemCamera.read(frame)) {
                        std::fprintf(stderr, "System camera frame read failed\n");
                        result = 1;
                        break;
                    }
                    ++received;
                } else {
                    std::lock_guard<std::mutex> lock(frame_mutex);
                    std::swap(frame, latest);
                }
                if (!frame.empty()) {
                    current = frame;
                    lastFrame = std::chrono::steady_clock::now();
                    if (first) {
                        std::printf("%s preview active: %dx%d\n", systemAV ? "System AV" : "UVC", frame.cols, frame.rows);
                        std::fflush(stdout);
                        first = false;
                        result = 0;
                    }
                }
                if (!current.empty()) {
                    if (turns) cv::rotate(current, display, turns == 1 ? cv::ROTATE_90_CLOCKWISE : turns == 2 ? cv::ROTATE_180 : cv::ROTATE_90_COUNTERCLOCKWISE);
                    else display = current;
                    try { recorder.write(display); }
                    catch (const std::exception &e) {
                        std::fprintf(stderr, "Recording failed: %s\n", e.what());
                        try { recorder.stop(); }
                        catch (const std::exception &finalizeError) {
                            std::fprintf(stderr, "%s\n", finalizeError.what());
                        }
                        cv::setWindowTitle(title, title);
                    }
                    cv::imshow(title, display);
                }
                int key = cv::waitKey(10);
                if (recordSeconds > 0 && !autoStarted && !display.empty()) key = 's';
                if (key == 's' || key == 'S') {
                    if (recorder.active()) {
                        try {
                            recorder.stop();
                            std::printf("Saved MP4: %s\n", recorder.path.c_str());
                        } catch (const std::exception &e) {
                            std::fprintf(stderr, "Cannot finalize recording: %s\n", e.what());
                        }
                        cv::setWindowTitle(title, title);
                    } else if (!display.empty()) {
                        try {
                            recorder.start(display.size(), recordingFps, systemAV);
                            if (recordSeconds > 0) {
                                autoStarted = true;
                                autoStart = std::chrono::steady_clock::now();
                            }
                            recorder.write(display);
                            cv::setWindowTitle(title, "REC | S: stop | Q: save and quit | " + recorder.path);
                            std::printf("Recording MP4: %s\n", recorder.path.c_str());
                        } catch (const std::exception &e) {
                            try { recorder.stop(); } catch (...) {}
                            std::fprintf(stderr, "Cannot record: %s\n", e.what());
                            if (recordSeconds > 0) { result = 1; break; }
                        }
                    }
                    std::fflush(stdout);
                }
                if (autoStarted && std::chrono::duration<double>(std::chrono::steady_clock::now() - autoStart).count() >= recordSeconds) break;
                // Keep the encoded dimensions and orientation constant per file.
                if ((key == 'r' || key == 'R') && !recorder.active()) turns = (turns + 1) % 4;
                if (key == 27 || key == 'q' || cv::getWindowProperty(title, cv::WND_PROP_VISIBLE) < 1) break;
                if (std::chrono::steady_clock::now() - lastFrame > std::chrono::seconds(10)) {
                    std::fprintf(stderr, "No decoded UVC frames for 10 seconds\n");
                    result = 1;
                    break;
                }
            }
            if (recorder.active()) {
                recorder.stop();
                std::printf("Saved MP4: %s\n", recorder.path.c_str());
            }
            cv::destroyAllWindows();
        } catch (const std::exception &e) {
            std::fprintf(stderr, "%s\n", e.what());
            result = 1;
        }
        if (handle) uvc_stop_streaming(handle);
        systemCamera.release();
        std::printf("Received %u %s frames\n", received.load(), systemAV ? "system camera" : "decoded UVC");
    }
    if (handle) uvc_close(handle);
    if (dev) uvc_unref_device(dev);
    if (ctx) uvc_exit(ctx);
    return result;
}
