// FFmpegUtils.cpp

#include "FFmpegUtils.h"
#include <cstdio>

namespace ffmpeg {

std::string av_err_str(int errnum) {
    char buf[256];
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

} // namespace ffmpeg
