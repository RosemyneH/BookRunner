#!/usr/bin/env python3
"""Emit data/bangs_generated.ini: curated bangs + labels + freedesktop-style icon names (offline)."""

from __future__ import annotations

import sys
from typing import Iterable, Tuple

BangRow = Tuple[str, str, str, str]

BANGS: Tuple[BangRow, ...] = (
    ("g", "https://www.google.com/search?q=%s", "Google Search", "google"),
    ("img", "https://www.google.com/search?tbm=isch&q=%s", "Google Images", "image-x-generic"),
    ("maps", "https://www.google.com/maps/search/%s", "Google Maps", "web-browser"),
    ("n", "https://news.google.com/search?q=%s", "Google News", "news"),
    ("scholar", "https://scholar.google.com/scholar?q=%s", "Google Scholar", "office-university"),
    ("tr", "https://translate.google.com/?sl=auto&tl=en&text=%s", "Google Translate", "preferences-desktop-locale"),
    ("flights", "https://www.google.com/travel/flights?q=%s", "Google Flights", "web-browser"),
    ("hotels", "https://www.google.com/travel/hotels?q=%s", "Google Hotels", "office-calendar"),
    ("w", "https://en.wikipedia.org/wiki/Special:Search?search=%s", "Wikipedia", "wikipedia"),
    ("wl", "https://en.wiktionary.org/wiki/Special:Search?search=%s", "Wiktionary", "accessories-dictionary"),
    ("aw", "https://wiki.archlinux.org/index.php?search=%s", "Arch Wiki", "distributor-logo-archlinux"),
    ("mdn", "https://developer.mozilla.org/en-US/search?q=%s", "MDN Web Docs", "text-html"),
    ("yt", "https://www.youtube.com/results?search_query=%s", "YouTube", "youtube"),
    ("tw", "https://www.twitch.tv/search?term=%s", "Twitch", "video-x-generic"),
    ("sp", "https://open.spotify.com/search/%s", "Spotify", "audio-x-generic"),
    ("bc", "https://bandcamp.com/search?q=%s", "Bandcamp", "audio-x-generic"),
    ("sc", "https://soundcloud.com/search?q=%s", "SoundCloud", "audio-x-generic"),
    ("imdb", "https://www.imdb.com/find?q=%s", "IMDb", "video-x-generic"),
    ("rt", "https://www.rottentomatoes.com/search?search=%s", "Rotten Tomatoes", "video-x-generic"),
    ("lb", "https://letterboxd.com/search/%s", "Letterboxd", "video-x-generic"),
    ("gh", "https://github.com/search?q=%s&type=repositories", "GitHub", "github"),
    ("gl", "https://gitlab.com/search?search=%s", "GitLab", "gitlab"),
    ("sr", "https://sourcegraph.com/search?q=context:global+%s", "Sourcegraph", "text-x-script"),
    ("so", "https://stackoverflow.com/search?q=%s", "Stack Overflow", "text-x-generic"),
    ("se", "https://stackexchange.com/search?q=%s", "Stack Exchange", "text-x-generic"),
    ("hn", "https://hn.algolia.com/?query=%s", "Hacker News", "text-x-generic"),
    ("lob", "https://lobste.rs/search?q=%s", "Lobsters", "text-x-generic"),
    ("r", "https://www.reddit.com/search/?q=%s", "Reddit", "web-browser"),
    ("x", "https://x.com/search?q=%s", "X (Twitter)", "web-browser"),
    ("li", "https://www.linkedin.com/search/results/all/?keywords=%s", "LinkedIn", "office-contact"),
    ("bsky", "https://bsky.app/search?q=%s", "Bluesky", "web-browser"),
    ("mast", "https://mastodon.social/explore?q=%s", "Mastodon (search)", "web-browser"),
    ("npm", "https://www.npmjs.com/search?q=%s", "npm", "package-x-generic"),
    ("crates", "https://crates.io/search?q=%s", "crates.io", "package-x-generic"),
    ("pypi", "https://pypi.org/search/?q=%s", "PyPI", "package-x-generic"),
    ("cpan", "https://metacpan.org/search?q=%s", "MetaCPAN", "package-x-generic"),
    ("go", "https://pkg.go.dev/search?q=%s", "Go packages", "package-x-generic"),
    ("docker", "https://hub.docker.com/search?q=%s", "Docker Hub", "application-x-executable"),
    ("brew", "https://formulae.brew.sh/search?q=%s", "Homebrew Formulae", "package-x-generic"),
    ("aur", "https://aur.archlinux.org/packages?O=0&K=%s", "AUR", "package-x-generic"),
    ("deb", "https://packages.debian.org/search?keywords=%s", "Debian packages", "package-x-generic"),
    ("man", "https://man.archlinux.org/search?q=%s", "man.archlinux.org", "help-browser"),
    ("ddg", "https://duckduckgo.com/?q=%s", "DuckDuckGo", "duckduckgo"),
    ("bing", "https://www.bing.com/search?q=%s", "Bing", "web-browser"),
    ("ebay", "https://www.ebay.com/sch/i.html?_nkw=%s", "eBay", "applications-office"),
    ("amz", "https://www.amazon.com/s?k=%s", "Amazon", "applications-office"),
    ("steam", "https://store.steampowered.com/search/?term=%s", "Steam Store", "steam"),
    ("epic", "https://store.epicgames.com/en-US/browse?q=%s", "Epic Games Store", "applications-games"),
    ("itch", "https://itch.io/search?q=%s", "itch.io", "applications-games"),
    ("pin", "https://www.pinterest.com/search/pins/?q=%s", "Pinterest", "web-browser"),
    ("ph", "https://www.producthunt.com/search?q=%s", "Product Hunt", "applications-internet"),
    ("arxiv", "https://arxiv.org/search/?query=%s&searchtype=all", "arXiv", "x-office-document"),
    ("doi", "https://doi.org/%s", "DOI resolver", "x-office-document"),
    ("orcid", "https://orcid.org/orcid-search/search?searchQuery=%s", "ORCID", "x-office-contact"),
    ("giphy", "https://giphy.com/search/%s", "Giphy", "image-x-generic"),
    ("urban", "https://www.urbandictionary.com/define.php?term=%s", "Urban Dictionary", "accessories-dictionary"),
    ("etym", "https://www.etymonline.com/search?q=%s", "Etymonline", "accessories-dictionary"),
    ("deepl", "https://www.deepl.com/translator#en/en/%s", "DeepL", "preferences-desktop-locale"),
    ("wa", "https://web.whatsapp.com/", "WhatsApp Web", "web-browser"),
    ("disc", "https://discord.com/channels/@me", "Discord", "web-browser"),
    ("meet", "https://meet.google.com/lookup/%s", "Google Meet (lookup)", "video-x-generic"),
    ("zoom", "https://www.zoom.us/search#searchType=1&query=%s", "Zoom search", "video-x-generic"),
    ("udemy", "https://www.udemy.com/courses/search/?src=ukw&q=%s", "Udemy", "office-education"),
    ("coursera", "https://www.coursera.org/search?query=%s", "Coursera", "office-education"),
    ("wh", "https://www.wikihow.com/wikiHowTo?search=%s", "wikiHow", "help-browser"),
    ("fandom", "https://community.fandom.com/wiki/Special:Search?scope=cross-wiki&query=%s", "Fandom", "web-browser"),
    ("cve", "https://nvd.nist.gov/vuln/search/results?form_type=Basic&results_type=overview&query=%s", "NVD CVE", "security-medium"),
    ("cvem", "https://cve.mitre.org/cgi-bin/cvekey.cgi?keyword=%s", "MITRE CVE", "security-medium"),
    ("shodan", "https://www.shodan.io/search?query=%s", "Shodan", "network-workgroup"),
    ("crt", "https://crt.sh/?q=%s", "crt.sh (certs)", "certificate-server"),
    ("dns", "https://www.nslookup.io/domains/%s/dns-records/", "DNS records (nslookup.io)", "network-workgroup"),
    ("wayback", "https://web.archive.org/web/*/%s", "Wayback Machine", "internet-web-browser"),
    ("whois", "https://www.whois.com/whois/%s", "WHOIS", "network-workgroup"),
    ("speed", "https://fast.com/", "Fast.com speed test", "network-transmit-receive"),
)


def write_ini(path: str, rows: Iterable[BangRow]) -> int:
    rows = tuple(rows)
    with open(path, "w", encoding="utf-8") as f:
        f.write(
            "# Curated bangs for BookRunner (regenerate: python3 tools/gen_bangs_ini.py)\n"
            "# [bang_icons] uses freedesktop icon names; missing icons fall back at runtime.\n\n"
        )
        f.write("[bangs]\n")
        for key, url, _name, _icon in rows:
            f.write(f"{key}={url}\n")
        f.write("\n[bang_desc]\n")
        for key, _url, name, _icon in rows:
            f.write(f"{key}={name}\n")
        f.write("\n[bang_icons]\n")
        for key, _url, _name, icon in rows:
            f.write(f"{key}={icon}\n")
    return len(rows)


def main() -> None:
    out = sys.argv[1] if len(sys.argv) > 1 else "data/bangs_generated.ini"
    n = write_ini(out, BANGS)
    print(n, "curated bangs ->", out, flush=True)


if __name__ == "__main__":
    main()
