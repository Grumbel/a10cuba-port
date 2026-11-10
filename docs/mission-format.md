# Mission database format (`Missions/*.db`)

Reverse-engineered from `A10Sim.dll` (loader `FUN_1001d850`, relocation walk
`FUN_1001e390`/`FUN_1001e640`) and the shipped files. Reader: `tools/a10db.py`
(`python3 -I tools/a10db.py FILE.db --depth 6 --props --paths`).
Status key: **verified** = matches data, **partial** = understood in outline.

Mission scenarios are not separate files: `CubaModels.db` (campaign),
`CubaNetModels.db` and `CubaTrainingModels.db` each hold a scene graph whose
`scenarios` root contains per-mission subtrees (`miss0`..`miss11`, `qs12`,
`qs13`, `net14`..`net17`). `Cuba*Terrain.db` hold terrain tile leaves.
Game data must not be committed; the parser tests use synthetic data only.

## File layout (little endian, verified)

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | magic `0x1a4f92ed` |
| 4 | u32 | data offset (always `0x14`; root node) |
| 8 | u16 | symbol table size |
| 0xa | u32 | symbol table file offset |
| 0xe | u16 | footer entry count |
| 0x10 | u32 | footer offset (6-byte entries, colour-like; role partial) |

All pointers in the file are absolute file offsets (the loader adds the
mapped base, flag bit 0 of node byte 1 marks "relocated").

**Symbols** are NUL-separated strings; a symbol id is the 16-bit byte offset
of the string in the table (`0` = first string). Unit names, model names,
property keywords, weapon names and place names all share this table.

## Nodes (verified)

Header, 0x20 bytes: `u8 type` (1 group, 2 leaf), `u8 flags`, `u16 name`
symbol, `u16 child count`, `u16 aux` (class symbol for terrain leaves),
`u32 +8` descendant/size count, `u32 +0xc` child-array offset (groups),
`u32 +0x10` **property-block pointer (0 if none)**, `+0x14..+0x1c` extra
words, last word a bounding value. Nodes are shared: one node may be
referenced by many parents (e.g. `Ga10g1`), so treat the tree as a DAG.

Group children: array of 0x24-byte entries
`i32 rot[5]; i32 x, y, z; u32 child_ptr`.
`rot` = Y-axis rotation in 2.30 fixed point: `cos, sin, 1.0, -sin, cos`
(identity = `40000000 0 40000000 0 40000000`). `x,y,z` are signed 32-bit
position units (believed feet: terrain tiles are 211200 apart = 40 miles;
**unit scale still to be confirmed in-game**).

Typical hierarchy: `scenarios → WORLD → missN → cubaN → {cubaNgM groups…}`.
`cubaN`'s children are the static world (`CUBA`: airports, cities, harbor,
trees…), friendly/enemy force groups, and path leaves `cubaNoM`. A unit
instance is `cubaNgM → model-ref group (Ga10g1) → model leaf (a10)` with the
instance's behaviour in its property block.

## Property blocks (verified for stream grammar)

A token stream of u32 words. Word = `symbol | argcount << 16`; the next
`argcount` words are raw i32 arguments. The block has no stored length; it
ends where the next structure in the file begins (the parser derives that).
Tokens with 0 args are flags or name references (e.g. `path` followed by the
path-leaf symbol `cuba0o57`).

Observed tokens (name, args): `pilot` / `chit` (AI brain: `pilot chit a10 a10`
= brain, model), `alwaysActive`, `active`, `discovered`, `detectable`,
`detector`, `radar`, `side(n)`, `token` + `tok1|tok2` + target model name,
`path <leaf>`, `skipTo(wp)` (start at waypoint), `vel0/vel/turnRate(n)`,
`vrandom(n)`, `time(h, m, s)` / `wait(h, m, s)`, `takeOff`, `targets(n)`,
`yaw(min,max)`, `spin(n)`, `directional/nondirectional(...)`, `class(n)`,
turret/weapon parameters (`rounds`, `burst`, `muzzleVel`, `rate`, `cool`).
**Loadout:** `Weapon(station, count)` (e.g. `AGM65B[3,3]` station 3 ×3;
single-arg form `Mk83[4]` = station 4, count 1; `AIM9L[11,2]`) — same
station concept as the `.lod` files. `AIM9L3/AIM9L4` and `Camera Pth1-3`
appear as zero-arg tokens. Exact semantics of `side`, `targets`, `vrandom`
are partial; compare against missions in-game to confirm.

## Path leaves (`cubaNoM`, type 2) (partial)

Leaf header as above; `+0x14/+0x18/+0x1c` point to up to three LOD vertex
blobs, `+0x20` points to a **waypoint script** stored just before LOD 0.
The script is the same token stream, keyed by vertex label:
`V1 takeOff alt(800) vel(280) V3 targets(1) V4 targets(11) V6 targets(0)
V9 vel(200) V12 land` (taken from the A-10 wingman path). Labels `V<n>` are
1-based indices into the LOD0 vertex list. Vertex data: signed 16-bit
`x, 0, z` triples (12 vertices for the example), scaled by a per-LOD
exponent (see `FUN_1001e740`); altitude comes from `alt(n)` tokens, not y.
Geometry leaves (models, terrain) share this leaf container.

## Mission editor implications

1. Edit in memory: parse tree → modify transforms / property tokens /
   path vertices → re-serialise with fresh offsets (all pointers are absolute
   file offsets; the symbol table may need new strings appended since ids
   are byte offsets; keep existing ones stable).
2. Safe first edits: unit positions/headings (child-array words), loadouts
   (`Weapon(station,count)` args), `time` and `skipTo` args, `side`. These
   keep sizes unchanged and need no repacking.
3. Adding units needs node duplication + property block growth + repack.

## Open items

- Unit scale/axes of `x,y,z` (feet assumed) and the mission-to-`missN`
  mapping (`Mission%02d` string 5010, `qs12/qs13`, `net14`..`net17`); which
  terrain DB each mode uses.
- Property-block length is not stored; the parser infers the end from the
  next known structure. The loader's real termination rule is unconfirmed.
- Footer entries (6 bytes, colour-like, tied to `mgColor`), node `+8`
  size/descendant count and the bounding word at `+0x1c`.
- LOD vertex scaling for path/geometry leaves (`FUN_1001e740`), the
  `+0x14/+0x18/+0x1c` pointer roles beyond LOD0, and leaf `+0xc/+0x10`
  values (clamped to `0x7ffffff` by the loader).
- Token semantics still unconfirmed in-game: `side`, `targets`, `vrandom`,
  `token`/`tok1`/`tok2`, `discovered`, `skipTo`, `time` vs `wait`.
- Some depth-3 walks of the `WORLD` children (`qs12`, `qs13`, others) hit
  bad pointers in an earlier ad-hoc walker; re-check with `a10db.py`.
- `.dmo` demo and `.rec` recording formats (not analysed).
- Editor: no serialiser/repacker exists yet; round-trip (parse, write,
  byte-identical) must be proven before any editing.
