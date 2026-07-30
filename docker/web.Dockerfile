# syntax=docker/dockerfile:1.7

ARG EMSCRIPTEN_VERSION=6.0.0

FROM emscripten/emsdk:${EMSCRIPTEN_VERSION} AS wasm-assets
ARG TARGETARCH
WORKDIR /src

COPY v769ClientRelease /src/v769ClientRelease
COPY webclient/client-wasm/config /src/webclient/client-wasm/config
COPY webclient/client-wasm/tools /src/webclient/client-wasm/tools
COPY webclient/client-wasm/assets/gdi-font /tmp/openwyd-gdi-font

RUN --mount=type=cache,id=openwyd-emscripten-system-${TARGETARCH},target=/emsdk/upstream/emscripten/cache \
    mkdir -p /src/webclient/client-wasm/build/generated/gdi-font \
    && cp /tmp/openwyd-gdi-font/openwyd_gdi_tahoma12_a4.bin \
        /src/webclient/client-wasm/build/generated/gdi-font/ \
    && cp /tmp/openwyd-gdi-font/openwyd_gdi_tahoma12_a4.json \
        /src/webclient/client-wasm/build/generated/gdi-font/ \
    && python3 webclient/client-wasm/tools/build_wasm_asset_bundle.py \
        --repo-root /src \
        --force

FROM emscripten/emsdk:${EMSCRIPTEN_VERSION} AS wasm-build
ARG TARGETARCH
WORKDIR /src

COPY Projects /src/Projects
COPY Dependencies/Directx/Include /src/Dependencies/Directx/Include
COPY webclient/client-wasm /src/webclient/client-wasm
COPY --from=wasm-assets \
    /src/webclient/client-wasm/build/link/ \
    /src/webclient/client-wasm/build/link/

RUN --mount=type=cache,id=openwyd-wasm-objects-${TARGETARCH},target=/src/webclient/client-wasm/build/obj \
    --mount=type=cache,id=openwyd-emscripten-system-${TARGETARCH},target=/emsdk/upstream/emscripten/cache \
    python3 webclient/client-wasm/tools/link_tmproject_wasm_startup.py \
        --repo-root /src \
        --dev \
        --jobs 8 \
        --link-opt-level O2 \
    && python3 webclient/client-wasm/tools/prepare_pages_site.py \
        --link-dir /src/webclient/client-wasm/build/link \
        --out-dir /src/webclient/client-wasm/build/site

FROM nginx:1.27-alpine
COPY docker/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=wasm-build /src/webclient/client-wasm/build/site/ /usr/share/nginx/html/

EXPOSE 80
