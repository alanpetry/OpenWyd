# Runtime stub audit

This audit distinguishes a migration stub from an intentionally empty base
method, an unused class, and original client code that is absent from the
available source lineage. It is based on a scan of every empty function body,
followed by call-site searches in `Projects/TMProject`, the WASM compatibility
layer, and the Linux server sources. Destructors and virtual base hooks whose
empty body is their contract are excluded from the missing-behaviour tables.

## Resolved runtime gaps

| Area | Resolution |
| --- | --- |
| D3D9 programmable skin fog | The bridge now consumes the `oFog` value emitted by the official `skinmesh1`-`skinmesh8` shaders. |
| Client screenshot | `D3DXSaveSurfaceToFile` reads the pre-Present backbuffer and writes/downloads the official 24-bit BMP format. |
| Partial socket writes | `CPSock::RefreshSendBuffer` now retains bytes from `pSendBuffer`; it previously copied unrelated receive bytes. Both overlapping buffer compactions use `memmove`. |
| Server quiz packet | The fixed-size answer payload is now built from bounded strings instead of reading 32 bytes past a single `char`. |
| Server bag/schedule paths | The bag scan always returns a value and the Zakum hour mask cannot shift outside its valid range. |

## Reachable code whose original implementation is absent

These are real product gaps, but filling them by approximation would violate
the project's requirement to preserve the official behavior. Searches across
the public repositories from the same TMProject source lineage found the same
empty methods, not an alternate implementation.

| Source | Reachability | Missing behavior |
| --- | --- | --- |
| `Mission.cpp` | The Field opens it for the head-67 mission NPC and invokes `ResultItemListSet` and `DoCombine`. | Mission result/requirement grids, inventory transfer, text and combine packet preparation. |
| `TMEffectFirework.cpp` | Instantiated by `TMHuman` and `TMScene`. | Particle creation, lifetime/update, rendering and custom firework decoding. |
| `TMFieldScene::DrawCustomFireWork` and `TMHuman::OnPacketPremiumFireWork` | Field controls call the custom path, and packet `0x3CA` dispatches the premium path. | Decoding and drawing the custom/premium firework payload. No matching producer exists in the bundled TMSrv. |
| `SControl.cpp` (`SReelPanel`) | Two instances are created by `TMFieldScene`. | Reel animation, stopping, result and jackpot update. |
| `SControl.cpp` (`SButtonBox`) | Created by `TMFieldScene`. | Page-button construction and event wiring. |
| `NewApp.cpp` web/board methods | Called from several Field actions. | External browser/board integration; no game rendering implementation is present. |
| `TMFieldScene.cpp` Toto methods | The Field dispatches the Toto selection, buy, close, tab-key and enter-key controls. | Toto selection, purchase, close and keyboard behaviour. |
| `TMFieldScene.cpp` premium NPC click | Called for the premium-NPC mouse path. | Premium-NPC interaction. |
| `TMFieldScene.cpp` packet handlers | Opcodes `0x105`, `0x106`, `0x1BF`, `0x1C1` and `0x2C8` dispatch to empty chat-parameter, gamble-result, array-request and automatic-kick handlers. | The packet-side UI/game response. None of these opcodes has a producer in the bundled TMSrv. |
| `TMHuman.cpp` guild-battle HP methods | HP update paths call two of the three methods. | Guild-battle HP/life presentation; the original source itself labels the bodies empty. |
| `TMObject::IsInCastleZone` | Called throughout target validation, combat and PvP checks. | The available source always returns false. `IsInCastleZone2` is implemented but has a different, narrow call surface. All checked client snapshots retain the same stub. |

## Server source gaps

The same empty-body scan was run over DBSrv and TMSrv. These entries are not
Linux-port omissions: the equivalent public Windows server sources contain the
same empty bodies.

| Source | Reachability | Missing behavior |
| --- | --- | --- |
| `DBSrv/CFileDB.cpp` (`SetEncPassword`) | Called by the encrypted-password message path. | Persisting or forwarding the encrypted password during server transfer. |
| `DBSrv/Server.cpp` (`ProcessMinTimer`) | Invoked by the DBSrv minute timer. | No minute-level DBSrv work is specified by the available source. |
| `TMSrv/CMob.cpp` (`ProcessorSecTimer`) | Invoked for active mobs by the second timer. | Per-mob second-timer work; only a commented counter remains. |
| `TMSrv/Server.cpp` (`ProcessBILLMessage`) | Reachable from the optional billing socket dispatcher. | External billing protocol handling. |
| `TMSrv/Server.cpp` (`SendUpdateWoteBattle`) | Called by the data-server packet handler. | The body is entirely commented, so Wote battle updates are not sent. |

`TMSrv/GuildHall.cpp` (`GuildLevelUp`, `TerritoryMob`) and
`TMSrv/Server.cpp` (`WriteArmor`, `BuildList`) are also empty, but have no call
site in this checkout.

The missing methods above must remain explicit source gaps until an official
implementation or a fully specified protocol/UI behavior is available. They
must not be replaced with invented screens, effects or rules.

## Empty but not a current migration blocker

| Source | Classification |
| --- | --- |
| `TMVideoWnd.cpp`, `DirShow.cpp` | Optional DirectShow intro. `WYD.avi` is not distributed, `OpenClip` fails, and startup continues. |
| `JBlur.cpp` | Constructed at startup, but only `InitObject` is called; no blur operation has a call site. |
| `TMEffectGold.cpp`, `TMSkillSnow.cpp`, `TMFlail.cpp` | Compiled classes with no construction/call site in this checkout. |
| `EventTranslator::OnIME`, `OnIME2`, `SetVisibleCandidateList` | Marked by the source as Chinese-client IME behavior. Browser CP1252 input uses the implemented `OnChar` path. |
| `EventTranslator::UpdateCompositionPos`, `OnLMouseReleased` | Hooks are reached, but all same-lineage sources leave them empty; input state is handled by the surrounding implemented paths. |
| `EnableSysKey`, `DisableSysKey` | The original global Windows-key hooks are empty. A browser cannot and must not install an operating-system-wide keyboard hook. |
| `NewApp::InitServerNameMR` | Declared and defined but has no call site. |
| `TMSelectServerScene::AniDemoPlayer`, `SetAlphaVirtualkey` | Declared and defined but have no call site. The actual Demo sequence uses the implemented scene paths. |
| `TMFieldScene::SetAutoOption` | Declared and defined but has no call site. |
| `TreeNode` and `SControl` empty base methods | Intentional virtual no-op defaults; concrete scene/control classes implement the paths they use. |
| `D3DXCreateFont` | No client call site; the client uses `TMFont2` and its implemented GDI/DIB bridge. |
| Base `IDirect3D9`/`ID3DXSprite` `E_NOTIMPL` templates | The concrete WASM classes override the methods used by the client; link and runtime telemetry show no unresolved dispatch. |
| DirectSound helper `E_NOTIMPL` paths | The WASM runtime uses the implemented WebAudio-backed DirectSound methods exercised by the client. |

This file tracks source completeness. Asset-format coverage is tracked
separately in `docs/wasm-asset-audit.md` and its generated JSON report.

## Source-search evidence

Exact-signature searches were checked across the public TMProject/W2PP source
lineage, including `EricSantos00/tm-project`, `lorransouza/TMProject-Free`,
`richaferreira/wyd2`, `ErickAlcan/W2PP`, `ramonchk/wyd` and related forks.
`TMEffectFirework.cpp` is byte-identical in the six repositories that carry it;
the other entries above likewise resolve to empty methods or do not exist.
They therefore remain explicit upstream-source gaps rather than being filled
with approximate rules or visuals.
