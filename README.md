# Ocean Builder — desktop edition

A single-player, offline port of the maritime trading game in `../sea-builder`, written in
C++17 on [raylib](https://www.raylib.com/). It plays the same game as the web build against
the same world data: claim ports, buy hulls, plot sea routes across the globe, deliver cargo
for freight and research points, and spend those points on port technologies.

Three things differ from the web version, by design:

- **No account, no server.** The Fastify backend is gone. Progress is a local JSON save file.
- **Vessels only.** The aircraft and airport layer (`aircraftData.js`, the airport markers and
  the Air mode) is not ported. There is one transport mode: ships.
- **No world ranking.** There are no other players to rank against, so the leaderboard and the
  home/login/register screens are dropped. The game starts straight into the map.

## Layout

| Path | What it is |
| --- | --- |
| `src/geo.*` | Great-circle maths, headings, bearings, route interpolation (`sea-builder/src/geo.js`). |
| `src/sea_route.*` | The 360x720 ocean grid and Dijkstra sea routing, ported from `seaRoute.js`. A `SeaField` is one search from a departure point, so any number of destinations can be priced from it. |
| `src/data.*` | Loads the port / vessel / trade catalogs and resolves the `data/` directory. |
| `src/voyage.*` | Hull prices, vessel classes, berth rules, cargo options and the voyage economy (`voyage.js`, `cargo.js`, `Fleet.js`). |
| `src/research.*` | The four port technologies, their costs and effects (`Research.js`). |
| `src/management.*` | Fleets and the admiralty: named ship groups, flag officers and their buffs, and the research-driven fleet/ship/admiral caps. |
| `src/game.*` | The game state: money, claimed ports and the defence pool of every port under siege, the rival powers and their economies, fleet, voyages under way, research, fleets/admirals, deliveries (`UI.js` game logic plus the backend's persisted player state). |
| `src/save.*` | The local save file, replacing the HTTP API. |
| `src/fonts.*` | The UI typeface: rasterises the bundled font at the exact pixel size each label is drawn at, so text is smooth instead of a magnified bitmap. |
| `src/map_view.*` | The world map: equirectangular projection with pan and zoom, country and lake outlines, port markers, route polylines and moving vessels. |
| `src/ui.*` | Every screen and all input: top and bottom bars, the port panel, the navigation planner, the shipyard, the fleet directory, the research tree, and the naval force on the map. |
| `src/naval_view.*` | The naval hulls: bakes the bundled 3D models into a sheet of heading sprites and draws the map units and the shipyard's 3D preview. |
| `src/main.cpp` | Window setup and the frame loop. |
| `tests/logic_tests.cpp` | Headless checks for all of the above (`144/144 checks passed`). |
| `tools/frame_report.cpp` | Summarises a captured frame and reports its text quality, so rendering can be verified without a display. |
| `data/` | The world data, copied byte-for-byte from the web build, plus the UI font. |
| `third_party/json.hpp` | nlohmann/json, single header, for reading the assets and writing saves. |

## Data

`data/` holds unchanged copies of the web build's assets:

| File | Source |
| --- | --- |
| `ports.json` | `sea-builder/src/assets/ports.json` — 3,630 World Port Index rows. |
| `vessels.json` | `sea-builder/src/assets/vessels.json` — 201 hulls, including starter id 201. |
| `military_vessels.json` | `fetch_military_fleet.py` — 605 naval ship classes from globalmilitary.net, listed read-only in the fleet screen's **Naval fleets** section. |
| `sea-grid.json` | `sea-builder/src/assets/sea-grid.json` — the packed 360x720 land/water grid. |
| `trade_summary.json` | `sea-builder/src/assets/trade_summary.json` — BACI trade flows by country and good. |
| `country_codes_V202601.json` | `sea-builder/src/assets/country_codes_V202601.json` — country name lookup. |
| `countries-low.geojson`, `lakes.geojson` | `sea-builder/public/` — the Natural Earth outlines the map draws. |
| `fonts/*.ttf` | Barlow Condensed, Regular / SemiBold / Bold — see **Typography**. |
| `models/*.glb` | CC0 ship and submarine hulls (see `models/CREDITS.md`), baked into the map's naval sprites at start-up. |

The game looks for `data/` next to the executable, in the working directory, and in the source
tree; set `SEA_BUILDER_DATA` to point somewhere else.

## Typography

raylib's built-in font is a 10-pixel bitmap drawn with a point filter, so any label at another
size is a magnified bitmap and reads as blocky. The game instead ships
[Barlow Condensed](https://fonts.google.com/specimen/Barlow+Condensed) (SIL Open Font License,
`data/fonts/OFL.txt`) — a narrow grotesque of the kind grand-strategy and simulator UIs use —
and `src/fonts.cpp` rasterises it **on demand, once per weight and pixel size**, then samples it
with bilinear filtering. Because a label is rendered at exactly the size it is drawn at, there
is no upscaling anywhere: headings take the Bold cut, buttons SemiBold, body text Regular.

To change the face, drop other `.ttf` files in `data/fonts` and point `kFiles` in
`src/fonts.cpp` at them. If the files are missing the game falls back to raylib's built-in
font, so it still runs from a stripped data directory.

## Build

You need a C++17 compiler, CMake 3.16 or newer and OpenGL. raylib is used from the system when
one is installed (`find_package(raylib 5.0)`), otherwise CMake fetches and builds it.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

That produces `build/sea_builder` (Windows: `build/sea_builder.exe`), with `data/` copied
beside it. On MinGW the build links the C++ runtime statically, so a stray `libstdc++-6.dll`
from another toolchain on `PATH` cannot be picked up at startup.

## Play

The game opens on a start menu: **Play** takes command and **Quit** closes the game. Tabs and
screens are reached with the mouse — there are no number or letter shortcuts for them. The
**Main Menu** button at the bottom right returns to that menu at any time; the game pauses
while it is up, and **Play** resumes the same game.

The game opens with one harbour in hand — **Nantes, France**, granted at the start — so there is
a berth to sail from and a quay to lay hulls down at. Click a port on the map to see its
details, then open **Navigation** to plan a crossing: pick a departure you own, an arrival
anywhere, a hull and the goods to trade, and the panel prices the run — distance by sea,
crossing time, estimated freight, research points and loss risk — before you commit.

## Taking a port

A neutral harbour is not bought off the map. It has a **harbour defence** pool of its own, and
an untouched port is still somebody else's and still manning its battery:

1. Select your warships, right click the neutral port (the marker turns red) and the force is
   ordered to bombard it. The ships close on the harbour, hold station at their own firing
   range and work its defences down, while the port's own battery answers them.
2. The port panel shows the siege as a bar — **HARBOUR DEFENCES**, current pool out of full —
   and a small bar over the marker tracks it on the map.
3. When the pool reaches zero the harbour's guns fall silent, the message *defences are broken*
   is raised, and the panel's button becomes **Buy for …**. Pay the purchase price and the port
   is yours, with its own machine gun mounted.

A bigger port is a bigger job: the pool comes from the port's `health` rating and its battery
from its `attack` rating, so a naval base out-shoots and out-lasts a fishing quay. Nothing is
lost by waiting — a broken harbour stays broken, and its defences survive a save.

| Input | Action |
| --- | --- |
| Left click | Claim or select a port; select a naval unit; buttons and lists. |
| Left drag (open water) | Sweep a box that selects every naval unit inside it. `Shift` adds to the selection. |
| Right click the map | Order the selected naval units to that point, or — on a port you do not own — to bombard it. |
| Right / middle drag | Pan the map. |
| Mouse wheel | Zoom at the cursor. |
| Arrow keys / WASD | Pan the map. |
| `Esc` | Close the navigation panel, then the port panel, then back to the map. |
| `F5` or `Ctrl+S` | Save. |
| `F9` | Load. |

Voyages run in real time: a hull under way advances along its plotted route and delivers when
it arrives, paying freight and research points. Manage the fleet from the **Fleet** tab, spend
points on berth depth, cargo handling, navigation and loss reduction in **Research**.

## Fleets and admirals

The **Admiralty** tab is the navy's command screen. It has two halves:

- **Fleets** — raise a named fleet, drag its ships in from the independent list, rename or
  disband it, and see a live recap: ship count, pooled health, power, whether the fleet is at
  sea, in combat, docked or damaged, and which admiral commands it. Each fleet card carries
  its admiral's portrait.
- **Admiralty** — the full flag-officer roster with portraits. Admirals are recruited straight
  into a fleet, never into an empty pool: pick the target fleet, pay the signing fee and the
  officer takes command immediately. An admiral commands one fleet at a time and boosts every
  ship in it (weapon range, damage, repair speed, fleet speed or hull defense, depending on the
  officer). Dismissing them ends the bonus; releasing them from an unassigned post frees the
  admiral slot.

The base navy may keep 2 fleets of 4 ships and 2 admirals. **Fleet Command** adds one fleet
slot per level, **Naval Organization** adds two ships per fleet per level, and **Admiral
Corps** adds one admiral per level; all three are in the **Research** tree.

Selecting any ship selects its whole fleet, and fleet orders are given to every member at
once. Move orders are laid out as a block formation (up to three ships wide) instead of
stacking every hull on the clicked point, and a fleet concentrates its fire on one target.
The map draws a Stellaris-style nameplate for a fleet with the admiral's portrait and name;
the selected-force panel in the map shows the same flag officer and their bonus.

## Naval units

Fit a port with the **Shipyard** module and its port panel grows a *Shipyard* button. The
shipyard screen lists the 605 naval classes: filter them, hover one to see its specs and a
spinning 3D hull, and click it to lay that hull down for its build cost (nations laying down
their own classes pay 20% less). The new ship appears in the water beside the port.

Those ships are played on the map like units in a real-time strategy game. Left click one to
select it, shift-click to add others or drag a box to sweep up a task force; right click open
water to send the selection there, right click an enemy hull to attack it, or right click a port
you do not own to put it under siege (see **Taking a port**). Each hull picks
its own course: a straight line when the whole crossing is open water, otherwise the sea
router takes it around the land, so an order from the Mediterranean to a Chinese berth goes
by the Cape rather than over Africa. The selected force gets a standing-orders panel with
*Hold Position*, *Return to Port* (which also rearms at the home yard) and *Engage Nearest*,
plus the firing-order and posture rows. Guns, machine guns, rockets, missiles and torpedoes
are tracked as separate mounts: each has its own magazine and reload, its own reach and its
own animation (tracers, motor trails, wakes, fireballs), and the firing order decides which
categories may open up. Offensive ships return fire at anything in reach, defensive ships
only answer whoever hit them, and aggressive ships chase the nearest boat in their patrol
radius. A rival power's hulls fight the same way: each one hunts the nearest of your ships that
comes into its own waters, steams to the edge of its own weapon range under its own power and
shoots from there, and backs off if you get inside that range, so enemy fire opens at a
distance instead of only at boarding range. A hull that is hit from beyond its lookouts' reach
still turns on the shooter.

Ships are drawn from the 3D models in `data/models`, baked at start-up into 24 heading sprites
each. Swapping in different hulls is a matter of dropping `.glb` files over the same names.

## Rival powers

Three opponents share the world with you, and they are players rather than a raider spawn:
**Somali Pirate** in the Gulf of Aden, the **Barbary corsairs** off the Maghreb and **Ching
Shih** in the South China Sea. Each is handed the real harbour nearest its own waters at the
start of a game — the marker is red, and the port panel names the power that holds it — and
then lives off the same economy you do:

- **Income.** Every port it holds pays it the same per-port rate yours pay you. Taking one of
  its harbours therefore costs it money as well as a base.
- **Trade.** It dispatches merchant crossings from its ports to the wider world, priced by the
  same voyage economy — the same freight, the same loss risk, the same research points per
  crossing. Their routes are drawn on the map in the same dashed language as yours, with a red
  hull crawling along them, so an opponent's shipping can be watched and raided.
- **Shipbuilding.** It lays hulls down at its own yard, buying the best class its treasury can
  afford while keeping a reserve. A rival's ships are catalogue classes with their real mounts,
  and a sunk hull is a hull it has to pay for again.
- **Research.** The points its crossings earn go into the same technology tree the player has,
  which raises its freight and shortens its crossings just as it does for you.

Its navy grows to two hulls per port it holds, plus a flagship, so a power that has lost its
harbours cannot keep a fleet at sea. Pressing and taking a rival's ports, and their fleets
coming for yours, is the next step of this layer.

Saves are written to `savegame.json` beside the executable when that directory is writable, or
otherwise to `sea-builder-save.json` in your home directory.

## Tests

```sh
cd build && ctest --output-on-failure     # or: ./logic_tests
```

The suite covers the ported logic only — no windowing — and includes golden sea-route distances
generated from the original JavaScript router, so the C++ Dijkstra is checked against the web
build's own numbers rather than against itself.

## Automation

The game can drive and capture itself, which is how it is verified without a display:

```sh
SEA_BUILDER_DEMO=1 SEA_BUILDER_START_VOYAGE=1 SEA_BUILDER_SCREEN=nav \
SEA_BUILDER_SCREENSHOT=frame.png SEA_BUILDER_FRAME=90 ./sea_builder
```

| Variable | Effect |
| --- | --- |
| `SEA_BUILDER_DEMO` | Claims two ports, plots the longest reachable crossing for the starter hull and primes the planner. |
| `SEA_BUILDER_START_VOYAGE` | Dispatches that route through the normal navigation code path. |
| `SEA_BUILDER_SCREEN` | `map`, `nav`, `port`, `modules`, `siege`, `armement`, `shipyard`, `navy`, `combat`, `sprites`, `fleet`, `vessel`, `admiralty`, `fleetcommand` or `research`. |
| `SEA_BUILDER_SCREENSHOT` | Writes the frame at `SEA_BUILDER_FRAME` (default 40) to this path. |
| `SEA_BUILDER_FRAMES` | Stops after this many frames. |
| `SEA_BUILDER_WIDTH` / `SEA_BUILDER_HEIGHT` | Window size, defaulting to 1440x860. Set them when running on a smaller display: a drawable smaller than the window clips the right-hand panels out of every capture. |

The `port` screen also upgrades the selected port once and fits a module, so a capture shows a
filled slot beside the empty ones; `modules` opens the fit-a-module list on top of that (a
screenshot cannot click the empty slot itself). `armement` opens the port's armament screen
with one weapon already mounted (run it with `SEA_BUILDER_DEMO=1`, which owns a port).
`shipyard` fits the shipyard module and opens the shipyard screen on an owned port (run it with
`SEA_BUILDER_DEMO=1`, which owns a port), and parks the cursor on the first class so the detail
card and its 3D well are populated. `siege` lays a destroyer down at the home yard and sends it
at the nearest small neutral harbour, so a capture shows a contested port: the red marker, the
defence bar over it and a port panel that refuses to sell the harbour yet. `navy` claims a yard, fits a shipyard and lays down a small
task force, then orders it to sea. `combat` claims a yard near a rival hull, builds a class
carrying several weapon types and orders it to attack, so a capture shows the target marker,
rounds in flight and their trails. `sprites`
lays out every baked hull sprite beside its compass bearings, so the model pipeline can be
checked from a capture.

Each frame it also prints the live game state, so a scripted run can be checked for the
delivery paying out: money rises and the voyage count drops back to zero.

```sh
cd build && ./frame_report ../frame.png
```

`frame_report` (built from `tools/frame_report.cpp`) loads a captured frame and reports its
content box, its distinct colour count and its dominant colours, which is enough to tell a
rendered screen from a blank one. It also reports two text-quality numbers — the number of
distinct colours and `flat rows`, the share of pixel rows identical to the row above — can
print a region as luminance characters, so a glyph can be read rather than only measured, and
can count pixels of an exact colour, which is how the map markers were checked:

```sh
cd build && ./frame_report post_map.png --ascii 10 220 170 26      # read a label, magnified
cd build && ./frame_report post_map.png --count 4080EB:port        # 20,738 px of port markers
```

Text quality is how the typography was checked: over the same frame, the built-in bitmap font
gives 34–354 distinct colours and up to 79% flat rows, the bundled font 1,580–4,329 colours and
at most 34% flat rows on the text-heavy screens — antialiased outlines rather than magnified
pixels. Reproduction recipe: run the capture twice, once with `data/fonts` moved aside (in both
`data/` and `build/data/`, since the game falls back to the source tree), and compare the two
frames.
