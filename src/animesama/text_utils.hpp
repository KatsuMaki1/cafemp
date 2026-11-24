#ifndef ANIMESAMA_TEXT_HPP
#define ANIMESAMA_TEXT_HPP

// Minimal string / HTML helpers (no regex: std::regex is slow and stack hungry on the Wii U).

#include <string>
#include <vector>

namespace txt {

std::string trim(const std::string &s);
std::string lower(std::string s);
bool starts_with(const std::string &s, const std::string &prefix);
bool ends_with(const std::string &s, const std::string &suffix);
bool contains_ci(const std::string &hay, const std::string &needle);
void replace_all(std::string &s, const std::string &from, const std::string &to);

// Text between `a` and `b` starting at `from`. Returns false if not found.
bool between(const std::string &s, const std::string &a, const std::string &b, std::string &out, size_t from = 0, size_t *end_pos = nullptr);

// Opening tag (from '<' to '>') that contains position `pos`.
std::string enclosing_tag(const std::string &html, size_t pos);
// First opening tag whose text contains `needle` (e.g. `id="synopsisText"`), starting at `from`.
bool find_tag(const std::string &html, const std::string &needle, size_t from, std::string &tag, size_t *tag_end = nullptr);
// Attribute value of a single opening tag, HTML-decoded. Empty if missing.
std::string attr(const std::string &tag, const std::string &name);
// Inner text of the element whose opening tag ends at `tag_end` (tags stripped, entities decoded, whitespace collapsed).
std::string inner_text(const std::string &html, size_t tag_end, const std::string &tag_name);

std::string strip_tags(const std::string &s);
std::string html_decode(const std::string &s);
std::string collapse_ws(const std::string &s);
std::string strip_js_comments(const std::string &s);

// Lowercase, accents folded, only [a-z0-9 ] kept, whitespace collapsed (port of clean_string()).
std::string clean_query(const std::string &s);

// URL helpers
std::string url_origin(const std::string &url); // scheme://host[:port]
std::string url_host(const std::string &url);   // host without "www."
std::string url_resolve(const std::string &base, const std::string &ref);
std::string rstrip_char(std::string s, char c);
std::string lstrip_char(const std::string &s, char c);

// Quoted string literals ('...' or "...") inside a JS snippet.
std::vector<std::string> js_string_literals(const std::string &js);

} // namespace txt

#endif

