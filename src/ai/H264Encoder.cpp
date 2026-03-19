// H264Encoder.cpp
// Uses FFmpeg subprocess via fork/pipe:
//   Parent writes raw BGR frames to FFmpeg stdin,
//   a reader thread consumes H.264 Annex-B data from FFmpeg stdout,
//   delivering one NAL unit per callback invocation.

#include "H264Encoder.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <vector>

namespace ai {

// ─────────────────────────────────────────────────────────────────────────────
struct H264Encoder::Impl {
    int           write_fd  = -1;  // parent writes BGR frames here
    int           read_fd   = -1;  // parent reads H.264 NAL here
    pid_t         child_pid = -1;
    std::thread   reader_thread;
    std::atomic<bool> running{false};
    FrameCallback callback;

    // Called on child exit / close
    void StopReader() {
        running = false;
        if (read_fd >= 0) {
            ::close(read_fd);
            read_fd = -1;
        }
        if (reader_thread.joinable()) reader_thread.join();
    }

    void StartReader(FrameCallback cb) {
        callback = std::move(cb);
        running  = true;
        reader_thread = std::thread([this]() { ReadLoop(); });
    }

    void ReadLoop() {
        std::vector<uint8_t> buf(1 << 20);
        std::vector<uint8_t> nal;
        nal.reserve(1 << 17);
        bool have_start = false;

        while (running && read_fd >= 0) {
            ssize_t n = ::read(read_fd, buf.data(), buf.size());
            if (n <= 0) break;
            ParseAndDeliver(buf.data(), (size_t)n, nal, have_start);
        }
        // Flush last NAL
        if (have_start && !nal.empty()) DeliverNAL(nal);
    }

    void ParseAndDeliver(const uint8_t* data, size_t len,
                         std::vector<uint8_t>& nal, bool& have_start) {
        size_t i = 0;
        while (i < len) {
            bool found4 = (i + 4 <= len &&
                           data[i]==0 && data[i+1]==0 &&
                           data[i+2]==0 && data[i+3]==1);
            bool found3 = (!found4 && i + 3 <= len &&
                           data[i]==0 && data[i+1]==0 && data[i+2]==1);
            if (found4 || found3) {
                if (have_start && !nal.empty()) DeliverNAL(nal);
                nal.clear();
                have_start = true;
                i += found4 ? 4 : 3;
                continue;
            }
            if (have_start) nal.push_back(data[i]);
            ++i;
        }
    }

    void DeliverNAL(const std::vector<uint8_t>& nal) {
        if (nal.empty() || !callback) return;
        static const uint8_t kStart[4] = {0, 0, 0, 1};
        std::vector<uint8_t> pkt;
        pkt.reserve(4 + nal.size());
        pkt.insert(pkt.end(), kStart, kStart + 4);
        pkt.insert(pkt.end(), nal.begin(), nal.end());
        uint8_t nal_type = nal[0] & 0x1F;
        bool key = (nal_type == 5 || nal_type == 7 || nal_type == 8);
        callback(pkt.data(), pkt.size(), key);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
H264Encoder::H264Encoder(int width, int height, double fps, int bitrate)
    : width_(width), height_(height), fps_(fps), bitrate_(bitrate)
    , impl_(new Impl())
{}

H264Encoder::~H264Encoder() {
    Close();
}

void H264Encoder::BuildCommand() {
    // Not used with fork/exec path; kept for documentation.
    std::ostringstream cmd;
    cmd << "ffmpeg -loglevel error"
        << " -f rawvideo -pixel_format bgr24"
        << " -video_size " << width_ << "x" << height_
        << " -framerate " << (int)fps_
        << " -i pipe:0"
        << " -vcodec libx264 -preset ultrafast -tune zerolatency"
        << " -b:v " << bitrate_
        << " -g " << (int)fps_
        << " -f h264 pipe:1";
    pipe_cmd_ = cmd.str();
}

bool H264Encoder::Open() {
    if (opened_) return true;
    BuildCommand();

    // Two pipes: parent→child (stdin) and child→parent (stdout)
    int stdin_pipe[2], stdout_pipe[2];
    if (::pipe(stdin_pipe) != 0 || ::pipe(stdout_pipe) != 0) {
        std::cerr << "[H264Encoder] pipe() failed." << std::endl;
        return false;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        std::cerr << "[H264Encoder] fork() failed." << std::endl;
        return false;
    }

    if (pid == 0) {
        // ── Child ──────────────────────────────────────────────────────────
        ::dup2(stdin_pipe[0],  STDIN_FILENO);
        ::dup2(stdout_pipe[1], STDOUT_FILENO);
        ::close(stdin_pipe[0]);  ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]); ::close(stdout_pipe[1]);

        std::string size_str  = std::to_string(width_) + "x" + std::to_string(height_);
        std::string fps_str   = std::to_string((int)fps_);
        std::string bv_str    = std::to_string(bitrate_);
        std::string gop_str   = fps_str;

        ::execlp("ffmpeg", "ffmpeg",
                 "-loglevel", "error",
                 "-f", "rawvideo",
                 "-pixel_format", "bgr24",
                 "-video_size", size_str.c_str(),
                 "-framerate", fps_str.c_str(),
                 "-i", "pipe:0",
                 "-vcodec", "libx264",
                 "-preset", "ultrafast",
                 "-tune", "zerolatency",
                 "-b:v", bv_str.c_str(),
                 "-g", gop_str.c_str(),
                 "-f", "h264",
                 "pipe:1",
                 (char*)nullptr);
        // execlp failed
        std::cerr << "[H264Encoder] execlp failed." << std::endl;
        ::_exit(1);
    }

    // ── Parent ───────────────────────────────────────────────────────────────
    ::close(stdin_pipe[0]);   // parent doesn't read from child stdin
    ::close(stdout_pipe[1]);  // parent doesn't write to child stdout

    impl_->write_fd  = stdin_pipe[1];
    impl_->read_fd   = stdout_pipe[0];
    impl_->child_pid = pid;

    impl_->StartReader(callback_);

    opened_ = true;
    return true;
}

void H264Encoder::Close() {
    if (!opened_) return;
    opened_ = false;

    if (impl_->write_fd >= 0) {
        ::close(impl_->write_fd);
        impl_->write_fd = -1;
    }
    impl_->StopReader();

    if (impl_->child_pid > 0) {
        ::waitpid(impl_->child_pid, nullptr, 0);
        impl_->child_pid = -1;
    }
}

bool H264Encoder::EncodeFrame(const cv::Mat& frame) {
    if (!opened_ || impl_->write_fd < 0) return false;
    if (frame.cols != width_ || frame.rows != height_ ||
        frame.type() != CV_8UC3) {
        std::cerr << "[H264Encoder] Frame mismatch: expected "
                  << width_ << "x" << height_ << " BGR, got "
                  << frame.cols << "x" << frame.rows
                  << " type=" << frame.type() << std::endl;
        return false;
    }

    const size_t row_bytes = (size_t)width_ * 3;
    for (int r = 0; r < frame.rows; ++r) {
        const uint8_t* row = frame.ptr<uint8_t>(r);
        ssize_t total = (ssize_t)row_bytes;
        while (total > 0) {
            ssize_t n = ::write(impl_->write_fd, row, (size_t)total);
            if (n <= 0) {
                std::cerr << "[H264Encoder] write error." << std::endl;
                return false;
            }
            row   += n;
            total -= n;
        }
    }
    return true;
}

} // namespace ai
