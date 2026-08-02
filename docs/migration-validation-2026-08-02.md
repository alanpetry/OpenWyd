# Migration validation — 2026-08-02

This report records the broad deterministic Windows-versus-WASM audit. The
external repositories were used only for source-lineage research; no external
gameplay or rendering code was imported.

## Coverage and result

| Surface | Coverage | Result |
| --- | ---: | --- |
| Skills | 122 scenarios: IDs 0–116 and 151–155, with the official attack packet and class-appropriate equipment | 122 completed, identical injected-packet hashes, no crash, `glErrorTotal=0`, pixel threshold passed. IDs 106, 112, 154 and 155 made no material standalone visual change because their effect is conditional/passive. |
| NPC models | 134 distinct valid face/model IDs decoded from 676 TMSrv NPC records | 133 pixel passes. Face 239 (`Draco_Lich`) exposed a real WebGL divergence in the wing. |
| NPC functions | All merchant/control IDs dispatched by `_MSG_SingleControl.cpp` were classified | Includes ordinary/skill merchants, dragons, Carbuncle, captains, Jeffi, Shaman, Zakum quest, training/Odin, Black Oracle, mount master, kings, broker, government, Uxmal and Urnammu. |
| Maps | Outdoor plus Desert 3 and Dungeons 1, 2, 4 and 5 | All six loaded real terrain, objects, lighting and HUD and passed the pixel threshold. One Dungeon 5 native load timed out once and passed immediately on rerun. |
| Items/effects | 24 representative material, overlay, world and special items, captured at frames 1, 12 and 30 | All stable captures at frames 12/30 passed. Some frame-1 native resources were still dark/unloaded while WASM had already loaded them; both converged by frame 12. |
| Quest UI | Four official tabs opened through the real Field control path | Layout and entries match. Pixel review is caused mainly by the remaining text/font raster difference. The detail pane is blank until an entry is selected in both clients. |
| Movement stop | Frames 0–30 densely around route completion, then 31–42, 44, 48, 52 and 60 | Position, animation indices, pose hash and moving flag matched at every transition. The previously reported last-step animation glitch did not reproduce in this build. |
| Permanent regression | 39 named Field, isolated, class, mount, fairy, item, map, quest and scene scenarios | Packet hashes matched and WebGL errors stayed at zero. Only quest text and the known teleport dialog exceeded automatic review thresholds. |

The skill table contains 151 rows (IDs 0–150). IDs 0–102 are named, 103 is
empty, and 104–150 are generic "Nova skill" rows; the client has explicit
branches for 104–116 and special IDs 151–155. The codebase contains 24 concrete
`TMSkill` subclasses.

## Confirmed divergences

### Draco Lich wing

The face-239 native and WASM states match: same class, skin type, pose hash,
camera, matrices, textures and draw ordering. The WASM wing contains jagged
dark crosshatch patches while native renders a uniform red sheet. Draws 77 and
78 use `dr010103.msh`/`dr010203.msh` and `dr010103.wys`. The second mesh has 295
vertices, 410 triangles, 14 palette entries and four influences, including
coplanar duplicate-position faces with opposing normals. The evidence points
to depth/rasterization interaction on duplicated coplanar geometry, not a
missing asset or skinning formula. No global cull/depth workaround was applied.

Artifact: `artifacts/openwyd_lab/audit_npc_face_0239/20260802T141605.427417Z`.

### First-frame item loading

Some special/world item resources render immediately in WASM but appear dark
or incomplete in the first native frame. By frames 12 and 30 both clients pass.
This is a resource scheduling/lazy-load timing difference and should be fixed
at the loading boundary rather than hidden with a visual tolerance.

## Source-lineage research

- [EricSantos00/tm-project](https://github.com/EricSantos00/tm-project) is the
  strongest public reference for the decompiled TMProject client lineage. The
  local Field source has about 99.4% line coverage against it; the differences
  are primarily OpenWyd migration work.
- [lorransouza/TMProject-Free](https://github.com/lorransouza/TMProject-Free)
  is a 2023 bulk package with client, server and assets. Its client is the same
  family but includes custom shop/drop/cash changes; its server is a different
  DataServer/GameServer lineage.
- [devMaikel/Projeto-Novo-Client-TM](https://github.com/devMaikel/Projeto-Novo-Client-TM)
  is a later bulk snapshot with essentially no useful development history. Its
  extra code is not evidence of a newer official client baseline.
- [MarcoPolo-development/WYD-NewWorld_b](https://github.com/MarcoPolo-development/WYD-NewWorld_b)
  and [richaferreira/wyd2](https://github.com/richaferreira/wyd2) are active
  same-family projects, but explicitly add or overhaul systems such as VIP,
  auctions, titles, loot boxes, new interfaces and persistence. They are useful
  behavioral references, not authoritative official-source upgrades.
- [Jean1dev/w2pp-OpenWYD](https://github.com/Jean1dev/w2pp-OpenWYD) is the most
  active related server found, but its current direction is a Go/PostgreSQL/gRPC
  big-bang rewrite for the 7.662/7.640 protocol family. It is neither a drop-in
  replacement nor the same preservation strategy as this project.
- The older `ErickAlcan/W2PP` and `ramonchk/wyd` C++ servers share the same
  classic `CFileDB` root. The local server already contains later changes plus
  the Linux port; rebasing wholesale would lose work and import custom rules.

No public repository found is both newer and a clean official-source baseline
for this exact client. `TMEffectFirework.cpp` is byte-identical in every checked
repository that carries it, and the mission implementation remains absent, so
those gaps cannot be recovered by copying from a supposedly newer fork.

## DBSrv responsibilities

DBSrv is a global account/session/coordinator service, not merely an account
file reader and writer. Its active responsibilities are:

1. Accepting TMSrv connections, validating server identity and routing the
   legacy packet protocol.
2. Authenticating accounts, enforcing block/password/temp-key rules, preventing
   duplicate online sessions and tracking online accounts and MAC counts.
3. Creating, selecting and deleting characters, enforcing globally unique
   names, applying base stats and maintaining the character-name index.
4. Persisting the full player state: MOB data, equipment, inventory/cargo,
   affects, skills, coin/donation data and daily-quest state.
5. Coordinating global guild ownership, alliances, wars, fame and guild-info
   broadcasts.
6. Relaying global notices/chat and handling optional administration messages
   such as character transfer, account lookup/edit, disable/enable and donation
   imports.
7. Managing archived/capsule characters, primary-account-by-MAC state, global
   EXP ranking and item/day statistics.
8. Running periodic imports, connection/status exports, ranking reloads, daily
   logs and weekly guild-fame resets.

Persistence is currently sharded flat files and direct legacy structure I/O.
The SQLite wrapper and its startup call are commented out. `ImportDonate` is
also commented/no-op, the minute timer is empty, and the encrypted-password
server-transfer path reaches the empty `SetEncPassword`. Direct structure I/O,
plaintext legacy credentials and raw numeric tokens are important future
security/ABI debts. World simulation, mob AI, combat and map rules remain in
TMSrv, not DBSrv.

## Remaining upstream gaps

The full classification is maintained in `docs/runtime-stub-audit.md`. The
highest-impact hard gaps are Mission UI/combination, normal and custom
fireworks, reel/jackpot UI, guild-battle HP UI, castle-zone classification,
premium NPC/Toto behavior and Wote battle updates. These require an official
binary/protocol/UI reference or an explicit product decision. Implementing them
from appearance alone would create non-official behavior.
