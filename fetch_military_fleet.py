#!/usr/bin/env python3
"""
Scrape every ship class listed on https://www.globalmilitary.net/ships/
and save all the data into one JSON file.

Usage
-----
    python scrape_ships.py                       # crawl everything -> military_vessels.json
    python scrape_ships.py --url https://www.globalmilitary.net/ships/le-redoutable/
                                                 # one ship, printed to stdout (good for checking)
    python scrape_ships.py --html saved_page.html   # parse a locally saved page (debugging)
    python scrape_ships.py --no-resume           # ignore an existing output file

Requires:  pip install requests beautifulsoup4
"""
import argparse
import json
import os
import re
import sys
import time
from datetime import datetime, timezone
from urllib.parse import urljoin, urlparse

import requests
from bs4 import BeautifulSoup, Comment, NavigableString

BASE_URL = "https://www.globalmilitary.net"
LIST_URL = BASE_URL + "/ships/"
OUT_FILE = "military_vessels.json"
DELAY = 0.7  # seconds between requests, be polite
HEADERS = {
    "User-Agent": "Mozilla/5.0 (compatible; ship-scraper/1.0)",
    "Accept-Language": "en",
}

SHIP_HREF = re.compile(r"^/ships/([^/]+)/$")  # /ships/<slug>/  (not category/country/decade pages)
NON_SHIP_SLUGS = {"category", "country", "decade"}
HEADING_TAGS = ["h1", "h2", "h3", "h4", "h5", "h6"]
HERO_LABELS = ["Origin", "Commissioned", "Updated", "Displacement", "Max speed", "Ships built"]

# Fields we expect on a normal ship page; used for the completeness report.
EXPECTED = ["ship_name", "summary", "type", "origin", "commissioned", "image",
            "description", "tables", "armament", "operators"]


# --------------------------------------------------------------------------- helpers
def clean(text):
    return re.sub(r"\s+", " ", text or "").strip()


def make_session():
    s = requests.Session()
    s.headers.update(HEADERS)
    return s


def get(session, url, retries=4):
    last = None
    for attempt in range(retries):
        try:
            r = session.get(url, timeout=30)
            if r.status_code == 200:
                return r.text
            last = f"HTTP {r.status_code}"
            if r.status_code == 404:
                break
        except requests.RequestException as e:
            last = str(e)
        time.sleep(1.5 * (attempt + 1))
    raise RuntimeError(f"GET {url} failed: {last}")


def prev_text(el):
    """Nearest non-empty text node before `el` (section titles are plain text lines)."""
    node = el.find_previous(
        string=lambda s: isinstance(s, NavigableString)
        and not isinstance(s, Comment)
        and s.strip()
    )
    return clean(node) if node else ""


def title_for(el, fallback):
    cap = el.find("caption")
    if cap and clean(cap.get_text()):
        return clean(cap.get_text())
    t = prev_text(el)
    if not t or len(t) > 60:
        h = el.find_previous(HEADING_TAGS)
        t = clean(h.get_text()) if h else ""
    return t or fallback


def put(d, key, value):
    k, n = key, 2
    while k in d:
        k = f"{key} ({n})"
        n += 1
    d[k] = value


def parse_table(table):
    rows = {}
    for tr in table.find_all("tr"):
        cells = [clean(c.get_text(" ")) for c in tr.find_all(["th", "td"])]
        cells = [c for c in cells]
        if len(cells) >= 2 and cells[0]:
            rows[cells[0]] = cells[1] if len(cells) == 2 else cells[1:]
        elif len(cells) == 1 and cells[0]:
            rows.setdefault("_notes", []).append(cells[0])
    return rows


def abs_url(href):
    return urljoin(BASE_URL + "/", href)


# --------------------------------------------------------------------------- listing
def list_ship_urls(session, max_pages=100):
    """Walk /ships/?page=1..N and collect every /ships/<slug>/ link."""
    urls = {}
    for page in range(1, max_pages + 1):
        html = get(session, f"{LIST_URL}?page={page}")
        soup = BeautifulSoup(html, "html.parser")
        new = 0
        for a in soup.find_all("a", href=True):
            path = urlparse(abs_url(a["href"])).path
            m = SHIP_HREF.match(path)
            if m and m.group(1) not in NON_SHIP_SLUGS and path not in urls:
                urls[path] = abs_url(path)
                new += 1
        print(f"  listing page {page}: {new} new ships (total {len(urls)})", file=sys.stderr)
        if new == 0:
            break
        time.sleep(DELAY)
    return list(urls.values())


# --------------------------------------------------------------------------- ship page
def parse_ship(html, url=""):
    soup = BeautifulSoup(html, "html.parser")
    data = {"url": url, "slug": urlparse(url).path.strip("/").split("/")[-1] if url else ""}

    # ---- <head> metadata
    data["page_title"] = clean(soup.title.get_text()) if soup.title else ""
    md = soup.find("meta", attrs={"name": "description"})
    data["meta_description"] = clean(md["content"]) if md and md.get("content") else ""
    canon = soup.find("link", rel="canonical")
    if canon and canon.get("href"):
        data["canonical"] = canon["href"]
    og_img = soup.find("meta", property="og:image")

    # ---- type + manufacturer ("Submarine · Arsenal de Cherbourg (DCAN)"), before nav is removed
    data["type"] = ""
    data["manufacturer"] = ""
    cat_links = [a for a in soup.find_all("a", href=True)
                 if re.match(r"^/ships/category/[^/]+/$", urlparse(abs_url(a["href"])).path)]
    for a in cat_links:
        parent_text = clean(a.parent.get_text(" ")) if a.parent else ""
        if "·" in parent_text:
            data["type"] = clean(a.get_text())
            data["manufacturer"] = clean(parent_text.split("·", 1)[1])
            data["category_url"] = abs_url(a["href"])
            break
    if not data["type"] and cat_links:
        data["type"] = clean(cat_links[-1].get_text())
        data["category_url"] = abs_url(cat_links[-1]["href"])

    # ---- strip page chrome
    for t in soup(["script", "style", "noscript", "nav", "header", "footer", "form", "select"]):
        t.decompose()
    root = soup.find("main") or soup.body or soup

    # ---- name + summary
    h1 = root.find("h1")
    data["ship_name"] = clean(h1.get_text()) if h1 else ""
    lede = h1.find_next("p") if h1 else None
    data["summary"] = clean(lede.get_text()) if lede else ""

    # ---- hero stats (Origin / Commissioned / Updated / Displacement / Max speed / Ships built)
    lines = [clean(x) for x in root.get_text("\n").split("\n")]
    lines = [x for x in lines if x]
    stats = {}
    for i, line in enumerate(lines):
        for label in HERO_LABELS:
            if label in stats:
                continue
            if line == label:
                value_lines = []
                for nxt in lines[i + 1:i + 4]:
                    if nxt in HERO_LABELS or nxt in ("Technical Specifications",):
                        break
                    value_lines.append(nxt)
                if value_lines:
                    stats[label] = " ".join(value_lines)
            elif line.startswith(label) and label in ("Origin", "Commissioned", "Updated"):
                rest = clean(line[len(label):])
                if rest:
                    stats[label] = rest
    key_map = {"Origin": "origin_text", "Commissioned": "commissioned", "Updated": "updated",
               "Displacement": "displacement", "Max speed": "max_speed", "Ships built": "ships_built"}
    for k, v in stats.items():
        data[key_map[k]] = v

    # origin country: prefer the real /countries/<code>/ link
    data["origin"] = ""
    for a in root.find_all("a", href=True):
        m = re.match(r"^/countries/([a-z]{3})/$", urlparse(abs_url(a["href"])).path)
        if m:
            data["origin"] = clean(a.get_text())
            data["origin_code"] = m.group(1)
            data["origin_url"] = abs_url(a["href"])
            break
    if not data["origin"] and data.get("origin_text"):
        data["origin"] = re.sub(r"^[^\w]+", "", data["origin_text"]).strip()
    data.setdefault("commissioned", "")

    # ---- images
    images = []
    for img in root.find_all("img"):
        src = img.get("src") or img.get("data-src") or ""
        if "/ship/profile/" in src or "/ship/" in src and "cutout" not in src:
            images.append({"url": abs_url(src), "alt": clean(img.get("alt", ""))})
    data["image"] = (og_img["content"] if og_img and og_img.get("content")
                     else (images[0]["url"] if images else ""))
    data["images"] = images

    # ---- tables (Technical Specifications, Performance, anything else)
    tables = {}
    for i, table in enumerate(root.find_all("table"), 1):
        rows = parse_table(table)
        if rows:
            put(tables, title_for(table, f"table_{i}"), rows)
    data["tables"] = tables
    # flat copy of every spec row for easy querying
    flat = {}
    for rows in tables.values():
        for k, v in rows.items():
            if k != "_notes" and k not in flat:
                flat[k] = v
    data["specs"] = flat
    units = flat.get("Units")
    if isinstance(units, str) and units:
        data["units_list"] = [u.strip() for u in units.split(",") if u.strip()]

    # ---- every bullet list under a section title (Armament, Latest Data Updates, ...)
    lists = {}
    armament_links = []
    for ul in root.find_all(["ul", "ol"]):
        if ul.find_parent(["ul", "ol"]):
            continue
        items = [clean(li.get_text(" ")) for li in ul.find_all("li", recursive=False)]
        items = [x for x in items if x]
        if not items:
            continue
        title = title_for(ul, "list")
        put(lists, title, items)
        if title.lower() == "armament":
            for a in ul.find_all("a", href=True):
                armament_links.append({"text": clean(a.get_text()), "url": abs_url(a["href"])})
    data["lists"] = lists
    data["armament"] = lists.get("Armament", [])
    if armament_links:
        data["armament_links"] = armament_links
    if "Latest Data Updates" in lists:
        data["update_log"] = lists["Latest Data Updates"]

    # ---- description (paragraphs after the "Description" heading)
    desc, source_lines = [], []
    dh = next((h for h in root.find_all(HEADING_TAGS) if clean(h.get_text()).lower() == "description"), None)
    if dh:
        for el in dh.find_all_next(["p", "h2"]):
            if el.name == "h2":
                break
            txt = clean(el.get_text(" "))
            if not txt:
                continue
            if re.search(r"Wikipedia|Illustrations generated|Last updated on|Suggest a change", txt):
                source_lines.append(txt)
                for a in el.find_all("a", href=True):
                    if "wikipedia.org" in a["href"]:
                        data["wikipedia_url"] = a["href"]
            else:
                desc.append(txt)
    data["description"] = "\n\n".join(desc)
    if source_lines:
        data["description_source"] = " ".join(source_lines)
        m = re.search(r"Last updated on ([0-9]{1,2} \w+ [0-9]{4})", data["description_source"])
        if m:
            data["last_updated"] = m.group(1)

    # ---- FAQ
    faq = []
    fh = next((h for h in root.find_all(HEADING_TAGS)
               if "frequently asked" in clean(h.get_text()).lower()), None)
    if fh:
        for el in fh.find_all_next(["strong", "b", "h2"]):
            if el.name == "h2":
                break
            q = clean(el.get_text())
            if not q.endswith("?"):
                continue
            ans = el.find_next("p")
            faq.append({"question": q, "answer": clean(ans.get_text(" ")) if ans else ""})
    data["faq"] = faq

    # ---- operators (navies) and similar ships
    operators, seen = [], set()
    for a in root.find_all("a", href=True):
        m = re.match(r"^/navies/([a-z]{3})/$", urlparse(abs_url(a["href"])).path)
        if m and m.group(1) not in seen:
            seen.add(m.group(1))
            operators.append({"name": clean(a.get_text()), "code": m.group(1), "url": abs_url(a["href"])})
    data["operators"] = operators

    similar = []
    marker = root.find(string=re.compile(r"^\s*Similar Ships\s*$"))
    if marker:
        for a in marker.find_all_next("a", href=True):
            path = urlparse(abs_url(a["href"])).path
            m = SHIP_HREF.match(path)
            if not m or m.group(1) in NON_SHIP_SLUGS or m.group(1) == data["slug"]:
                if path.startswith(("/rankings", "/air_forces", "/page/")):
                    break
                continue
            name = clean(a.get_text())
            if name and all(s["slug"] != m.group(1) for s in similar):
                similar.append({"name": name, "slug": m.group(1), "url": abs_url(path)})
    data["similar_ships"] = similar

    data["missing_fields"] = [k for k in EXPECTED if not data.get(k)]
    return data


# --------------------------------------------------------------------------- output
def save(path, vessels, errors, total):
    payload = {
        "source": BASE_URL,
        "scraped_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "expected_count": total,
        "count": len(vessels),
        "errors": errors,
        "vessels": vessels,
    }
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
    os.replace(tmp, path)


def crawl(out_file, resume=True):
    session = make_session()
    vessels, errors, done = [], [], set()
    if resume and os.path.exists(out_file):
        with open(out_file, encoding="utf-8") as f:
            old = json.load(f)
        vessels = old.get("vessels", [])
        done = {v["url"] for v in vessels}
        print(f"Resuming: {len(done)} ships already saved", file=sys.stderr)

    print("Collecting ship URLs...", file=sys.stderr)
    urls = list_ship_urls(session)
    print(f"Found {len(urls)} ship pages", file=sys.stderr)

    for n, url in enumerate(urls, 1):
        if url in done:
            continue
        try:
            vessel = parse_ship(get(session, url), url)
            vessels.append(vessel)
            flag = f"  MISSING: {vessel['missing_fields']}" if vessel["missing_fields"] else ""
            print(f"[{n}/{len(urls)}] {vessel['ship_name'] or url}{flag}", file=sys.stderr)
        except Exception as e:  # keep going, record the failure
            errors.append({"url": url, "error": str(e)})
            print(f"[{n}/{len(urls)}] ERROR {url}: {e}", file=sys.stderr)
        if n % 25 == 0:
            save(out_file, vessels, errors, len(urls))
        time.sleep(DELAY)

    save(out_file, vessels, errors, len(urls))
    print(f"\nDone: {len(vessels)}/{len(urls)} ships, {len(errors)} errors -> {out_file}", file=sys.stderr)
    incomplete = [v for v in vessels if v.get("missing_fields")]
    print(f"{len(incomplete)} ships have at least one empty expected field", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT_FILE)
    ap.add_argument("--url", help="scrape a single ship page and print JSON")
    ap.add_argument("--html", help="parse a locally saved ship page and print JSON")
    ap.add_argument("--no-resume", action="store_true")
    args = ap.parse_args()

    if args.html:
        with open(args.html, encoding="utf-8") as f:
            print(json.dumps(parse_ship(f.read(), args.url or ""), ensure_ascii=False, indent=2))
    elif args.url:
        print(json.dumps(parse_ship(get(make_session(), args.url), args.url), ensure_ascii=False, indent=2))
    else:
        crawl(args.out, resume=not args.no_resume)


if __name__ == "__main__":
    main()
