#ifndef NET_HTTP_CLIENT_HPP
#define NET_HTTP_CLIENT_HPP

// Small libcurl wrapper used by the Anime-Sama scraper and the network stream reader.

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace http {

extern const char *USER_AGENT;

struct Request {
    std::string url;
    std::string referer;
    std::vector<std::string> headers; // raw "Name: value" lines
    std::string range;                // e.g. "0-1023" (without "bytes=")
    bool follow_redirects = true;
    long timeout_s = 15;              // total timeout, 0 = none (streams use a low-speed limit instead)
    std::string post_body;            // non-empty -> HTTP POST
};

struct Response {
    long status = 0;
    std::string body;
    std::string content_type;
    std::string effective_url;
    std::string redirect_url;
    std::string raw_headers; // lower-cased header block of the last response
    std::string error;       // non-empty on transport error

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
    std::string header(const std::string &lower_name) const;
};

// Return false from the callback to abort the transfer.
using DataCallback = std::function<bool(const char *data, size_t len)>;

// A Session keeps one curl handle so connections (and TLS sessions) are reused.
// A Session must only be used from one thread at a time.
class Session {
  public:
    Session();
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    Response get(const Request &req, const std::atomic<bool> *abort_flag = nullptr);
    Response stream(const Request &req, const DataCallback &on_data, const std::atomic<bool> *abort_flag = nullptr);

  private:
    void *handle_;
    Response perform(const Request &req, const DataCallback *on_data, const std::atomic<bool> *abort_flag);
};

void global_init();
void global_cleanup();

// Convenience one-shot GET.
Response get(const Request &req);

std::string url_encode(const std::string &s);

// True when the body looks like a Cloudflare / anti-bot interstitial.
bool is_bot_wall(const Response &r);

} // namespace http

#endif

