#include "youtube/youtube.hpp"

#include "logger/logger.hpp"
#include "net/http_client.hpp"

#include <jansson.h>

namespace youtube {

static const char *YT = "YouTube";
static const char *INNERTUBE_BASE = "https://www.youtube.com/youtubei/v1/";

static std::string jstr(json_t *obj, const char *key) {
    if (!obj || !json_is_object(obj))
        return "";
    json_t *v = json_object_get(obj, key);
    return json_is_string(v) ? json_string_value(v) : "";
}

static std::string yt_text(json_t *obj) {
    if (!obj || !json_is_object(obj))
        return "";
    std::string s = jstr(obj, "simpleText");
    if (!s.empty())
        return s;
    s = jstr(obj, "content");
    if (!s.empty())
        return s;
    json_t *runs = json_object_get(obj, "runs");
    if (json_is_array(runs)) {
        std::string out;
        size_t i;
        json_t *el;
        json_array_foreach(runs, i, el) { out += jstr(el, "text"); }
        return out;
    }
    return "";
}

static json_t *post_innertube(const char *endpoint, const char *client_name, const char *client_ver, const char *ua, const char *extra_os, json_t *body, std::string &err) {
    json_t *client = json_object();
    json_object_set_new(client, "clientName", json_string(client_name));
    json_object_set_new(client, "clientVersion", json_string(client_ver));
    json_object_set_new(client, "hl", json_string("fr"));
    json_object_set_new(client, "gl", json_string("FR"));
    if (extra_os && *extra_os) {
        json_object_set_new(client, "deviceMake", json_string("Apple"));
        json_object_set_new(client, "deviceModel", json_string("RealityDevice17,1"));
        json_object_set_new(client, "osName", json_string("visionOS"));
        json_object_set_new(client, "osVersion", json_string("26.5.23O471"));
        json_object_set_new(client, "userAgent", json_string(ua));
    }

    json_t *ctx = json_object();
    json_object_set_new(ctx, "client", client);
    json_object_set_new(body, "context", ctx);

    char *dumped = json_dumps(body, JSON_COMPACT);
    std::string payload = dumped ? dumped : "{}";
    if (dumped)
        free(dumped);
    json_decref(body);

    http::Request req;
    req.url = std::string(INNERTUBE_BASE) + endpoint + "?prettyPrint=false";
    req.post_body = payload;
    req.timeout_s = 12;
    req.headers.push_back("Content-Type: application/json");
    req.headers.push_back(std::string("User-Agent: ") + ua);

    http::Response r = http::get(req);
    if (!r.ok()) {
        err = r.error.empty() ? ("YouTube HTTP " + std::to_string(r.status)) : r.error;
        return nullptr;
    }

    json_error_t jerr;
    json_t *root = json_loads(r.body.c_str(), 0, &jerr);
    if (!root) {
        err = "Reponse JSON YouTube invalide";
        return nullptr;
    }
    return root;
}

static void collect_videos(json_t *node, std::vector<VideoResult> &out) {
    if (!node || out.size() >= 24)
        return;
    if (json_is_array(node)) {
        size_t i;
        json_t *el;
        json_array_foreach(node, i, el) { collect_videos(el, out); }
        return;
    }
    if (!json_is_object(node))
        return;

    json_t *vr = json_object_get(node, "videoRenderer");
    if (vr && json_is_object(vr)) {
        std::string id = jstr(vr, "videoId");
        std::string title = yt_text(json_object_get(vr, "title"));
        if (!id.empty() && !title.empty()) {
            VideoResult v;
            v.id = id;
            v.title = title;
            v.channel = yt_text(json_object_get(vr, "ownerText"));
            if (v.channel.empty())
                v.channel = yt_text(json_object_get(vr, "shortBylineText"));
            v.duration = yt_text(json_object_get(vr, "lengthText"));
            v.views = yt_text(json_object_get(vr, "shortViewCountText"));
            v.thumb = "https://i.ytimg.com/vi/" + id + "/mqdefault.jpg";
            out.push_back(std::move(v));
        }
        return;
    }

    const char *key;
    json_t *val;
    json_object_foreach(node, key, val) {
        if (json_is_object(val) || json_is_array(val))
            collect_videos(val, out);
    }
}

std::vector<VideoResult> search(const std::string &query, std::string &err) {
    std::vector<VideoResult> out;
    if (query.empty()) {
        err = "Recherche vide";
        return out;
    }

    json_t *body = json_object();
    json_object_set_new(body, "query", json_string(query.c_str()));

    json_t *root = post_innertube("search", "WEB", "2.20250922.01.00",
                                  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36", nullptr, body, err);
    if (!root)
        return out;

    collect_videos(root, out);
    json_decref(root);
    log_message(LOG_OK, YT, "search('%s') -> %d video(s)", query.c_str(), (int)out.size());
    return out;
}

bool resolve_stream(const std::string &video_id, std::string &out_url, std::string &err) {
    json_t *body = json_object();
    json_object_set_new(body, "videoId", json_string(video_id.c_str()));
    json_object_set_new(body, "contentCheckOk", json_true());
    json_object_set_new(body, "racyCheckOk", json_true());

    json_t *root = post_innertube("player", "VISIONOS", "1.02",
                                  "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15", "visionOS", body, err);
    if (!root)
        return false;

    json_t *ps = json_object_get(root, "playabilityStatus");
    std::string status = jstr(ps, "status");
    if (status != "OK") {
        err = jstr(ps, "reason");
        if (err.empty())
            err = "Video indisponible (" + status + ")";
        json_decref(root);
        return false;
    }

    json_t *sd = json_object_get(root, "streamingData");
    std::string hls = jstr(sd, "hlsManifestUrl");
    if (!hls.empty()) {
        out_url = hls;
        json_decref(root);
        log_message(LOG_OK, YT, "%s -> HLS stream", video_id.c_str());
        return true;
    }

    // Fallback to highest-height progressive H.264 format
    json_t *formats = json_object_get(sd, "formats");
    std::string best_url;
    int best_h = -1;
    if (json_is_array(formats)) {
        size_t i;
        json_t *f;
        json_array_foreach(formats, i, f) {
            std::string mime = jstr(f, "mimeType");
            std::string url = jstr(f, "url");
            int h = (int)json_integer_value(json_object_get(f, "height"));
            if (!url.empty() && mime.find("avc1") != std::string::npos && h > best_h) {
                best_h = h;
                best_url = url;
            }
        }
    }

    json_decref(root);
    if (!best_url.empty()) {
        out_url = best_url;
        log_message(LOG_OK, YT, "%s -> progressive %dp", video_id.c_str(), best_h);
        return true;
    }

    err = "Aucun flux H.264 compatible trouve";
    return false;
}

} // namespace youtube

