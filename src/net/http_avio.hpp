#ifndef NET_HTTP_AVIO_HPP
#define NET_HTTP_AVIO_HPP

// Custom FFmpeg AVIOContext backed by libcurl (curl_multi).
// Allows FFmpeg to stream HTTP/HTTPS (MP4 and HLS playlists/segments) with custom
// Referer / User-Agent headers even when FFmpeg itself was built without network/TLS.

extern "C" {
#include <libavformat/avformat.h>
}

#include <atomic>
#include <string>

namespace http {

// Attaches our custom io_open / io_close2 callbacks to `fmt_ctx` and opens `url`
// as `fmt_ctx->pb` (setting AVFMT_FLAG_CUSTOM_IO).
// `abort_flag` is polled during reads so media_player_cleanup() can unblock immediately.
bool avio_attach(AVFormatContext *fmt_ctx, const std::string &url, const std::string &referer, const std::atomic<bool> *abort_flag);

// Closes `fmt_ctx` (via avformat_close_input) and frees any custom IO attached by avio_attach().
void avio_close_input(AVFormatContext **fmt_ctx);

} // namespace http

#endif
