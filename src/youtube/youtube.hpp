#ifndef YOUTUBE_HPP
#define YOUTUBE_HPP

// Lightweight YouTube InnerTube client for CaféMP.

#include <string>
#include <vector>

namespace youtube {

struct VideoResult {
    std::string id;
    std::string title;
    std::string channel;
    std::string duration;
    std::string views;
    std::string thumb;
};

std::vector<VideoResult> search(const std::string &query, std::string &err);
bool resolve_stream(const std::string &video_id, std::string &out_url, std::string &err);

} // namespace youtube

#endif

