# syntax=docker/dockerfile:1.7

FROM debian:bookworm AS build

ARG TARGETARCH
ARG OPENWYD_BUILD_JOBS=2

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ccache \
        cmake \
        g++ \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt /src/CMakeLists.txt
COPY Servidor/CMakeLists.txt /src/Servidor/CMakeLists.txt
COPY Servidor/Source /src/Servidor/Source

RUN --mount=type=cache,id=openwyd-server-ccache-${TARGETARCH},target=/root/.cache/ccache \
    cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DOPENWYD_BUILD_SERVERS=ON \
    && cmake --build /build --parallel "${OPENWYD_BUILD_JOBS}"

FROM debian:bookworm-slim AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libstdc++6 \
        tzdata \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 10001 openwyd

COPY --from=build /build/Servidor/dbsrv /opt/openwyd/bin/dbsrv
COPY --from=build /build/Servidor/tmsrv /opt/openwyd/bin/tmsrv
COPY --chown=openwyd:openwyd Servidor/Data/Common /opt/openwyd/Server/Common
COPY --chown=openwyd:openwyd Servidor/Data/DBSrv /opt/openwyd/Server/DBSRV/Run
COPY --chown=openwyd:openwyd Servidor/Data/TMSrv /opt/openwyd/Server/TMSRV/Run

RUN mkdir -p \
        /opt/openwyd/Server/DBSRV/Run/Log \
        /opt/openwyd/Server/TMSRV/Run/Ban/AutoBan \
        /opt/openwyd/Server/TMSRV/Run/Log/Attack \
        /opt/openwyd/Server/TMSRV/Run/Log/Banned \
        /opt/openwyd/Server/TMSRV/Run/Log/Combine \
        /opt/openwyd/Server/TMSRV/Run/Log/Command \
        /opt/openwyd/Server/TMSRV/Run/Log/Debugs \
        /opt/openwyd/Server/TMSRV/Run/Log/Events \
        /opt/openwyd/Server/TMSRV/Run/Log/Itens \
        /opt/openwyd/Server/TMSRV/Run/Log/Quests \
        /opt/openwyd/Server/TMSRV/Run/Log/System \
        /opt/openwyd/Server/TMSRV/Run/NewNPC \
        /opt/openwyd/Server/TMSRV/Run/Novato \
        /opt/openwyd/Server/TMSRV/Run/TitleSystem \
    && chown -R openwyd:openwyd /opt/openwyd

USER openwyd
ENV TZ=America/Sao_Paulo
