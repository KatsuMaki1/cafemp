#ifndef ANIMESAMA_HPP
#define ANIMESAMA_HPP

// Native C++ port of AnimeSamaApi (https://github.com/TMCooper/AnimeSamaApi, GPL-3.0)
// Scrapes anime-sama directly from the console: no PC / Python server required.

#include "net/http_client.hpp"

#include <string>
#include <vector>

namespace animesama {

enum class Lang { VOSTFR, VF };
const char *lang_name(Lang l);

struct SearchResult {
    std::string title;
    std::string url; // canonical catalogue url, e.g. https://anime-sama.xx/catalogue/frieren
    std::string cover;
};

struct Season {
    std::string name; // "Saison 1", "Film", "OAV"...
    std::string url;  // e.g. .../catalogue/frieren/saison1/vostfr
};

struct AnimeInfo {
    std::string title;
    std::string url;
    std::string synopsis;
    std::vector<std::string> alt_titles;
    std::vector<Season> seasons;
};

struct Episode {
    int number = 0;
    std::vector<std::string> sources; // raw player entries (urls or "lien:<id>"), best player first
};

struct EpisodeList {
    std::string page_url; // season page actually used (with language)
    std::string jeton;    // epsJeton, needed to resolve "lien:" entries
    std::vector<Episode> episodes;
};

struct Stream {
    std::string url;
    std::string type; // "m3u8", "mp4", "embed", "raw"
    std::string referer;
};

// Discovers the currently active anime-sama domain (cached after the first success).
bool find_base_url(std::string &out, std::string &err);
void set_base_url(const std::string &url);

std::vector<SearchResult> search(const std::string &query, std::string &err);
bool get_info(const SearchResult &anime, AnimeInfo &out, std::string &err);
bool get_episodes(const Season &season, Lang lang, EpisodeList &out, std::string &err);
// Tries every player of the episode until a playable direct stream (mp4 / m3u8) is found.
bool resolve_episode(const EpisodeList &list, const Episode &ep, Stream &out, std::string &err);

// --- resolvers.cpp ---
bool resolve_video_url(http::Session &s, const std::string &url, Stream &out);
// 0 = verified healthy, 1 = probably fine (slow / unknown), >= 999 = dead.
int check_stream_health(http::Session &s, const Stream &stream);
std::string unpack_packer(const std::string &html); // P.A.C.K.E.R. decoder, empty if not found

} // namespace animesama

#endif

