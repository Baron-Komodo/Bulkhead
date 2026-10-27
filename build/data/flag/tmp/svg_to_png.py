#!/usr/bin/env python3
"""Rasterise the flag SVGs in this folder into the game's flag database.

Every country / territory SVG in this directory is rendered to a 640x480 PNG in
``../`` (``data/flag``), named the way the game looks flags up: the country's
English name (``Morocco.png``), matching the names the existing files use and the
names in ``military_vessels.json``'s ``origin`` field.

Non-country art is deliberately left alone: the organisation emblems (Arab
League, ASEAN, CE/FTA, EAC, EU, UN, Pacific Community), the subnational flags
(Catalonia, Galicia, Basque Country, Wales, Scotland, England, Northern Ireland,
Canary Islands) and flag-icons' two-letter sub-codes that duplicate a flag the
game already reaches by name (Ascension, Saint Helena itself, Tristan da Cunha).

Run it from this directory: ``python svg_to_png.py``.
"""

import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, ".."))
SIZE = ("640", "480")

# ISO 3166-1 alpha-2 (plus the reserved codes flag-icons ships) -> file name.
NAME = {
    "ad": "Andorra",
    "ae": "United Arab Emirates",
    "af": "Afghanistan",
    "ag": "Antigua and Barbuda",
    "ai": "Anguilla",
    "al": "Albania",
    "am": "Armenia",
    "ao": "Angola",
    "aq": "Antarctica",
    "ar": "Argentina",
    "as": "American Samoa",
    "at": "Austria",
    "au": "Australia",
    "aw": "Aruba",
    "ax": "Åland Islands",
    "az": "Azerbaijan",
    "ba": "Bosnia and Herzegovina",
    "bb": "Barbados",
    "bd": "Bangladesh",
    "be": "Belgium",
    "bf": "Burkina Faso",
    "bg": "Bulgaria",
    "bh": "Bahrain",
    "bi": "Burundi",
    "bj": "Benin",
    "bl": "Saint Barthélemy",
    "bm": "Bermuda",
    "bn": "Brunei",
    "bo": "Bolivia",
    "bq": "Bonaire",
    "br": "Brazil",
    "bs": "Bahamas",
    "bt": "Bhutan",
    "bv": "Bouvet Island",
    "bw": "Botswana",
    "by": "Belarus",
    "bz": "Belize",
    "ca": "Canada",
    "cc": "Cocos Islands",
    "cd": "Democratic Republic of the Congo",
    "cf": "Central African Republic",
    "cg": "Congo",
    "ch": "Switzerland",
    "ci": "Côte d'Ivoire",
    "ck": "Cook Islands",
    "cl": "Chile",
    "cm": "Cameroon",
    "cn": "China",
    "co": "Colombia",
    "cp": "Clipperton Island",
    "cr": "Costa Rica",
    "cu": "Cuba",
    "cv": "Cabo Verde",
    "cw": "Curaçao",
    "cx": "Christmas Island",
    "cy": "Cyprus",
    "cz": "Czech Republic",
    "de": "Germany",
    "dg": "Diego Garcia",
    "dj": "Djibouti",
    "dk": "Denmark",
    "dm": "Dominica",
    "do": "Dominican Republic",
    "dz": "Algeria",
    "ec": "Ecuador",
    "ee": "Estonia",
    "eg": "Egypt",
    "eh": "Western Sahara",
    "er": "Eritrea",
    "es": "Spain",
    "et": "Ethiopia",
    "fi": "Finland",
    "fj": "Fiji",
    "fk": "Falkland Islands",
    "fm": "Micronesia",
    "fo": "Faroe Islands",
    "fr": "France",
    "ga": "Gabon",
    "gb": "United Kingdom",
    "gd": "Grenada",
    "ge": "Georgia",
    "gf": "French Guiana",
    "gg": "Guernsey",
    "gh": "Ghana",
    "gi": "Gibraltar",
    "gl": "Greenland",
    "gm": "Gambia",
    "gn": "Guinea",
    "gp": "Guadeloupe",
    "gq": "Equatorial Guinea",
    "gr": "Greece",
    "gs": "South Georgia and the South Sandwich Islands",
    "gt": "Guatemala",
    "gu": "Guam",
    "gw": "Guinea-Bissau",
    "gy": "Guyana",
    "hk": "Hong Kong",
    "hm": "Heard Island and McDonald Islands",
    "hn": "Honduras",
    "hr": "Croatia",
    "ht": "Haiti",
    "hu": "Hungary",
    "id": "Indonesia",
    "ie": "Ireland",
    "il": "Israel",
    "im": "Isle of Man",
    "in": "India",
    "io": "British Indian Ocean Territory",
    "iq": "Iraq",
    "ir": "Iran",
    "is": "Iceland",
    "it": "Italy",
    "je": "Jersey",
    "jm": "Jamaica",
    "jo": "Jordan",
    "jp": "Japan",
    "ke": "Kenya",
    "kg": "Kyrgyzstan",
    "kh": "Cambodia",
    "ki": "Kiribati",
    "km": "Comoros",
    "kn": "Saint Kitts and Nevis",
    "kp": "North Korea",
    "kr": "South Korea",
    "kw": "Kuwait",
    "ky": "Cayman Islands",
    "kz": "Kazakhstan",
    "la": "Laos",
    "lb": "Lebanon",
    "lc": "Saint Lucia",
    "li": "Liechtenstein",
    "lk": "Sri Lanka",
    "lr": "Liberia",
    "ls": "Lesotho",
    "lt": "Lithuania",
    "lu": "Luxembourg",
    "lv": "Latvia",
    "ly": "Libya",
    "ma": "Morocco",
    "mc": "Monaco",
    "md": "Moldova",
    "me": "Montenegro",
    "mf": "Saint Martin",
    "mg": "Madagascar",
    "mh": "Marshall Islands",
    "mk": "North Macedonia",
    "ml": "Mali",
    "mm": "Myanmar",
    "mn": "Mongolia",
    "mo": "Macau",
    "mp": "Northern Mariana Islands",
    "mq": "Martinique",
    "mr": "Mauritania",
    "ms": "Montserrat",
    "mt": "Malta",
    "mu": "Mauritius",
    "mv": "Maldives",
    "mw": "Malawi",
    "mx": "Mexico",
    "my": "Malaysia",
    "mz": "Mozambique",
    "na": "Namibia",
    "nc": "New Caledonia",
    "ne": "Niger",
    "nf": "Norfolk Island",
    "ng": "Nigeria",
    "ni": "Nicaragua",
    "nl": "Netherlands",
    "no": "Norway",
    "np": "Nepal",
    "nr": "Nauru",
    "nu": "Niue",
    "nz": "New Zealand",
    "om": "Oman",
    "pa": "Panama",
    "pe": "Peru",
    "pf": "French Polynesia",
    "pg": "Papua New Guinea",
    "ph": "Philippines",
    "pk": "Pakistan",
    "pl": "Poland",
    "pm": "Saint Pierre and Miquelon",
    "pn": "Pitcairn Islands",
    "pr": "Puerto Rico",
    "ps": "Palestine",
    "pt": "Portugal",
    "pw": "Palau",
    "py": "Paraguay",
    "qa": "Qatar",
    "re": "Réunion",
    "ro": "Romania",
    "rs": "Serbia",
    "ru": "Russia",
    "rw": "Rwanda",
    "sa": "Saudi Arabia",
    "sb": "Solomon Islands",
    "sc": "Seychelles",
    "sd": "Sudan",
    "se": "Sweden",
    "sg": "Singapore",
    "sh": "Saint Helena",
    "si": "Slovenia",
    "sj": "Svalbard and Jan Mayen",
    "sk": "Slovakia",
    "sl": "Sierra Leone",
    "sm": "San Marino",
    "sn": "Senegal",
    "so": "Somalia",
    "sr": "Suriname",
    "ss": "South Sudan",
    "st": "São Tomé and Príncipe",
    "sv": "El Salvador",
    "sx": "Sint Maarten",
    "sy": "Syria",
    "sz": "Eswatini",
    "tc": "Turks and Caicos Islands",
    "td": "Chad",
    "tf": "French Southern Territories",
    "tg": "Togo",
    "th": "Thailand",
    "tj": "Tajikistan",
    "tk": "Tokelau",
    "tl": "Timor-Leste",
    "tm": "Turkmenistan",
    "tn": "Tunisia",
    "to": "Tonga",
    "tr": "Turkey",
    "tt": "Trinidad and Tobago",
    "tv": "Tuvalu",
    "tw": "Taiwan",
    "tz": "Tanzania",
    "ua": "Ukraine",
    "ug": "Uganda",
    "um": "United States Minor Outlying Islands",
    "us": "United States",
    "uy": "Uruguay",
    "uz": "Uzbekistan",
    "va": "Vatican City",
    "vc": "Saint Vincent and the Grenadines",
    "ve": "Venezuela",
    "vg": "British Virgin Islands",
    "vi": "United States Virgin Islands",
    "vn": "Vietnam",
    "vu": "Vanuatu",
    "wf": "Wallis and Futuna",
    "ws": "Samoa",
    "xk": "Kosovo",
    "ye": "Yemen",
    "yt": "Mayotte",
    "za": "South Africa",
    "zm": "Zambia",
    "zw": "Zimbabwe",
}

# SVGs that are not a country or a territory, so they have no place in the
# country database. Kept here so the script can assert that every SVG on disk is
# accounted for, and so the omissions are visible rather than silent.
SKIP = {
    "arab": "Arab League",
    "asean": "ASEAN",
    "cefta": "CEFTA",
    "eac": "East African Community",
    "eu": "European Union",
    "pc": "Pacific Community",
    "un": "United Nations",
    "xx": "flag-icons placeholder",
    "es-ct": "Catalonia",
    "es-ga": "Galicia",
    "es-pv": "Basque Country",
    "gb-eng": "England",
    "gb-nir": "Northern Ireland",
    "gb-sct": "Scotland",
    "gb-wls": "Wales",
    "ic": "Canary Islands",
    "sh-ac": "Ascension Island",
    "sh-hl": "Saint Helena (duplicate of sh)",
    "sh-ta": "Tristan da Cunha",
}


def render(job):
    code, name = job
    source = os.path.join(HERE, code + ".svg")
    target = os.path.join(OUT, name + ".png")
    result = subprocess.run(
        [SHARP, "-i", source, "-o", target, "resize", *SIZE],
        capture_output=True,
        text=True,
    )
    ok = result.returncode == 0 and os.path.exists(target)
    return code, name, target, ok, (result.stderr or result.stdout or "").strip()


def main():
    on_disk = {f[:-4] for f in os.listdir(HERE) if f.endswith(".svg")}
    unaccounted = on_disk - set(NAME) - set(SKIP)
    unknown = (set(NAME) | set(SKIP)) - on_disk
    if unaccounted or unknown:
        print("SVG map does not match the folder.", file=sys.stderr)
        if unaccounted:
            print("  no name mapped: " + ", ".join(sorted(unaccounted)), file=sys.stderr)
        if unknown:
            print("  mapped but missing: " + ", ".join(sorted(unknown)), file=sys.stderr)
        return 1

    before = {f for f in os.listdir(OUT) if f.endswith(".png")}
    jobs = sorted(NAME.items())
    failures = []
    with ThreadPoolExecutor(max_workers=8) as pool:
        for code, name, target, ok, message in pool.map(render, jobs):
            if not ok:
                failures.append((code, name, message))
            print(f"{'ok  ' if ok else 'FAIL'} {code}.svg -> {os.path.basename(target)}")

    rendered = {name + ".png" for _, name in jobs}
    print(f"\n{len(jobs) - len(failures)}/{len(jobs)} SVGs rendered to {OUT}")
    print(f"added {len(rendered - before)}, redrawn {len(rendered & before)}, "
          f"skipped {len(SKIP)} (emblems, subnational flags and sub-codes)")
    left = sorted(f for f in before - rendered)
    if left:
        print("left untouched: " + ", ".join(left))
    for code, name, message in failures:
        print(f"failed {code}.svg: {message}", file=sys.stderr)
    return 1 if failures else 0


SHARP = shutil.which("sharp")
if SHARP is None:
    print("sharp is not on PATH; install it with: npm install -g sharp-cli", file=sys.stderr)
    raise SystemExit(2)

if __name__ == "__main__":
    raise SystemExit(main())
