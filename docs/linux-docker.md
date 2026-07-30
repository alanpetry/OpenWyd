# OpenWyd no Linux e Docker

O ambiente contém quatro processos separados:

```text
browser/WASM -> web:/ws -> wsproxy:8282 -> tmsrv:8281 -> dbsrv:7514
```

Somente `web` publica uma porta no host. O proxy tem destino fixo e não
interpreta pacotes: cada payload binário WebSocket é escrito no TCP sem
alteração, e cada bloco TCP volta como um frame WebSocket binário.

## Subir o ambiente

```bash
docker compose up --build -d
docker compose logs -f dbsrv tmsrv wsproxy
```

Abra `http://localhost:8080`. Para trocar a porta publicada:

```bash
OPENWYD_WEB_PORT=8088 docker compose up --build -d
```

A conta inicial é:

```text
conta: ADMIN
senha: admin
```

O DBSrv e o TMSrv usam os dados originais em arquivos. Os volumes
`dbsrv-data`, `tmsrv-data` e `common-data` preservam contas, personagens,
guildas e estado compartilhado entre reinícios.

Para voltar exatamente ao estado inicial incluído na imagem:

```bash
docker compose down -v
docker compose up --build -d
```

## Configuração suportada

- `OPENWYD_BIND_ADDRESS`, padrão `0.0.0.0`;
- `OPENWYD_DB_HOST`, padrão `dbsrv`;
- `OPENWYD_DB_PORT`, padrão `7514`;
- `OPENWYD_SERVER_GROUP`, padrão `0`;
- `OPENWYD_SERVER_INDEX`, padrão `0`.

As demais regras continuam vindo dos arquivos oficiais.

## Builds multiplataforma

O Compose compila na arquitetura atual. Para criar um manifest conjunto
amd64/arm64 em um registry:

```bash
docker buildx create --use --name openwyd-builder
docker buildx bake --push
```

É possível trocar o destino e a tag sem alterar os arquivos:

```bash
REGISTRY=ghcr.io/usuario TAG=teste docker buildx bake --push
```

As imagens oficiais do Emscripten e as imagens Debian/Python/Nginx usadas
no build possuem variantes amd64 e arm64.

## Sanitizers

Para diagnosticar o servidor nativamente em Linux amd64:

```bash
cmake -S . -B build/linux-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DOPENWYD_SANITIZE=ON
cmake --build build/linux-asan --parallel
```

Os layouts persistidos e de rede mais importantes são verificados por
`static_assert` durante qualquer build. O build também rejeita alvos
big-endian.
