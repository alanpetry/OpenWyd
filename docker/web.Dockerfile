# syntax=docker/dockerfile:1.7

ARG EMSCRIPTEN_VERSION=6.0.0
ARG OPENWYD_BUILD_JOBS=2

FROM emscripten/emsdk:${EMSCRIPTEN_VERSION} AS wasm-assets
ARG TARGETARCH
WORKDIR /src

RUN apt-get update \
    && apt-get install -y --no-install-recommends brotli \
    && rm -rf /var/lib/apt/lists/*

COPY v769ClientRelease /src/v769ClientRelease
COPY webclient/client-wasm/config/startup-preload-manifest.txt \
    /src/webclient/client-wasm/config/startup-preload-manifest.txt
COPY webclient/client-wasm/tools/build_wasm_asset_bundle.py \
    webclient/client-wasm/tools/build_gdi_font_atlas.py \
    webclient/client-wasm/tools/convert_wyt_to_png.py \
    webclient/client-wasm/tools/preload_manifest.py \
    webclient/client-wasm/tools/generate_gdi_font_atlas.cpp \
    /src/webclient/client-wasm/tools/
COPY webclient/client-wasm/assets/gdi-font /tmp/openwyd-gdi-font
COPY webclient/client-wasm/assets/optimized-hd \
    /src/webclient/client-wasm/assets/optimized-hd

RUN --mount=type=cache,id=openwyd-emscripten-system-${TARGETARCH},target=/emsdk/upstream/emscripten/cache \
    --mount=type=cache,id=openwyd-asset-hashes-${TARGETARCH},target=/src/webclient/client-wasm/build/cache \
    mkdir -p /src/webclient/client-wasm/build/generated/gdi-font \
    && cp /tmp/openwyd-gdi-font/openwyd_gdi_tahoma12_a4.bin \
        /src/webclient/client-wasm/build/generated/gdi-font/ \
    && cp /tmp/openwyd-gdi-font/openwyd_gdi_tahoma12_a4.json \
        /src/webclient/client-wasm/build/generated/gdi-font/ \
    && python3 webclient/client-wasm/tools/build_wasm_asset_bundle.py \
        --repo-root /src \
    && python3 webclient/client-wasm/tools/convert_wyt_to_png.py \
        /src/v769ClientRelease/UI/newtitle.wyt \
        /src/webclient/client-wasm/build/generated/openwyd_loading.png \
    && find /src/webclient/client-wasm/build/link -type f \
        \( -name 'openwyd_*.data' -o -name 'openwyd_*.js' \) \
        -exec gzip -6 -k -f '{}' \; \
    && find /src/webclient/client-wasm/build/link -type f \
        \( -name 'openwyd_*.data' -o -name 'openwyd_*.js' \) \
        -exec brotli -f -q 8 '{}' \;

FROM emscripten/emsdk:${EMSCRIPTEN_VERSION} AS wasm-build
ARG TARGETARCH
ARG OPENWYD_BUILD_JOBS
WORKDIR /src

RUN apt-get update \
    && apt-get install -y --no-install-recommends brotli \
    && rm -rf /var/lib/apt/lists/*

COPY Projects /src/Projects
COPY Dependencies/Directx/Include /src/Dependencies/Directx/Include
COPY webclient/client-wasm /src/webclient/client-wasm
COPY --from=wasm-assets \
    /src/webclient/client-wasm/build/link/ \
    /src/webclient/client-wasm/build/link/
COPY --from=wasm-assets \
    /src/webclient/client-wasm/build/generated/openwyd_loading.png \
    /src/webclient/client-wasm/build/generated/openwyd_loading.png

RUN --mount=type=cache,id=openwyd-wasm-objects-${TARGETARCH},target=/src/webclient/client-wasm/build/obj \
    --mount=type=cache,id=openwyd-emscripten-system-${TARGETARCH},target=/emsdk/upstream/emscripten/cache \
    python3 webclient/client-wasm/tools/link_tmproject_wasm_startup.py \
        --repo-root /src \
        --dev \
        --jobs "${OPENWYD_BUILD_JOBS}" \
        --link-opt-level O2 \
    && python3 webclient/client-wasm/tools/prepare_pages_site.py \
        --link-dir /src/webclient/client-wasm/build/link \
        --out-dir /src/webclient/client-wasm/build/site \
        --max-bytes 2400000000 \
        --loading-art /src/webclient/client-wasm/build/generated/openwyd_loading.png \
    && find /src/webclient/client-wasm/build/site -type f \
        \( -name '*.wasm' -o -name 'tmproject_startup.*.js' \) \
        -exec gzip -6 -k -f '{}' \; \
    && find /src/webclient/client-wasm/build/site -type f \
        \( -name '*.wasm' -o -name 'tmproject_startup.*.js' \) \
        -exec brotli -f -q 8 '{}' \;

FROM nginx:1.27-alpine
COPY docker/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=wasm-build /src/webclient/client-wasm/build/site/ /usr/share/nginx/html/
COPY --from=wasm-assets /src/v769ClientRelease/music/ /usr/share/nginx/html/music/

EXPOSE 80
