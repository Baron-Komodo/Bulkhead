# Naval hull models

The shipyard's 3D hulls and the map's naval sprites are baked from these models at start-up.
Every one is released under **CC0 1.0 (public domain)** — no attribution is required, but the
sources are recorded here anyway.

| File | Model | Author | License | Source |
| --- | --- | --- | --- | --- |
| `ship.glb` | Ship | Kenney | CC0 1.0 | https://poly.pizza/m/SPxFN3Oazd |
| `small_ship.glb` | Small Ship | Quaternius | CC0 1.0 | https://poly.pizza/m/W2vMzztgIi |
| `submarine.glb` | Sub | Kenney | CC0 1.0 | https://poly.pizza/m/nPAJwSFIHb |
| `lifeboat.glb` | Lifeboat | Quaternius | CC0 1.0 | https://poly.pizza/m/Bkd4KKQA4O |

The models are used as follows (`navalModelKindFor` in `src/game.cpp`):

* **Ship** — carriers, battleships, cruisers, destroyers, frigates, corvettes.
* **SmallShip** — patrol and missile craft, mine warfare, amphibious and landing ships.
* **Submarine** — every submarine class.
* **Auxiliary** — everything else (rescue, survey, tenders, replenishment).

To swap a hull, drop a replacement `.glb`/`.gltf`/`.obj` over the same file name. `NavalView`
centres each model on its own bounding box, scales it to a common length and bakes 24 heading
sprites from a fixed aerial camera, so a replacement of any size or unit scale just works.


https://sofa499.itch.io/vanillaplus - for vessel model
https://artpirates.itch.io/free-49-military-weapons-pack - for icons
