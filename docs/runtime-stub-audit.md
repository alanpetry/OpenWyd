# Runtime stub audit

This audit distinguishes a migration stub from an intentionally empty base
method, an unused class, and original client code that is absent from the
available source lineage. It is based on call-site searches in `Projects/TMProject`,
the WASM compatibility layer, and the Linux server sources.

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
| `SControl.cpp` (`SReelPanel`) | Two instances are created by `TMFieldScene`. | Reel animation, stopping, result and jackpot update. |
| `SControl.cpp` (`SButtonBox`) | Created by `TMFieldScene`. | Page-button construction and event wiring. |
| `NewApp.cpp` web/board methods | Called from several Field actions. | External browser/board integration; no game rendering implementation is present. |

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
| `D3DXCreateFont` | No client call site; the client uses `TMFont2` and its implemented GDI/DIB bridge. |
| Base `IDirect3D9`/`ID3DXSprite` `E_NOTIMPL` templates | The concrete WASM classes override the methods used by the client; link and runtime telemetry show no unresolved dispatch. |
| DirectSound helper `E_NOTIMPL` paths | The WASM runtime uses the implemented WebAudio-backed DirectSound methods exercised by the client. |

This file tracks source completeness. Asset-format coverage is tracked
separately in `docs/wasm-asset-audit.md` and its generated JSON report.
