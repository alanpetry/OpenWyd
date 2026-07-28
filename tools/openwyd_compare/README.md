# OpenWyd paired-frame comparator

This debug-only tool compares a reference DirectX frame with the WebGL frame
for the same logical tick. It complements the existing image statistics in
`webclient/client-wasm/tools/visual_progress_round.mjs`; unlike that embedded
helper, this CLI has explicit dimension/orientation normalization, exact RGBA
metrics, a supplemental windowed SSIM score, deterministic JSON, and unit
tests.

The tool does not alter either capture. Its defaults are deliberately strict:
different dimensions fail, every RGBA channel difference counts, and the
changed-pixel threshold is zero. Any normalization that could conceal a
capture mismatch must be explicitly requested and is recorded in the report.

## Setup

Python 3.10 or newer is required. Pillow is the only runtime dependency and is
pinned in `requirements.txt`:

```powershell
python -m venv ../.tools/openwyd-compare-venv
../.tools/openwyd-compare-venv/Scripts/python.exe -m pip install -r tools/openwyd_compare/requirements.txt
```

The sibling `.tools` directory is local tooling outside the checkout, matching
the repository's existing development setup.

## Usage

Run from the repository root:

```powershell
python -m tools.openwyd_compare `
  artifacts/captures/directx/frame-42.png `
  artifacts/captures/webgl/frame-42.png `
  --frame-id 42
```

By default, results are written to
`artifacts/openwyd_compare/frame-00000042/`. The repository already ignores
the entire `artifacts/` tree. The CLI also writes the exact report JSON to
stdout.

Useful explicit normalization options:

```powershell
# WebGL readback is vertically inverted.
python -m tools.openwyd_compare dx.png webgl.png `
  --frame-id 42 `
  --candidate-orientation flip-y

# Normalize both captures to the official base resolution.
python -m tools.openwyd_compare dx.png webgl.png `
  --frame-id 42 `
  --target-size 800x600 `
  --resize-filter nearest
```

Other controls:

- `--size-policy strict|reference|candidate`: fail on dimension mismatch by
  default, or explicitly resize one side.
- `--alpha-mode compare|opaque`: compare alpha exactly by default, or force
  both captures to alpha 255.
- `--threshold N`: a pixel changes only when its maximum compared-channel
  delta is greater than `N`. The default is exact (`0`).
- `--heatmap-gain N`: changes only heatmap visibility, never the metrics or
  absolute diff.
- `--no-ssim`: omit SSIM if only exact metrics are wanted.

Each frame directory contains:

- `reference.normalized.png`
- `candidate.normalized.png`
- `diff.absolute.png` (absolute RGB deltas, opaque for reliable viewing)
- `diff.heatmap.png` (maximum RGBA/RGB delta according to alpha mode)
- `report.json`

The report has no timestamps or machine-specific absolute paths. It records
input and normalized-pixel SHA-256 hashes, all normalization choices, RMS and
mean absolute deltas, per-channel metrics, changed-pixel count/percentage,
maximum delta, and an 8x8 non-overlapping RGB SSIM score. SSIM is supplemental:
exact changed-pixel and RMS metrics remain present and are never relaxed by
SSIM.

## Tests

```powershell
python -m unittest discover -s tools/openwyd_compare/tests -v
```

Tests use synthetic PNGs and cover exact comparison, thresholds, alpha
normalization, orientation, size policy, CLI artifacts, SSIM, and deterministic
JSON.
