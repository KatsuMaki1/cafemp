// Port of AnimeSamaApi src/backend.py + src/utils/utils.py (GPL-3.0, TMCooper)

#include "animesama/animesama.hpp"

#include "animesama/text_utils.hpp"
#include "logger/logger.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <jansson.h>
#include <map>
#include <mutex>

namespace animesama {

static const char *AS = "AnimeSama";
static const char *DOMAIN_LIST_URL = "https://anime-sama.pw";
// Used when anime-sama.pw itself is unreachable.
static const char *FALLBACK_DOMAINS[] = {"anime-sama.org", "anime-sama.fr", "anime-sama.si", "anime-sama.eu", "anime-sama.to", "anime-sama.tv"};

static std::mutex base_mtx;
static std::string base_url_cache;

const char *lang_name(Lang l) { return l == Lang::VF ? "vf" : "vostfr"; }

void set_base_url(const std::string &url) {
    std::lock_guard<std::mutex> lk(base_mtx);
    base_url_cache = txt::rstrip_char(url, '/');
}

static std::string bot_wall_msg(const std::string &where) { return "Cloudflare bloque l'acces a " + where + " depuis la console. Reessaie plus tard."; }

static bool domain_is_online(http::Session &s, const std::string &domain, std::string &resolved_origin, std::string &last_err) {
    http::Request req;
    req.url = "https://" + domain;
    req.follow_redirects = true;
    req.timeout_s = 6;
    http::Response r = s.get(req);
    if (!r.error.empty()) {
        last_err = domain + ": " + r.error;
        return false;
    }
    if (http::is_bot_wall(r)) {
        last_err = domain + ": Cloudflare (" + std::to_string(r.status) + ")";
        return false;
    }
    if (r.status == 200) {
        std::string origin = txt::url_origin(r.effective_url);
        resolved_origin = origin.empty() ? ("https://" + domain) : origin;
        return true;
    }
    last_err = domain + ": HTTP " + std::to_string(r.status);
    return false;
}

bool find_base_url(std::string &out, std::string &err) {
    {
        std::lock_guard<std::mutex> lk(base_mtx);
        if (!base_url_cache.empty()) {
            out = base_url_cache;
            return true;
        }
    }

    http::Session s;
    std::vector<std::string> domains;

    http::Request req;
    req.url = DOMAIN_LIST_URL;
    req.timeout_s = 10;
    http::Response r = s.get(req);
    std::string list;
    if (r.ok() && txt::between(r.body, "const domains = [", "];", list)) {
        size_t p = 0;
        std::string name;
        size_t end = 0;
        while (txt::between(list, "name: '", "'", name, p, &end)) {
            domains.push_back(name);
            p = end;
        }
    } else {
        log_message(LOG_WARNING, AS, "Could not read domain list from %s (status %ld, err=%s)", DOMAIN_LIST_URL, r.status, r.error.c_str());
    }
    for (const char *d : FALLBACK_DOMAINS)
        if (std::find(domains.begin(), domains.end(), d) == domains.end())
            domains.push_back(d);

    std::string last_err;
    for (const std::string &d : domains) {
        std::string origin;
        if (domain_is_online(s, d, origin, last_err)) {
            set_base_url(origin);
            out = origin;
            log_message(LOG_OK, AS, "Active domain: %s", out.c_str());
            return true;
        }
    }
    err = "Anime-Sama inaccessible (" + (last_err.empty() ? "connexion internet ?" : last_err) + ")";
    return false;
}

// ---------------------------------------------------------------- search

// Positions of the opening tags whose class list contains the word "as-carte".
static std::vector<size_t> find_card_positions(const std::string &html) {
    std::vector<size_t> pos;
    size_t p = 0;
    while ((p = html.find("as-carte", p)) != std::string::npos) {
        std::string tag = txt::enclosing_tag(html, p);
        std::string c = " " + txt::attr(tag, "class") + " ";
        for (char &ch : c)
            if (ch == '\t' || ch == '\n' || ch == '\r')
                ch = ' ';
        if (c.find(" as-carte ") != std::string::npos) {
            size_t b = html.rfind('<', p);
            if (pos.empty() || pos.back() != b)
                pos.push_back(b);
        }
        p += 8;
    }
    return pos;
}

static void parse_cards(const std::string &html, const std::string &base, std::vector<SearchResult> &out) {
    std::vector<size_t> cards = find_card_positions(html);
    for (size_t ci = 0; ci < cards.size(); ci++) {
        size_t p = cards[ci];
        size_t tag_close = html.find('>', p);
        if (tag_close == std::string::npos)
            break;
        std::string tag = html.substr(p, tag_close - p + 1);

        SearchResult res;
        res.title = txt::trim(txt::attr(tag, "data-nom"));
        std::string link = txt::trim(txt::attr(tag, "data-url"));
        res.cover = txt::trim(txt::attr(tag, "data-img"));
        if (txt::starts_with(tag, "<a"))
            link = link.empty() ? txt::attr(tag, "href") : link;

        size_t card_end = ci + 1 < cards.size() ? cards[ci + 1] : std::string::npos;
        std::string body = html.substr(p, card_end == std::string::npos ? std::string::npos : card_end - p);

        std::string inner;
        if (link.empty() && txt::find_tag(body, "<a", 0, inner))
            link = txt::attr(inner, "href");
        if (res.title.empty()) {
            size_t te = 0;
            if (txt::find_tag(body, "as-carte-titre", 0, inner, &te))
                res.title = txt::trim(txt::inner_text(body, te, inner.substr(1, inner.find_first_of(" >") - 1)));
        }
        if (res.cover.empty() && txt::find_tag(body, "<img", 0, inner))
            res.cover = txt::attr(inner, "src");

        if (!link.empty()) {
            std::string abs = txt::url_resolve(base + "/", link);
            // Keep the path only, rebased on the active domain.
            std::string origin = txt::url_origin(abs);
            std::string path = abs.substr(origin.size());
            size_t q = path.find_first_of("?#");
            if (q != std::string::npos)
                path = path.substr(0, q);
            res.url = base + txt::rstrip_char(path, '/');
        }

        bool dup = false;
        for (const SearchResult &e : out)
            if (e.url == res.url)
                dup = true;
        if (!res.title.empty() && !res.url.empty() && !dup)
            out.push_back(res);
    }

    if (!out.empty())
        return;

    // Legacy layout: <a href="..."><img class="card-image" src="..."><h2 class="card-title">Title</h2></a>
    size_t p = 0;
    std::string tag;
    size_t tag_end = 0;
    while (txt::find_tag(html, "card-title", p, tag, &tag_end)) {
        SearchResult res;
        res.title = txt::trim(txt::inner_text(html, tag_end, "h2"));
        size_t a = html.rfind("<a ", tag_end);
        if (a != std::string::npos) {
            std::string atag = txt::enclosing_tag(html, a + 1);
            std::string href = txt::attr(atag, "href");
            if (!href.empty())
                res.url = txt::rstrip_char(txt::url_resolve(base + "/", href), '/');
            std::string img;
            if (txt::find_tag(html.substr(a, tag_end - a), "card-image", 0, img))
                res.cover = txt::attr(img, "src");
        }
        if (!res.title.empty() && !res.url.empty())
            out.push_back(res);
        p = tag_end;
    }
}

std::vector<SearchResult> search(const std::string &query, std::string &err) {
    std::vector<SearchResult> results;
    std::string base;
    if (!find_base_url(base, err))
        return results;

    std::string cleaned = txt::clean_query(query);
    if (cleaned.empty()) {
        err = "Recherche vide";
        return results;
    }

    http::Session s;
    http::Request req;
    req.url = base + "/catalogue/fragment.php?page=1&search=" + http::url_encode(cleaned);
    req.referer = base + "/catalogue/";
    req.headers.push_back("X-Requested-With: XMLHttpRequest");
    req.timeout_s = 10;
    http::Response r = s.get(req);

    if (http::is_bot_wall(r)) {
        err = bot_wall_msg("anime-sama");
        return results;
    }

    std::string html;
    if (r.ok()) {
        json_error_t jerr;
        json_t *root = json_loads(r.body.c_str(), 0, &jerr);
        if (root) {
            json_t *liste = json_object_get(root, "liste");
            if (json_is_string(liste))
                html = json_string_value(liste);
            json_decref(root);
        } else if (r.body.find("as-carte") != std::string::npos) {
            html = r.body;
        }
    }

    if (html.empty()) {
        // Fallback: regular catalogue page with a search parameter.
        req.url = base + "/catalogue/?search=" + http::url_encode(cleaned);
        req.headers.clear();
        r = s.get(req);
        if (http::is_bot_wall(r)) {
            err = bot_wall_msg("anime-sama");
            return results;
        }
        if (!r.ok()) {
            err = r.error.empty() ? "Erreur HTTP " + std::to_string(r.status) : r.error;
            return results;
        }
        html = r.body;
    }

    parse_cards(html, base, results);
    log_message(LOG_OK, AS, "search('%s') -> %d result(s)", cleaned.c_str(), (int)results.size());
    return results;
}

// ---------------------------------------------------------------- anime page

static std::string extract_synopsis(const std::string &html) {
    struct Candidate {
        const char *needle;
        const char *tag;
    } candidates[] = {
        {"ak-slide-synopsis", "p"},
        {"id=\"synopsisText\"", nullptr},
        {"oe-synopsis", nullptr},
    };
    for (const Candidate &c : candidates) {
        std::string tag;
        size_t tag_end = 0;
        if (!txt::find_tag(html, c.needle, 0, tag, &tag_end))
            continue;
        std::string name = c.tag ? c.tag : tag.substr(1, tag.find_first_of(" \t\n>") - 1);
        std::string inner;
        if (std::string(c.needle) == "oe-synopsis") {
            // Prefer the first <p> inside the container.
            std::string ptag;
            size_t p_end = 0;
            size_t limit = html.find("</" + name, tag_end);
            if (txt::find_tag(html, "<p", tag_end, ptag, &p_end) && p_end < limit)
                inner = txt::inner_text(html, p_end, "p");
        }
        if (inner.empty())
            inner = txt::inner_text(html, tag_end, name);
        // Drop "Lire plus" style buttons.
        for (const char *noise : {"Lire plus", "Lire moins", "Voir plus", "Voir moins"})
            txt::replace_all(inner, noise, "");
        inner = txt::trim(txt::collapse_ws(inner));
        if (!inner.empty())
            return inner;
    }
    return "";
}

static std::vector<std::string> extract_alt_titles(const std::string &html) {
    std::vector<std::string> out;
    std::string tag;
    size_t tag_end = 0;
    if (!txt::find_tag(html, "id=\"titreAlter\"", 0, tag, &tag_end) && !txt::find_tag(html, "oe-alters", 0, tag, &tag_end))
        return out;
    std::string name = tag.substr(1, tag.find_first_of(" \t\n>") - 1);
    std::string text = txt::inner_text(html, tag_end, name);
    size_t b = 0;
    while (b <= text.size()) {
        size_t e = text.find(',', b);
        std::string t = txt::trim(text.substr(b, e == std::string::npos ? std::string::npos : e - b));
        if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end())
            out.push_back(t);
        if (e == std::string::npos)
            break;
        b = e + 1;
    }
    return out;
}

// panneauAnime("Saison 1", "saison1/vostfr"); (also panneauFilm / panneauVisual; scans are skipped)
static void extract_seasons(const std::string &html, const std::string &anime_url, std::vector<Season> &out) {
    std::string js = txt::strip_js_comments(html);
    size_t p = 0;
    while ((p = js.find("panneau", p)) != std::string::npos) {
        size_t q = p + 7;
        p = q;
        std::string kind;
        while (q < js.size() && std::isalpha((unsigned char)js[q]))
            kind += js[q++];
        if (kind != "Anime" && kind != "Film" && kind != "Visual")
            continue;
        while (q < js.size() && std::isspace((unsigned char)js[q]))
            q++;
        if (q >= js.size() || js[q] != '(')
            continue;
        size_t close = js.find(')', q);
        if (close == std::string::npos)
            continue;
        std::vector<std::string> args = txt::js_string_literals(js.substr(q + 1, close - q - 1));
        if (args.size() < 2)
            continue;
        std::string nom = txt::trim(args[0]);
        std::string lien = txt::trim(args[1]);
        if (txt::lower(nom) == "nom" || txt::lower(lien) == "url" || lien.empty())
            continue;
        Season s;
        s.name = txt::html_decode(nom);
        s.url = txt::rstrip_char(anime_url, '/') + "/" + txt::lstrip_char(lien, '/');
        bool dup = false;
        for (const Season &e : out)
            if (e.url == s.url)
                dup = true;
        if (!dup)
            out.push_back(s);
    }
}

bool get_info(const SearchResult &anime, AnimeInfo &out, std::string &err) {
    http::Session s;
    http::Request req;
    req.url = anime.url;
    req.timeout_s = 12;
    http::Response r = s.get(req);
    if (http::is_bot_wall(r)) {
        err = bot_wall_msg("anime-sama");
        return false;
    }
    if (!r.ok()) {
        err = r.error.empty() ? "Erreur HTTP " + std::to_string(r.status) : r.error;
        return false;
    }

    out = AnimeInfo{};
    out.title = anime.title;
    out.url = anime.url;
    out.synopsis = extract_synopsis(r.body);
    out.alt_titles = extract_alt_titles(r.body);
    extract_seasons(r.body, anime.url, out.seasons);

    if (out.seasons.empty()) {
        err = "Aucune saison trouvee pour cet anime";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- episodes

static std::string season_url_for_lang(const std::string &url, Lang lang) {
    std::string u = txt::rstrip_char(url, '/');
    std::string low = txt::lower(u);
    if (txt::ends_with(low, "/vostfr"))
        u = u.substr(0, u.size() - 7);
    else if (txt::ends_with(low, "/vf"))
        u = u.substr(0, u.size() - 3);
    return u + "/" + lang_name(lang);
}

struct Player {
    std::string name;
    std::vector<std::string> entries;
    std::string hint; // url or epsMeta hint for the first entry, lower case
};

static int player_index(const std::string &name) {
    std::string digits;
    for (char c : name)
        if (std::isdigit((unsigned char)c))
            digits += c;
    return digits.empty() ? 99 : atoi(digits.c_str());
}

static int player_priority(const std::string &hint) {
    if (hint.find("ansembed") != std::string::npos)
        return 0;
    for (const char *h : {"smoothpre", "vidhide", "streamwish", "vidmoly"})
        if (hint.find(h) != std::string::npos)
            return 1;
    if (hint.find("sendvid") != std::string::npos)
        return 2;
    for (const char *h : {"embed4me", "lpayer", "player"})
        if (hint.find(h) != std::string::npos)
            return 3;
    if (hint.find("sibnet") != std::string::npos)
        return 5; // supported by our resolver but skipped by the original API: keep as last resort
    return 4;
}

// Parses `var epsX = [ ... ];` arrays.
static std::vector<Player> parse_players(const std::string &src) {
    std::vector<Player> players;
    size_t p = 0;
    while ((p = src.find("var ", p)) != std::string::npos) {
        size_t q = p + 4;
        p = q;
        while (q < src.size() && std::isspace((unsigned char)src[q]))
            q++;
        size_t name_b = q;
        while (q < src.size() && (std::isalnum((unsigned char)src[q]) || src[q] == '_'))
            q++;
        std::string name = src.substr(name_b, q - name_b);
        if (!txt::starts_with(name, "eps") || name == "epsJeton" || name == "epsMeta")
            continue;
        while (q < src.size() && std::isspace((unsigned char)src[q]))
            q++;
        if (q >= src.size() || src[q] != '=')
            continue;
        q++;
        while (q < src.size() && std::isspace((unsigned char)src[q]))
            q++;
        if (q >= src.size() || src[q] != '[')
            continue;
        size_t end = src.find("];", q);
        if (end == std::string::npos)
            continue;
        Player pl;
        pl.name = name;
        for (const std::string &lit : txt::js_string_literals(src.substr(q + 1, end - q - 1))) {
            std::string e = txt::trim(lit);
            if (txt::starts_with(e, "http://") || txt::starts_with(e, "https://") || txt::starts_with(e, "lien:"))
                pl.entries.push_back(e);
        }
        if (!pl.entries.empty())
            players.push_back(pl);
        p = end;
    }
    return players;
}

bool get_episodes(const Season &season, Lang lang, EpisodeList &out, std::string &err) {
    http::Session s;
    std::string link = season_url_for_lang(season.url, lang);

    http::Request req;
    req.url = link;
    req.timeout_s = 12;
    http::Response r = s.get(req);
    if (!r.ok() && link != season.url) {
        req.url = season.url;
        r = s.get(req);
        if (r.ok())
            link = season.url;
    }
    if (http::is_bot_wall(r)) {
        err = bot_wall_msg("anime-sama");
        return false;
    }
    if (!r.ok()) {
        err = lang == Lang::VF ? "Pas de version VF disponible pour cette saison" : (r.error.empty() ? "Erreur HTTP " + std::to_string(r.status) : r.error);
        return false;
    }

    out = EpisodeList{};
    out.page_url = link;

    std::string eps_source;
    // New format: inline script with var eps1 = [...], epsJeton, epsMeta.
    {
        size_t p = 0;
        while ((p = r.body.find("var eps", p)) != std::string::npos) {
            size_t q = r.body.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_", p + 7);
            if (q != std::string::npos) {
                q = r.body.find_first_not_of(" \t\r\n", q);
                if (q != std::string::npos && r.body[q] == '=') {
                    q = r.body.find_first_not_of(" \t\r\n", q + 1);
                    if (q != std::string::npos && r.body[q] == '[') {
                        size_t sb = r.body.rfind("<script", p);
                        size_t se = r.body.find("</script>", p);
                        eps_source = r.body.substr(sb == std::string::npos ? 0 : sb, se == std::string::npos ? std::string::npos : se - (sb == std::string::npos ? 0 : sb));
                        break;
                    }
                }
            }
            p += 7;
        }
    }

    std::map<std::string, std::string> meta_hint;
    if (!eps_source.empty()) {
        std::string jeton;
        size_t jp = eps_source.find("epsJeton");
        if (jp != std::string::npos) {
            size_t eq = eps_source.find('=', jp);
            if (eq != std::string::npos) {
                size_t q1 = eps_source.find_first_of("\"'", eq);
                if (q1 != std::string::npos) {
                    size_t q2 = eps_source.find(eps_source[q1], q1 + 1);
                    if (q2 != std::string::npos)
                        out.jeton = eps_source.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        }
        size_t mp = eps_source.find("epsMeta");
        if (mp != std::string::npos) {
            size_t ob = eps_source.find('{', mp);
            size_t oe = ob == std::string::npos ? std::string::npos : eps_source.find("};", ob);
            if (oe != std::string::npos) {
                json_error_t jerr;
                json_t *meta = json_loadb(eps_source.c_str() + ob, oe - ob + 1, 0, &jerr);
                if (meta && json_is_object(meta)) {
                    const char *key;
                    json_t *val;
                    json_object_foreach(meta, key, val) {
                        if (json_is_array(val) && json_array_size(val) > 0 && json_is_string(json_array_get(val, 0)))
                            meta_hint[txt::lower(key)] = txt::lower(json_string_value(json_array_get(val, 0)));
                    }
                }
                if (meta)
                    json_decref(meta);
            }
        }
    } else {
        // Old format: external episodes.js
        std::string tag;
        size_t from = 0, tag_end = 0;
        std::string js_link;
        while (txt::find_tag(r.body, "<script", from, tag, &tag_end)) {
            std::string src = txt::attr(tag, "src");
            if (src.find("episodes.js") != std::string::npos) {
                js_link = src;
                break;
            }
            from = tag_end;
        }
        if (js_link.empty()) {
            err = "Liste des episodes introuvable";
            return false;
        }
        http::Request jr;
        jr.url = txt::url_resolve(txt::rstrip_char(link, '/') + "/", js_link);
        jr.referer = link;
        jr.timeout_s = 12;
        http::Response js = s.get(jr);
        if (!js.ok()) {
            err = "Impossible de charger episodes.js";
            return false;
        }
        eps_source = js.body;
    }

    std::vector<Player> players = parse_players(eps_source);
    if (players.empty()) {
        err = "Aucun lecteur trouve pour cette saison";
        return false;
    }

    for (Player &pl : players) {
        std::string first = txt::lower(pl.entries[0]);
        if (txt::starts_with(first, "lien:")) {
            auto it = meta_hint.find(first.substr(5));
            pl.hint = it != meta_hint.end() ? it->second : first;
        } else {
            pl.hint = first;
        }
    }

    std::stable_sort(players.begin(), players.end(), [](const Player &a, const Player &b) {
        int pa = player_priority(a.hint), pb = player_priority(b.hint);
        if (pa != pb)
            return pa < pb;
        return player_index(a.name) < player_index(b.name);
    });

    size_t count = 0;
    for (const Player &pl : players)
        count = std::max(count, pl.entries.size());

    for (size_t i = 0; i < count; i++) {
        Episode ep;
        ep.number = (int)i + 1;
        for (const Player &pl : players)
            if (i < pl.entries.size())
                ep.sources.push_back(pl.entries[i]);
        out.episodes.push_back(ep);
    }

    log_message(LOG_OK, AS, "%s: %d episode(s), %d player(s)", link.c_str(), (int)count, (int)players.size());
    return true;
}

// ---------------------------------------------------------------- resolve

static std::string fetch_lien_url(http::Session &s, const EpisodeList &list, const std::string &entry) {
    if (!txt::starts_with(entry, "lien:"))
        return entry;
    if (list.jeton.empty())
        return "";
    std::string id = entry.substr(5);
    std::string api = txt::url_origin(list.page_url) + "/api/lien.php?j=" + http::url_encode(list.jeton) + "&l=" + http::url_encode(id);
    for (int attempt = 0; attempt < 2; attempt++) {
        http::Response r = s.get({api, list.page_url, {"X-Requested-With: XMLHttpRequest"}, "", true, 8});
        if (r.status != 200)
            continue;
        json_error_t jerr;
        json_t *root = json_loads(r.body.c_str(), 0, &jerr);
        if (!root)
            continue;
        std::string u;
        json_t *jv = json_object_get(root, "u");
        if (json_is_string(jv))
            u = txt::trim(json_string_value(jv));
        json_decref(root);
        if (!u.empty())
            return u;
    }
    return "";
}

bool resolve_episode(const EpisodeList &list, const Episode &ep, Stream &out, std::string &err) {
    http::Session s;
    Stream best_candidate;
    int tried = 0;

    for (const std::string &raw : ep.sources) {
        std::string url = fetch_lien_url(s, list, txt::trim(raw));
        if (url.empty())
            continue;
        tried++;

        Stream st;
        if (!resolve_video_url(s, url, st)) {
            log_message(LOG_DEBUG, AS, "Resolver failed for %s", url.c_str());
            continue;
        }
        if (st.type != "m3u8" && st.type != "mp4")
            continue; // embeds can't be played natively

        int score = check_stream_health(s, st);
        if (score == 0) {
            out = st;
            log_message(LOG_OK, AS, "Episode %d -> %s (%s)", ep.number, st.url.c_str(), st.type.c_str());
            return true;
        }
        if (score < 999 && best_candidate.url.empty())
            best_candidate = st;
    }

    if (!best_candidate.url.empty()) {
        out = best_candidate;
        return true;
    }
    err = tried ? "Aucun lecteur compatible pour cet episode (" + std::to_string(tried) + " essaye(s))" : "Aucun lien disponible pour cet episode";
    return false;
}

} // namespace animesama
