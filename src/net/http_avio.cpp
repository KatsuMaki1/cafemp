#include "net/http_avio.hpp"

#include "logger/logger.hpp"
#include "net/http_client.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <sys/stat.h>
#include <vector>

#ifdef __WIIU__
#define HTTP_CA_BUNDLE_PATH "content:/cacert.pem"
#else
#define HTTP_CA_BUNDLE_PATH "cacert.pem"
#endif

namespace http {

static const char *HA = "HTTP/AVIO";
static const int AVIO_BUF_SIZE = 32 * 1024;

struct CurlStream {
    std::string url;
    std::string referer;
    const std::atomic<bool> *abort_flag = nullptr;

    CURLM *multi = nullptr;
    CURL *easy = nullptr;
    struct curl_slist *hdrs = nullptr;

    std::vector<uint8_t> buf;
    size_t buf_pos = 0;

    int64_t pos = 0;
    int64_t size = -1;
    bool eof = false;
    bool headers_done = false;
    long status = 0;
    int still_running = 0;

    ~CurlStream() { close_transfer(); }

    void close_transfer() {
        if (multi && easy) {
            curl_multi_remove_handle(multi, easy);
        }
        if (easy) {
            curl_easy_cleanup(easy);
            easy = nullptr;
        }
        if (multi) {
            curl_multi_cleanup(multi);
            multi = nullptr;
        }
        if (hdrs) {
            curl_slist_free_all(hdrs);
            hdrs = nullptr;
        }
        buf.clear();
        buf_pos = 0;
        still_running = 0;
    }

    static size_t on_write(char *ptr, size_t sz, size_t nmemb, void *ud) {
        CurlStream *cs = static_cast<CurlStream *>(ud);
        if (cs->abort_flag && cs->abort_flag->load())
            return 0;
        size_t len = sz * nmemb;
        cs->buf.insert(cs->buf.end(), reinterpret_cast<uint8_t *>(ptr), reinterpret_cast<uint8_t *>(ptr) + len);
        return len;
    }

    static size_t on_header(char *ptr, size_t sz, size_t nmemb, void *ud) {
        CurlStream *cs = static_cast<CurlStream *>(ud);
        size_t len = sz * nmemb;
        std::string line(ptr, len);
        std::string low = line;
        for (char &c : low)
            c = (char)std::tolower((unsigned char)c);

        if (low.compare(0, 5, "http/") == 0) {
            cs->headers_done = false;
            cs->size = -1;
        } else if (low.compare(0, 14, "content-range:") == 0) {
            size_t slash = line.find('/');
            if (slash != std::string::npos) {
                int64_t total = strtoll(line.c_str() + slash + 1, nullptr, 10);
                if (total > 0)
                    cs->size = total;
            }
        } else if (low.compare(0, 15, "content-length:") == 0 && cs->size < 0) {
            int64_t cl = strtoll(line.c_str() + 15, nullptr, 10);
            if (cl > 0)
                cs->size = cs->pos + cl;
        } else if (line == "\r\n" || line == "\n") {
            cs->headers_done = true;
            if (cs->easy)
                curl_easy_getinfo(cs->easy, CURLINFO_RESPONSE_CODE, &cs->status);
        }
        return len;
    }

    bool open_at(int64_t offset) {
        close_transfer();
        pos = offset;
        eof = false;
        headers_done = false;
        status = 0;

        multi = curl_multi_init();
        easy = curl_easy_init();
        if (!multi || !easy) {
            close_transfer();
            return false;
        }

        curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy, CURLOPT_USERAGENT, USER_AGENT);
        curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 8L);
        curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 20L);
        curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "identity");
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, on_write);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, on_header);
        curl_easy_setopt(easy, CURLOPT_HEADERDATA, this);
        curl_easy_setopt(easy, CURLOPT_BUFFERSIZE, 64L * 1024L);

        struct stat st;
        if (stat(HTTP_CA_BUNDLE_PATH, &st) == 0 && st.st_size > 0) {
            curl_easy_setopt(easy, CURLOPT_CAINFO, HTTP_CA_BUNDLE_PATH);
        } else {
            curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
        }

        if (offset > 0) {
            char range[64];
            snprintf(range, sizeof(range), "%lld-", (long long)offset);
            curl_easy_setopt(easy, CURLOPT_RANGE, range);
        }

        if (!referer.empty())
            hdrs = curl_slist_append(hdrs, ("Referer: " + referer).c_str());
        if (hdrs)
            curl_easy_setopt(easy, CURLOPT_HTTPHEADER, hdrs);

        curl_multi_add_handle(multi, easy);

        // Pump until response headers are received (or error).
        while (!headers_done) {
            if (abort_flag && abort_flag->load())
                return false;
            CURLMcode mc = curl_multi_perform(multi, &still_running);
            if (mc != CURLM_OK)
                return false;
            if (!still_running)
                break;
            if (!headers_done)
                curl_multi_wait(multi, nullptr, 0, 50, nullptr);
        }

        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
        if (status >= 400 || status == 0) {
            log_message(LOG_WARNING, HA, "HTTP %ld for %s", status, url.c_str());
            return false;
        }
        return true;
    }

    int read_bytes(uint8_t *dst, int dst_size) {
        if (abort_flag && abort_flag->load())
            return AVERROR_EXIT;

        while (buf_pos >= buf.size()) {
            buf.clear();
            buf_pos = 0;

            if (!still_running) {
                eof = true;
                return AVERROR_EOF;
            }
            if (abort_flag && abort_flag->load())
                return AVERROR_EXIT;

            CURLMcode mc = curl_multi_perform(multi, &still_running);
            if (mc != CURLM_OK)
                return AVERROR(EIO);
            if (buf.empty() && still_running)
                curl_multi_wait(multi, nullptr, 0, 50, nullptr);
        }

        size_t avail = buf.size() - buf_pos;
        size_t n = std::min(avail, static_cast<size_t>(dst_size));
        memcpy(dst, buf.data() + buf_pos, n);
        buf_pos += n;
        pos += (int64_t)n;

        // Compact buffer when fully consumed.
        if (buf_pos >= buf.size()) {
            buf.clear();
            buf_pos = 0;
        }
        return (int)n;
    }

    int64_t seek_bytes(int64_t offset, int whence) {
        if (whence == AVSEEK_SIZE)
            return size;

        int64_t target = offset;
        if (whence == SEEK_CUR)
            target = pos + offset;
        else if (whence == SEEK_END) {
            if (size < 0)
                return AVERROR(ENOSYS);
            target = size + offset;
        } else if (whence != SEEK_SET) {
            return AVERROR(EINVAL);
        }

        if (target < 0)
            return AVERROR(EINVAL);
        if (target == pos)
            return pos;

        // Fast path: target is already inside our unread buffer.
        int64_t buffered_avail = (int64_t)(buf.size() - buf_pos);
        if (target > pos && target - pos <= buffered_avail) {
            buf_pos += (size_t)(target - pos);
            pos = target;
            return pos;
        }

        if (!open_at(target))
            return AVERROR(EIO);
        return pos;
    }
};

static int avio_read_cb(void *opaque, uint8_t *buf, int buf_size) { return static_cast<CurlStream *>(opaque)->read_bytes(buf, buf_size); }

static int64_t avio_seek_cb(void *opaque, int64_t offset, int whence) { return static_cast<CurlStream *>(opaque)->seek_bytes(offset, whence); }

struct IoContextData {
    std::string referer;
    const std::atomic<bool> *abort_flag;
};

static AVIOContext *open_curl_avio(const std::string &url, const std::string &referer, const std::atomic<bool> *abort_flag) {
    CurlStream *cs = new CurlStream();
    cs->url = url;
    cs->referer = referer;
    cs->abort_flag = abort_flag;
    if (!cs->open_at(0)) {
        delete cs;
        return nullptr;
    }

    uint8_t *io_buf = static_cast<uint8_t *>(av_malloc(AVIO_BUF_SIZE));
    if (!io_buf) {
        delete cs;
        return nullptr;
    }

    AVIOContext *pb = avio_alloc_context(io_buf, AVIO_BUF_SIZE, 0, cs, avio_read_cb, nullptr, avio_seek_cb);
    if (!pb) {
        av_free(io_buf);
        delete cs;
        return nullptr;
    }
    pb->seekable = (cs->size > 0) ? AVIO_SEEKABLE_NORMAL : 0;
    return pb;
}

static int custom_io_open(AVFormatContext *s, AVIOContext **pb, const char *url, int, AVDictionary **) {
    IoContextData *data = static_cast<IoContextData *>(s->opaque);
    AVIOContext *ctx = open_curl_avio(url, data ? data->referer : "", data ? data->abort_flag : nullptr);
    if (!ctx)
        return AVERROR(EIO);
    *pb = ctx;
    return 0;
}

static void custom_io_close(AVFormatContext *, AVIOContext *pb) {
    if (!pb)
        return;
    CurlStream *cs = static_cast<CurlStream *>(pb->opaque);
    delete cs;
    av_freep(&pb->buffer);
    avio_context_free(&pb);
}

bool avio_attach(AVFormatContext *fmt_ctx, const std::string &url, const std::string &referer, const std::atomic<bool> *abort_flag) {
    if (!fmt_ctx)
        return false;

    IoContextData *data = new IoContextData{referer, abort_flag};
    fmt_ctx->opaque = data;
    fmt_ctx->io_open = custom_io_open;
    fmt_ctx->io_close = custom_io_close;
    fmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

    AVIOContext *pb = open_curl_avio(url, referer, abort_flag);
    if (!pb) {
        delete data;
        fmt_ctx->opaque = nullptr;
        fmt_ctx->io_open = nullptr;
        fmt_ctx->io_close = nullptr;
        fmt_ctx->flags &= ~AVFMT_FLAG_CUSTOM_IO;
        return false;
    }
    fmt_ctx->pb = pb;
    return true;
}

void avio_close_input(AVFormatContext **fmt_ctx_ptr) {
    if (!fmt_ctx_ptr || !*fmt_ctx_ptr)
        return;
    AVFormatContext *ctx = *fmt_ctx_ptr;
    bool custom = (ctx->flags & AVFMT_FLAG_CUSTOM_IO) != 0;
    AVIOContext *pb = custom ? ctx->pb : nullptr;
    IoContextData *data = custom ? static_cast<IoContextData *>(ctx->opaque) : nullptr;

    avformat_close_input(fmt_ctx_ptr);

    if (custom) {
        if (pb)
            custom_io_close(nullptr, pb);
        delete data;
    }
}

} // namespace http
