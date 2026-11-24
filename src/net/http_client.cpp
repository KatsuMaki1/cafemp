#include "net/http_client.hpp"

#include "logger/logger.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <curl/curl.h>
#include <sys/stat.h>

#ifdef __WIIU__
#define HTTP_CA_BUNDLE_PATH "content:/cacert.pem"
#else
#define HTTP_CA_BUNDLE_PATH "cacert.pem"
#endif

namespace http {

const char *USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/138.0.0.0 Safari/537.36";

static const size_t MAX_BODY_SIZE = 16 * 1024 * 1024;

static int ca_bundle_state = -1; // -1 unknown, 0 missing, 1 present

static bool ca_bundle_available() {
    if (ca_bundle_state < 0) {
        struct stat st;
        ca_bundle_state = (stat(HTTP_CA_BUNDLE_PATH, &st) == 0 && st.st_size > 0) ? 1 : 0;
        if (!ca_bundle_state)
            log_message(LOG_WARNING, "HTTP", "No CA bundle at %s, TLS peer verification disabled", HTTP_CA_BUNDLE_PATH);
    }
    return ca_bundle_state == 1;
}

void global_init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    ca_bundle_available();
}

void global_cleanup() { curl_global_cleanup(); }

std::string Response::header(const std::string &lower_name) const {
    std::string needle = "\n" + lower_name + ":";
    std::string hay = "\n" + raw_headers;
    size_t p = hay.rfind(needle);
    if (p == std::string::npos)
        return "";
    p += needle.size();
    size_t e = hay.find('\n', p);
    std::string v = hay.substr(p, e == std::string::npos ? std::string::npos : e - p);
    while (!v.empty() && (v.back() == '\r' || v.back() == ' ' || v.back() == '\t'))
        v.pop_back();
    size_t s = 0;
    while (s < v.size() && (v[s] == ' ' || v[s] == '\t'))
        s++;
    return v.substr(s);
}

struct TransferCtx {
    Response *resp;
    const DataCallback *on_data;
    const std::atomic<bool> *abort_flag;
    bool callback_aborted = false;
};

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    TransferCtx *ctx = static_cast<TransferCtx *>(userdata);
    size_t len = size * nmemb;
    if (ctx->abort_flag && ctx->abort_flag->load())
        return 0;
    if (ctx->on_data) {
        if (!(*ctx->on_data)(ptr, len)) {
            ctx->callback_aborted = true;
            return 0;
        }
        return len;
    }
    if (ctx->resp->body.size() + len > MAX_BODY_SIZE)
        return 0;
    ctx->resp->body.append(ptr, len);
    return len;
}

static size_t header_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    TransferCtx *ctx = static_cast<TransferCtx *>(userdata);
    size_t len = size * nmemb;
    std::string line(ptr, len);
    // A new status line means a new response (redirects): reset the header block.
    if (line.compare(0, 5, "HTTP/") == 0)
        ctx->resp->raw_headers.clear();
    for (char &c : line) {
        if (c == ':')
            break;
        c = (char)std::tolower((unsigned char)c);
    }
    ctx->resp->raw_headers += line;
    return len;
}

static int xferinfo_cb(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    TransferCtx *ctx = static_cast<TransferCtx *>(userdata);
    return (ctx->abort_flag && ctx->abort_flag->load()) ? 1 : 0;
}

Session::Session() : handle_(curl_easy_init()) {}

Session::~Session() {
    if (handle_)
        curl_easy_cleanup(static_cast<CURL *>(handle_));
}

Response Session::get(const Request &req, const std::atomic<bool> *abort_flag) { return perform(req, nullptr, abort_flag); }

Response Session::stream(const Request &req, const DataCallback &on_data, const std::atomic<bool> *abort_flag) { return perform(req, &on_data, abort_flag); }

Response Session::perform(const Request &req, const DataCallback *on_data, const std::atomic<bool> *abort_flag) {
    Response resp;
    CURL *h = static_cast<CURL *>(handle_);
    if (!h) {
        resp.error = "curl_easy_init failed";
        return resp;
    }

    curl_easy_reset(h); // keeps the connection cache alive

    TransferCtx ctx{&resp, on_data, abort_flag};

    curl_easy_setopt(h, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(h, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, req.follow_redirects ? 1L : 0L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 10L);
    if (req.timeout_s > 0)
        curl_easy_setopt(h, CURLOPT_TIMEOUT, req.timeout_s);
    // Abort stalled transfers (no data for 20 s).
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, on_data ? "identity" : "");
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(h, CURLOPT_HEADERDATA, &ctx);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(h, CURLOPT_BUFFERSIZE, 64L * 1024L);

    if (ca_bundle_available()) {
        curl_easy_setopt(h, CURLOPT_CAINFO, HTTP_CA_BUNDLE_PATH);
    } else {
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    if (!req.range.empty())
        curl_easy_setopt(h, CURLOPT_RANGE, req.range.c_str());
    if (!req.post_body.empty()) {
        curl_easy_setopt(h, CURLOPT_POST, 1L);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, req.post_body.c_str());
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)req.post_body.size());
    }

    struct curl_slist *hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    hdrs = curl_slist_append(hdrs, "Accept-Language: fr-FR,fr;q=0.9,en-US;q=0.8,en;q=0.7");
    if (!req.referer.empty())
        hdrs = curl_slist_append(hdrs, ("Referer: " + req.referer).c_str());
    for (const std::string &line : req.headers)
        hdrs = curl_slist_append(hdrs, line.c_str());
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);

    CURLcode rc = curl_easy_perform(h);

    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    resp.status = status;

    char *s = nullptr;
    if (curl_easy_getinfo(h, CURLINFO_CONTENT_TYPE, &s) == CURLE_OK && s)
        resp.content_type = s;
    s = nullptr;
    if (curl_easy_getinfo(h, CURLINFO_EFFECTIVE_URL, &s) == CURLE_OK && s)
        resp.effective_url = s;
    s = nullptr;
    if (curl_easy_getinfo(h, CURLINFO_REDIRECT_URL, &s) == CURLE_OK && s)
        resp.redirect_url = s;

    if (rc != CURLE_OK && !(rc == CURLE_WRITE_ERROR && ctx.callback_aborted)) {
        resp.error = curl_easy_strerror(rc);
        if (!(abort_flag && abort_flag->load()))
            log_message(LOG_WARNING, "HTTP", "GET %s failed: %s", req.url.c_str(), resp.error.c_str());
    }

    curl_slist_free_all(hdrs);
    return resp;
}

Response get(const Request &req) {
    Session s;
    return s.get(req);
}

std::string url_encode(const std::string &s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

bool is_bot_wall(const Response &r) {
    if (r.status != 403 && r.status != 503 && r.status != 429)
        return false;
    const std::string &b = r.body;
    return b.find("Just a moment") != std::string::npos || b.find("cf-browser-verification") != std::string::npos || b.find("challenge-platform") != std::string::npos ||
           !r.header("cf-mitigated").empty();
}

} // namespace http

