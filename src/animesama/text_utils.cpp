#include "animesama/text_utils.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace txt {

std::string trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ')
        b++;
    while (e > b && (unsigned char)s[e - 1] <= ' ')
        e--;
    // Also strip UTF-8 non-breaking spaces (C2 A0).
    while (e - b >= 2 && (unsigned char)s[b] == 0xC2 && (unsigned char)s[b + 1] == 0xA0)
        b += 2;
    while (e - b >= 2 && (unsigned char)s[e - 2] == 0xC2 && (unsigned char)s[e - 1] == 0xA0)
        e -= 2;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

bool starts_with(const std::string &s, const std::string &prefix) { return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0; }

bool ends_with(const std::string &s, const std::string &suffix) { return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0; }

bool contains_ci(const std::string &hay, const std::string &needle) { return lower(hay).find(lower(needle)) != std::string::npos; }

void replace_all(std::string &s, const std::string &from, const std::string &to) {
    if (from.empty())
        return;
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to);
        p += to.size();
    }
}

bool between(const std::string &s, const std::string &a, const std::string &b, std::string &out, size_t from, size_t *end_pos) {
    size_t p = s.find(a, from);
    if (p == std::string::npos)
        return false;
    p += a.size();
    size_t e = s.find(b, p);
    if (e == std::string::npos)
        return false;
    out = s.substr(p, e - p);
    if (end_pos)
        *end_pos = e + b.size();
    return true;
}

std::string enclosing_tag(const std::string &html, size_t pos) {
    size_t b = html.rfind('<', pos);
    if (b == std::string::npos)
        return "";
    size_t e = html.find('>', pos);
    if (e == std::string::npos)
        return "";
    return html.substr(b, e - b + 1);
}

bool find_tag(const std::string &html, const std::string &needle, size_t from, std::string &tag, size_t *tag_end) {
    size_t p = from;
    while ((p = html.find(needle, p)) != std::string::npos) {
        size_t b = html.rfind('<', p);
        size_t close = html.rfind('>', p);
        if (b != std::string::npos && (close == std::string::npos || close < b)) {
            size_t e = html.find('>', p);
            if (e == std::string::npos)
                return false;
            tag = html.substr(b, e - b + 1);
            if (tag_end)
                *tag_end = e + 1;
            return true;
        }
        p += needle.size();
    }
    return false;
}

std::string attr(const std::string &tag, const std::string &name) {
    size_t p = 0;
    while ((p = tag.find(name, p)) != std::string::npos) {
        bool boundary_ok = p > 0 && (std::isspace((unsigned char)tag[p - 1]) || tag[p - 1] == '<');
        size_t q = p + name.size();
        while (q < tag.size() && std::isspace((unsigned char)tag[q]))
            q++;
        if (!boundary_ok || q >= tag.size() || tag[q] != '=') {
            p += name.size();
            continue;
        }
        q++;
        while (q < tag.size() && std::isspace((unsigned char)tag[q]))
            q++;
        if (q >= tag.size())
            return "";
        char quote = tag[q];
        if (quote == '"' || quote == '\'') {
            size_t e = tag.find(quote, q + 1);
            if (e == std::string::npos)
                return "";
            return html_decode(tag.substr(q + 1, e - q - 1));
        }
        size_t e = q;
        while (e < tag.size() && !std::isspace((unsigned char)tag[e]) && tag[e] != '>')
            e++;
        return html_decode(tag.substr(q, e - q));
    }
    return "";
}

std::string inner_text(const std::string &html, size_t tag_end, const std::string &tag_name) {
    // Handles nesting of the same tag name.
    std::string open = "<" + tag_name;
    std::string close = "</" + tag_name;
    size_t depth = 1, p = tag_end;
    while (p < html.size()) {
        size_t no = html.find(open, p);
        size_t nc = html.find(close, p);
        if (nc == std::string::npos)
            break;
        if (no != std::string::npos && no < nc) {
            depth++;
            p = no + open.size();
        } else {
            if (--depth == 0)
                return collapse_ws(html_decode(strip_tags(html.substr(tag_end, nc - tag_end))));
            p = nc + close.size();
        }
    }
    return "";
}

std::string strip_tags(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    bool in_tag = false;
    for (char c : s) {
        if (c == '<') {
            in_tag = true;
            out += ' ';
        } else if (c == '>') {
            in_tag = false;
        } else if (!in_tag) {
            out += c;
        }
    }
    return out;
}

static void append_utf8(std::string &out, unsigned long cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

std::string html_decode(const std::string &s) {
    static const struct {
        const char *name;
        const char *value;
    } named[] = {
        {"amp", "&"},       {"lt", "<"},        {"gt", ">"},        {"quot", "\""},     {"apos", "'"},      {"nbsp", " "},      {"eacute", "\xC3\xA9"},
        {"egrave", "\xC3\xA8"}, {"ecirc", "\xC3\xAA"}, {"agrave", "\xC3\xA0"}, {"acirc", "\xC3\xA2"}, {"ccedil", "\xC3\xA7"}, {"ocirc", "\xC3\xB4"}, {"icirc", "\xC3\xAE"},
        {"ucirc", "\xC3\xBB"}, {"ugrave", "\xC3\xB9"}, {"euml", "\xC3\xAB"},  {"iuml", "\xC3\xAF"},  {"rsquo", "'"},     {"lsquo", "'"},     {"ldquo", "\""},
        {"rdquo", "\""},    {"hellip", "..."},  {"laquo", "\""},    {"raquo", "\""},    {"ndash", "-"},     {"mdash", "-"},
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            out += s[i];
            continue;
        }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (!ent.empty() && ent[0] == '#') {
            unsigned long cp = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) ? strtoul(ent.c_str() + 2, nullptr, 16) : strtoul(ent.c_str() + 1, nullptr, 10);
            if (cp > 0) {
                append_utf8(out, cp);
                i = semi;
                continue;
            }
        } else {
            bool found = false;
            for (const auto &n : named) {
                if (ent == n.name) {
                    out += n.value;
                    found = true;
                    break;
                }
            }
            if (found) {
                i = semi;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

std::string collapse_ws(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    bool ws = false;
    for (char c : s) {
        if ((unsigned char)c <= ' ') {
            ws = true;
        } else {
            if (ws && !out.empty())
                out += ' ';
            ws = false;
            out += c;
        }
    }
    return out;
}

std::string strip_js_comments(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            size_t e = s.find("*/", i + 2);
            if (e == std::string::npos)
                break;
            i = e + 2;
            continue;
        }
        out += s[i++];
    }
    return out;
}

std::string clean_query(const std::string &in) {
    // Fold the most common Latin-1 accents encoded as UTF-8 (C3 xx).
    static const char *fold = "aaaaaaaceeeeiiiidnooooo/ouuuuyty"; // for C3 A0..BF (lowercase block)
    std::string tmp;
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == 0xC3 && i + 1 < in.size()) {
            unsigned char d = (unsigned char)in[i + 1];
            if (d >= 0x80 && d <= 0x9F)
                d += 0x20; // uppercase -> lowercase block
            if (d >= 0xA0 && d <= 0xBF)
                tmp += fold[d - 0xA0];
            i++;
            continue;
        }
        if (c >= 0x80)
            continue;
        tmp += (char)std::tolower(c);
    }
    std::string out;
    for (char c : tmp) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ')
            out += c;
        else if (std::isspace((unsigned char)c))
            out += ' ';
    }
    return collapse_ws(out);
}

std::string url_origin(const std::string &url) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos)
        return "";
    size_t e = url.find('/', scheme + 3);
    return e == std::string::npos ? url : url.substr(0, e);
}

std::string url_host(const std::string &url) {
    std::string o = url_origin(url);
    size_t scheme = o.find("://");
    std::string host = scheme == std::string::npos ? o : o.substr(scheme + 3);
    size_t colon = host.find(':');
    if (colon != std::string::npos)
        host = host.substr(0, colon);
    host = lower(host);
    if (starts_with(host, "www."))
        host = host.substr(4);
    return host;
}

std::string url_resolve(const std::string &base, const std::string &ref) {
    if (ref.empty())
        return base;
    if (starts_with(ref, "http://") || starts_with(ref, "https://"))
        return ref;
    if (starts_with(ref, "//")) {
        size_t scheme = base.find("://");
        return (scheme == std::string::npos ? "https" : base.substr(0, scheme)) + ":" + ref;
    }
    if (ref[0] == '/')
        return url_origin(base) + ref;
    std::string b = base;
    size_t q = b.find_first_of("?#");
    if (q != std::string::npos)
        b = b.substr(0, q);
    size_t slash = b.rfind('/');
    size_t scheme = b.find("://");
    if (slash == std::string::npos || (scheme != std::string::npos && slash < scheme + 3))
        return b + "/" + ref;
    return b.substr(0, slash + 1) + ref;
}

std::string rstrip_char(std::string s, char c) {
    while (!s.empty() && s.back() == c)
        s.pop_back();
    return s;
}

std::string lstrip_char(const std::string &s, char c) {
    size_t b = 0;
    while (b < s.size() && s[b] == c)
        b++;
    return s.substr(b);
}

std::vector<std::string> js_string_literals(const std::string &js) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < js.size()) {
        char q = js[i];
        if (q != '\'' && q != '"') {
            i++;
            continue;
        }
        std::string lit;
        size_t j = i + 1;
        while (j < js.size() && js[j] != q) {
            if (js[j] == '\\' && j + 1 < js.size()) {
                lit += js[j + 1];
                j += 2;
                continue;
            }
            lit += js[j++];
        }
        out.push_back(lit);
        i = j + 1;
    }
    return out;
}

} // namespace txt

