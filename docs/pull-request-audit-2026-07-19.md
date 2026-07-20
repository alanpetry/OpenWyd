# Open pull request audit - 2026-07-19

This audit reviewed all 425 pull requests that were open against `main`. The
review started with a repository-wide inventory and patch graph, then inspected
the individual commits behind the candidates that could affect the active
runtime. This was necessary because many pull requests are stacked, duplicate
one another, or end with cleanup commits that hide earlier changes.

## Inventory

- 425 open pull requests
- 527 unique commits not reachable from `main`
- 102 commits hidden behind later cleanup or revert-like branch heads
- 55 pull request heads in 23 exact duplicate patch groups
- 23 pull request heads whose effective patch is already present in `main`
- 139 pull request heads changing five lines or fewer

The pull requests were classified into these disjoint groups:

| Group | Pull requests | Result |
| --- | ---: | --- |
| Startup preload manifest | 87 | Rejected as empty, redundant, or unrelated to the WASM runtime |
| Disconnected compatibility headers | 97 | Rejected because no active runtime source includes them |
| Active D3D compatibility headers | 33 | Rejected as partial, obsolete, or semantically risky |
| Tooling and documentation | 20 | Reviewed individually; one manifest validation path was retained |
| Client rendering sources | 188 | Reviewed individually; conservative correctness fixes were retained |

## Rejected families

### Preload expansion

The current manifest expands to 8,060 existing files. Most proposed additions
were already covered by broader globs. Thirty-five additions matched no file at
all. The only novel existing files proposed by the remaining branches were
launcher images, additional fonts/cursors, root gameplay tables, and executable
or system mesh artifacts; none was required by the current browser runtime.

The empty `NUI/*.wys` entry was removed and an executable manifest checker was
added so missing files, empty globs, malformed entries, and duplicate virtual
destinations fail before the expensive WASM link.

### Compatibility helper headers

Ninety-seven pull requests added blend, depth-bias, FVF, or runtime-state helper
headers that are not included by the active bridge. Shipping these files would
not change behavior and would create a misleading impression of implemented
Direct3D compatibility.

### Render-state resets

One hundred twenty-two rendering proposals forced guessed default states after
individual draws. Those changes were not backed by the original client state
flow and could conceal bridge bugs or change later draws. They were not merged.
Only precondition checks that prevent invalid pointer or buffer access before
state mutation were retained.

### Partial D3D declarations

The active state implementation lives in `win32_emscripten_stubs.cpp`. Adding
constants to `d3d9.h` without implementing their behavior is incomplete. The
proposed `SetFVF` changes that cleared shaders were also rejected because they
do not reproduce the original DX9 state semantics.

## Consolidated fixes

The retained changes correct concrete defects without adding WASM-only visual
simulation:

- initialize spark and skill-effect child pointers before partial construction;
- clamp spark counts and propagate lifetimes to the actual child effects;
- correct Holy Touch and Thunder Bolt coordinate/target assignment mistakes;
- correct shade array element counts and null-scene handling;
- correct Dust material color channels and restore depth writes after its stone pass;
- validate meshes, vertex/index buffers, and lock results in Sky, Sea, Charge,
  Start, Mesh, and MeshRotate paths before accessing memory or changing state;
- validate the preload manifest automatically before every startup link.

Representative pull request families reviewed for these fixes include #212,
#218, #221, #223, #224, #226, #235, #240, #243, #244, #248, #260, #263,
#267, #268, #273, #277, #279, #285, #294, #299, #300, #337, #377, #384,
#391, #400, #411, #418, and #424. Duplicate and stacked variants are covered
by the same consolidated implementation.

## Validation

- Manifest audit: 8,060 files, no missing paths, empty globs, or duplicate destinations.
- WASM object build: 109/109 translation units compiled.
- Strict startup link: zero undefined symbols.
- Field smoke at 800x600: real scene, visible render, `criticalError=0`,
  `glErrorTotal=0`, and zero console errors.
- State sweep from -1 through 9: requested and actual states matched, with
  `glErrorTotal=0` and zero console errors for every state.
