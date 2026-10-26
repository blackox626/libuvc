#ifndef LIBUVC_MICROPHONE_RECORDER_HPP
#define LIBUVC_MICROPHONE_RECORDER_HPP
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <cerrno>
#include <pwd.h>
extern char **environ;

class MicrophoneRecorder {
    pid_t child = -1;
    std::string audioPath, logPath, progressPath;
    double audioLead = 0;
    static pid_t launch(const std::vector<std::string> &args, const std::string &log) {
        std::vector<std::string> command = args;
#ifdef __APPLE__
        // Root is needed by libusb, but microphone consent belongs to the
        // logged-in desktop user. Spawn audio tools with that user's identity.
        if (geteuid() == 0) {
            struct stat console;
            if (stat("/dev/console", &console) != 0 || console.st_uid == 0)
                throw std::runtime_error("No logged-in desktop user for microphone access");
            struct passwd *user = getpwuid(console.st_uid);
            if (!user) throw std::runtime_error("Cannot resolve desktop user");
            command.insert(command.begin(), {"/usr/bin/sudo", "-n", "-u", user->pw_name, "--"});
        }
#endif
        std::vector<char *> argv;
        for (const auto &s : command) argv.push_back(const_cast<char *>(s.c_str()));
        argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
        pid_t pid;
        int error = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (error) throw std::runtime_error("Cannot launch FFmpeg. Install ffmpeg or set LIBUVC_FFMPEG.");
        return pid;
    }
    static std::string ffmpeg() {
        const char *custom = std::getenv("LIBUVC_FFMPEG");
        if (custom && *custom) return custom;
        return access("/opt/homebrew/bin/ffmpeg", X_OK) == 0 ? "/opt/homebrew/bin/ffmpeg" : "ffmpeg";
    }
public:
    ~MicrophoneRecorder() { stop(); }
    void start(const std::string &base) {
#ifndef __APPLE__
        throw std::runtime_error("Microphone recording currently requires macOS AVFoundation.");
#endif
        audioPath = base + ".audio.wav";
        logPath = base + ".ffmpeg.log";
        progressPath = base + ".progress";
        const char *device = std::getenv("LIBUVC_AUDIO_DEVICE");
        const std::string input = std::string(":") + (device ? device : "HD CAMERA");
        child = launch({ffmpeg(), "-hide_banner", "-loglevel", "error", "-nostdin", "-n",
            "-f", "avfoundation", "-i", input, "-vn", "-c:a", "pcm_s16le",
            "-flush_packets", "1", "-stats_period", "0.05", "-progress", progressPath, audioPath}, logPath);
        // Do not report recording as active until actual microphone data arrives.
        for (int i = 0; i < 100; ++i) {
            int status;
            if (waitpid(child, &status, WNOHANG) == child) {
                child = -1;
                throw std::runtime_error("Microphone failed; check permission and " + logPath);
            }
            std::ifstream progress(progressPath);
            std::string line;
            long long micros = 0;
            while (std::getline(progress, line))
                if (line.compare(0, 12, "out_time_us=") == 0)
                    micros = std::strtoll(line.c_str() + 12, nullptr, 10);
            if (micros > 0) {
                audioLead = micros / 1000000.0;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        stop();
        throw std::runtime_error("No microphone samples; allow microphone access, then retry. See " + logPath);
    }
    void stop() noexcept {
        if (child <= 0) return;
        kill(child, SIGINT);
        for (int i = 0; i < 100; ++i) {
            if (waitpid(child, nullptr, WNOHANG) != 0) { child = -1; return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        child = -1;
    }
    bool running() {
        if (child <= 0) return false;
        if (waitpid(child, nullptr, WNOHANG) == 0) return true;
        child = -1;
        return false;
    }
    void mux(const std::string &video, const std::string &output) {
        stop();
        pid_t pid = launch({ffmpeg(), "-hide_banner", "-loglevel", "error", "-nostdin", "-n",
            "-i", video, "-ss", std::to_string(audioLead), "-i", audioPath, "-map", "0:v:0", "-map", "1:a:0",
            "-c:v", "copy", "-c:a", "aac", "-b:a", "96k", "-shortest",
            "-movflags", "+faststart", output}, logPath);
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw std::runtime_error("Audio/video merge failed; originals retained: " + video + ", " + audioPath + "; see " + logPath);
        std::remove(video.c_str());
        std::remove(audioPath.c_str());
        std::remove(progressPath.c_str());
    }
};
#endif
