# Light Simulation Engine: Design Doc

12 November 2026

## Purpose and principles

This is a learning project: a 2D isometric engine in C that runs on MCU-class hardware and spends its budget on simulation depth, not rendering. The farming RPG exists to drive engine features and is not a product.

- **Depth per cycle:** prefer consistent, conserved, legible rules over numerical accuracy.
- **Floor first:** design to the smallest target. Raising a budget later is a constant change.
- **Cosmetic tiers only:** stronger hardware may add resolution, colour or audio quality, never gameplay.
- **Measure before extending:** nothing leaves the parked list until the fire, water and wind prototype runs on real hardware.
- **Estimates are labelled:** every timing and memory figure here is unmeasured until that prototype exists.

## Target and budgets

The floor is an RP2350-class microcontroller, with a desktop build held to the same limits.

| Item | Decision |
| --- | --- |
| Language | C; assembly only for profiled inner loops |
| Hardware floor | Dual Cortex-M33 at ~150 MHz, 520 KB SRAM, external PSRAM permitted, assets read in place from flash |
| Display | 320×240, 8-bit indexed (76.8 KB framebuffer) |
| Tick | Fixed 30 Hz |
| View | 2D isometric, diamond tiles (32×16 px proposed) |
| Simulation grid | 64×64 tiles per active map (proposed) |
| Memory | One static block: permanent, per-map and per-frame arenas, plus pools addressed by handle |
| Development host | Desktop SDL3 build with the same fixed block and no libc heap |

Framebuffer plus simulation fields come to roughly 167–183 KB of the 520 KB on-chip RAM (estimate). RAM is a tracked budget, not a design limit: features are not cut to fit it, and PSRAM or a larger MCU is the accepted remedy.

## Visual tiers and art

The outdoor world is isometric on every tier, and the tiers differ only in presentation.

| Aspect | MCU tier | Linux tier |
| --- | --- | --- |
| Resolution | 320×240 | About 480×270 to 640×360, integer-scaled |
| Colour | 8-bit indexed | The same indexed assets, composited in full colour |
| Lighting | Palette swaps and simple light radii | Soft glow from a light buffer |
| Layers | Ground and objects | Adds parallax foreground and background |
| Prop density | Capped per screen | Much higher |

- **One tile size:** both tiers use the same pixel scale, so the Linux tier shows more of the world.
- **Simulated outdoors:** modular tiles, with palette swaps and sprite variants carrying state (wetness, nutrient stress, season, growth stage).
- **Interiors and landmarks:** authored set pieces, including multi-floor cutaways, with no general strata system behind them.
- **Terrain height:** drawn as stepped tiles with side faces, so the height field is shown in a small number of visible levels.
- **Style reference:** dense, small-palette isometric pixel art with warm point lighting, somewhat less detailed.

## Architecture

Simulation and rendering are two tasks joined only by a per-frame draw list. On the RP2350 they run on separate cores, and on a single-core target they run in turn.

- **Platform layer:** about six functions (present frame, read input, audio callback, storage read/write, time), implemented once for SDL and once for the RP2350. Everything above it is portable C.
- **Memory:** no heap after start-up. Objects are referenced by handle (index + generation), never by pointer.
- **Renderer:** a software blitter into the paletted framebuffer. Draw commands carry a depth key, are sorted back to front, and the frame is fully redrawn each tick.
- **Numbers:** integer and fixed-point throughout, so results are bit-identical on every target and input can be recorded and replayed.
- **Assets:** a host-side cooker turns PNGs, maps and data tables into binary blobs usable in place. Nothing is parsed at runtime.
- **Game logic:** data tables plus a small bytecode VM or state machines, behind a narrow script API so the VM can be replaced.
- **Debug tooling:** an overlay for arena high-water marks and for viewing any simulation field.

## Simulation core

The world is a height-mapped grid of per-tile fields, stored as separate arrays and updated at different rates.

| Field | Bytes per tile | Update rate |
| --- | --- | --- |
| Height | 1 | Rare (digging, terracing) |
| Soil type and flags | 1 | Rare |
| Topsoil depth | 1 | While water flows |
| Surface water depth | 2 | Every tick while active |
| Water flux, four directions | 4–8 | Every tick while active |
| Sediment in water | 1 | Every tick while active |
| Dissolved nitrogen in water | 1 | Every tick while active |
| Sediment-bound phosphorus in water | 1 | Every tick while active |
| Heat | 1 | Every few ticks while active |
| Material and chemistry flags | 1 | On change |
| Soil moisture | 1 | Every few game minutes |
| Light received today | 1 | A few times per game day |
| Nitrogen | 1 | Hourly to daily |
| Phosphorus | 1 | Daily |
| Potassium | 1 | Daily |
| Organic matter | 1 | Daily |
| Ground cover density | 1 | Daily |
| Canopy cover | 1 | Daily |

That is 22–26 bytes per tile, or about 90–106 KB for a 64×64 map (estimate). Wind sits on a coarser grid, around 16×16 per map.

- **Scheduler:** each field declares its rate. Slow sweeps are sliced across ticks so no single tick spikes.
- **Active sets:** fast systems touch only cells on an active list, so a dry, unburning map costs almost nothing.
- **Catch-up:** unloaded maps skip the fast systems and run the slower steps with a coarse time step when revisited. Every slow integrator must stay stable at large steps.
- **Slow change in 8 bits:** small deltas round to zero and processes stall. Use sub-unit accumulators or seeded random rounding, or 16 bits for the slowest fields.

## Water

Water lives in two coupled stores: surface water, which moves fast, and soil moisture, which moves slowly.

**Surface water** uses a shallow-water pipe model on the heightfield. It conserves volume and pools in basins. Rain, channels, pumps and spells are its sources.

**Soil moisture** is one bucket per tile:

- **Infiltration:** surface water soaks in at a rate set by soil type (sand fast, clay slow) until the tile saturates.
- **Retention:** each soil type has a holding capacity and a wilting point below which plants cannot draw water.
- **Loss:** evaporation rises with heat, wind and light. Plants transpire according to species and growth stage.
- **Lateral seep:** moisture drifts slowly downslope, so hollows stay wet and ridges dry out.
- **Drainage:** saturated soil drains downward and carries nitrogen with it.
- **Waterlogging:** soil held at saturation for too long harms most crops.

This gives the player real irrigation choices: channels, terraces, raised beds, mulch and drainage.

## Soil and nutrients

Four variables per tile give crop rotation, legumes, compost and a cost for over-watering, without a full soil chemistry model.

| Variable | Behaviour | Raised by | Lowered by |
| --- | --- | --- | --- |
| Nitrogen | Mobile: moves with water. Drives leaf growth | Nitrogen-fixing crops, manure, fertiliser, decomposing residue | Crop uptake, leaching when soil drains |
| Phosphorus | Immobile: binds to soil. Drives roots and flowering | Bone meal, rock phosphate, manure | Crop uptake, erosion carrying soil away |
| Potassium | Partly mobile: leaches in sandy soil. Drives fruit quality and drought tolerance | Wood ash, potash, compost | Crop uptake (heaviest for fruiting and root crops), leaching in sand |
| Organic matter | Slow: raises water retention and releases nitrogen gradually | Residue left in the field, compost, mulch, cover crops | Tillage, erosion by surface flow |

- **Rotation emerges:** species draw different nutrient mixes and legumes restore nitrogen, so no rotation rule is coded.
- **Water couples to nutrients:** over-irrigating drains nitrogen away, which is where much of the depth comes from.
- **Mycelial network (stretch):** a per-tile mycelium density that speeds nutrient transport between neighbouring tiles. Real mycoheterotrophs such as ghost pipe take from the fungus and give nothing back, and nitrogen fixing is done by bacteria on legume roots. A plant that trades through the network would be a fictional species, which suits the setting.

## Erosion, runoff and water bodies

Flowing water carries soil and nutrients downhill into standing water, where they feed algae and affect fish. This closes the loop from field practice to water quality.

- **Topsoil:** each tile has a topsoil depth separate from terrain height. Erosion strips topsoil and deposition builds it, so terrain shape changes only when the player digs.
- **Erosion:** flowing water has a sediment capacity that rises with speed and slope. Below capacity it picks up topsoil, above capacity it deposits. Plant cover, mulch and organic matter reduce pick-up, while tillage and bare soil increase it.
- **What moves:** dissolved nitrogen travels in the water. Phosphorus and organic matter travel bound to sediment. Potassium is lost by leaching only.
- **Player responses:** contour planting, terraces, cover crops and buffer strips beside water.
- **Water bodies:** connected standing water is one body with pooled state: volume, depth zones, temperature, nitrogen, phosphorus, algae and dissolved oxygen. It updates daily.
- **Algal blooms:** algae grow with phosphorus (usually the limiting nutrient in fresh water), nitrogen, light and warmth. A bloom's die-off consumes oxygen and can kill fish.
- **Fish:** each body holds populations by species, using the same response bands as plants (temperature, oxygen, depth, food). A catch is sampled from the population at cast time, and no individual fish are simulated.
- **Map edges:** water leaves through outflow tiles. Carrying pollution between maps is parked.
- **Conservation checks:** debug builds total water, sediment, nitrogen and phosphorus across soil, water, plants and bodies every game day. Any drift is a bug.

## Light, temperature and climate

Climate is a per-map record, weather is generated daily from it, and light and temperature reach plants as per-tile daily totals.

- **Climate zone:** latitude (day length and sun height by season), a seasonal temperature curve, a rainfall regime and altitude.
- **Weather:** a daily generator picks rain, cloud cover, a temperature offset and wind direction. A two-state wet/dry Markov chain is the usual simple method in agricultural weather generators.
- **Light:** sun direction comes from hour and season. Shade is cast by terrain and tall objects with a sweep across the heightfield along the sun direction, recomputed a few times per game day. Each tile accumulates a daily light total.
- **Temperature:** a map-wide base from climate and season, lowered with altitude, plus local heat from the chemistry system.
- **Thermal time:** plants accumulate growing degree days, so the same crop matures at different speeds in different climates. Frost and heat stress are thresholds on temperature.

## Plants

A plant is a row in a species table plus 4–6 bytes of instance state; all behaviour comes from how the species' response bands meet the tile's fields.

| Species parameter | Meaning |
| --- | --- |
| Moisture band | Wilting point, optimum low, optimum high, waterlogged |
| Light band | Minimum and optimum daily light, shade tolerance |
| Temperature | Base, optimum and maximum, frost hardiness, thermal time to each stage |
| Nutrient demand | Nitrogen, phosphorus and potassium drawn per stage |
| Transpiration | Water drawn per stage |
| Soil effects | Nitrogen fixed per day, residue returned as organic matter |

- **Growth rule:** each slow step, potential growth from the day's thermal time is scaled by the most limiting of moisture, light, nutrients and temperature (the law of the minimum).
- **Stress:** time spent outside a band accumulates and lowers yield and quality, so a badly sited crop is poor, not just late.
- **Instance state:** species, accumulated thermal time, health and a yield modifier.
- **Legibility:** wilting, yellowing from nitrogen shortage and stunting appear as palette swaps or sprite variants. A soil probe tool and field overlays expose the numbers.

## Fauna hooks

Fauna is parked until plants complete a full crop cycle, but four hooks go in now so it is not a retrofit.

- **One flux interface:** every add or remove on a field goes through a call the conservation accounting sees. Grazing and manure are then just more callers.
- **Consumable biomass:** plants expose how much can be eaten, not only a growth stage.
- **Generic organism table:** the response-band mechanism is named for organisms, since plants, fish and later animals all use it.
- **Ground cover as a field:** grass and weeds are a per-tile density. Only crops and trees are instances.

When fauna arrives it takes two forms. Populations (pests, pollinators, soil life) are density fields. Individuals (livestock, wild animals) are entities with needs, capped at dozens, which collapse into population statistics when their map is unloaded.

Vertical habitat starts as attributes, not geometry: a per-tile canopy cover value derived from trees, and burrows as shelter sites that hide an animal from the surface. Two spare bits for a stratum (underground, ground, canopy) are reserved in the entity record and the draw key.

## Chemistry and wind

A small set of generic rules acts on material properties, so interactions emerge instead of being authored pair by pair.

- **Material table:** flammability, conductivity, porosity and ignition point. Tiles, entities and items all use it, so a wooden tool burns and a metal one conducts.
- **Rules on properties:** written against properties, never against specific pairs of things.
- **Spells as injectors:** a spell adds heat, water or charge to the fields and the rules do the rest.
- **Wind:** a coarse vector field deflected by terrain and updated slowly. It drives fire spread, evaporation, and seed and smoke drift.
- **Damping:** fuel depletion and moisture stop fire from running away.
- **Kinematics:** projectiles with a fake height axis, knockback and pushable objects. No constraint solver.

## Build order

Step 8 is the gate: it produces the first real measurements and decides what leaves the parked list.

1. Platform interface, SDL host and a fixed-timestep loop
2. Arenas, pools, handles and the memory overlay
3. Paletted framebuffer, clipped colour-keyed blits and the depth-sorted draw list
4. Minimal asset cooker
5. Isometric heightfield tilemap, camera and one moving entity
6. Port to RP2350 hardware, with the renderer on core 1
7. Field system, scheduler and field overlay
8. Prototype: fire, surface water and wind on one map
9. Soil moisture, then nutrients, light and climate, then erosion, runoff and water bodies
10. Plants from the species table, and one full crop cycle with save and load
11. Inventory UI, one spell and one NPC dialogue

## Parked extensions

Each item stays parked until its trigger is observed in measurements.

| Extension | Trigger to revisit |
| --- | --- |
| External PSRAM | On-chip RAM is exceeded (expected, and accepted in advance) |
| Cortex-M7 class part | CPU is the measured limit, or larger grids are wanted |
| Second MCU as video processor | The renderer no longer fits on core 1 |
| Full 2D fluid solver | The pipe model proves too shallow in play |
| Lua scripting | Authoring in the bytecode VM becomes the bottleneck |
| Mycelial network | The base nutrient model is tuned and stable |
| 90° camera rotation | The isometric view hides too much |
| FPGA rasteriser and 3D camera | Sequel project, once the simulation is worth viewing from another angle |
| Fauna: population fields first, then individual agents | Plants complete a full crop cycle |
| Fixed strata (underground, ground, canopy) that entities can move between | Fauna or gameplay needs animals or the player inside canopy or burrows |

## Risks and open questions

The largest risk is a simulation that is deep but invisible or untunable, not one that is too slow.

- **Legibility:** the player must be able to read wet, dry, hot and depleted on a low-resolution paletted screen.
- **Tuning load:** many coupled parameters. A headless host build that runs whole seasons in seconds and dumps field statistics is needed early.
- **Catch-up accuracy:** coarse steps on unloaded maps will diverge from fine steps. Decide how much divergence is acceptable.
- **Quantisation:** 8-bit fields can stall slow processes.
- **RAM:** on-chip RAM will probably be exceeded. PSRAM is slower than SRAM, so hot data (framebuffer, active water and heat) stays on-chip and the slow soil fields move out.
- **Save size:** raw fields are roughly 90–106 KB per map, so saves need run-length compression and a storage budget.

Open questions:

- [ ] Simulation grid size, and whether one map or several are simulated at once
- [ ] Isometric tile size
- [ ] Time scale: real seconds per game day
- [ ] Whether a water table is needed or lateral seep is enough
- [ ] Storage medium: on-board flash or SD card
- [ ] One heightfield only, or stacked levels for bridges and caves
