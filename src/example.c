#include "libuvc/libuvc.h"
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

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
    if (streaming) {
        try {
            const char *title = "libuvc USB Preview | R: rotate | Q / Esc: quit";
            cv::namedWindow(title, cv::WINDOW_NORMAL);
            cv::resizeWindow(title, 1280, 720);
            int turns = portrait ? 1 : 0;
            bool first = true;
            cv::Mat current;
            auto lastFrame = std::chrono::steady_clock::now();
            while (true) {
                cv::Mat frame;
                {
                    std::lock_guard<std::mutex> lock(frame_mutex);
                    std::swap(frame, latest);
                }
                if (!frame.empty()) {
                    current = frame;
                    lastFrame = std::chrono::steady_clock::now();
                    if (first) {
                        std::printf("UVC preview active: %dx%d\n", frame.cols, frame.rows);
                        std::fflush(stdout);
                        first = false;
                        result = 0;
                    }
                }
                if (!current.empty()) {
                    cv::Mat display;
                    if (turns) cv::rotate(current, display, turns == 1 ? cv::ROTATE_90_CLOCKWISE : turns == 2 ? cv::ROTATE_180 : cv::ROTATE_90_COUNTERCLOCKWISE);
                    else display = current;
                    cv::imshow(title, display);
                }
                int key = cv::waitKey(10);
                if (key == 'r' || key == 'R') turns = (turns + 1) % 4;
                if (key == 27 || key == 'q' || cv::getWindowProperty(title, cv::WND_PROP_VISIBLE) < 1) break;
                if (std::chrono::steady_clock::now() - lastFrame > std::chrono::seconds(10)) {
                    std::fprintf(stderr, "No decoded UVC frames for 10 seconds\n");
                    result = 1;
                    break;
                }
            }
            cv::destroyAllWindows();
        } catch (const cv::Exception &e) {
            std::fprintf(stderr, "%s\n", e.what());
            result = 1;
        }
        uvc_stop_streaming(handle);
        std::printf("Received %u decoded UVC frames\n", received.load());
    }
    if (handle) uvc_close(handle);
    if (dev) uvc_unref_device(dev);
    if (ctx) uvc_exit(ctx);
    return result;
}
