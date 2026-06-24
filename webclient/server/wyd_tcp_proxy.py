#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import base64
import hashlib
import logging
import struct
from typing import Optional
from urllib.parse import parse_qs, urlsplit


GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


async def read_http_request(reader: asyncio.StreamReader) -> tuple[str, dict[str, str]]:
    data = await reader.readuntil(b"\r\n\r\n")
    if len(data) > 16384:
        raise ValueError("websocket request too large")
    lines = data.decode("latin1").split("\r\n")
    request_line = lines[0]
    headers: dict[str, str] = {}
    for line in lines[1:]:
        if not line or ":" not in line:
            continue
        name, value = line.split(":", 1)
        headers[name.strip().lower()] = value.strip()
    return request_line, headers


async def websocket_handshake(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> str:
    request_line, headers = await read_http_request(reader)
    parts = request_line.split()
    if len(parts) < 2 or parts[0].upper() != "GET":
        raise ValueError("invalid websocket request line")
    key = headers.get("sec-websocket-key")
    if not key:
        raise ValueError("missing websocket key")
    accept = base64.b64encode(hashlib.sha1((key + GUID).encode("ascii")).digest()).decode("ascii")
    writer.write(
        (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Accept: {accept}\r\n"
            "\r\n"
        ).encode("ascii")
    )
    await writer.drain()
    return parts[1]


async def read_exact(reader: asyncio.StreamReader, size: int) -> bytes:
    if size <= 0:
        return b""
    return await reader.readexactly(size)


async def read_ws_frame(reader: asyncio.StreamReader) -> tuple[int, bytes]:
    first = await read_exact(reader, 2)
    b1, b2 = first[0], first[1]
    opcode = b1 & 0x0F
    masked = (b2 & 0x80) != 0
    length = b2 & 0x7F
    if length == 126:
        length = struct.unpack("!H", await read_exact(reader, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", await read_exact(reader, 8))[0]
    mask = await read_exact(reader, 4) if masked else b""
    payload = await read_exact(reader, length)
    if masked:
        payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    return opcode, payload


async def write_ws_frame(writer: asyncio.StreamWriter, opcode: int, payload: bytes) -> None:
    header = bytearray([0x80 | (opcode & 0x0F)])
    length = len(payload)
    if length < 126:
        header.append(length)
    elif length <= 0xFFFF:
        header.extend([126])
        header.extend(struct.pack("!H", length))
    else:
        header.extend([127])
        header.extend(struct.pack("!Q", length))
    writer.write(bytes(header) + payload)
    await writer.drain()


def pick_target(path: str, default_host: str, default_port: int, allow_client_target: bool) -> tuple[str, int]:
    host = default_host
    port = default_port
    if allow_client_target:
        query = parse_qs(urlsplit(path).query)
        host = query.get("host", [host])[0]
        raw_port = query.get("port", [str(port)])[0]
        try:
            port = int(raw_port)
        except ValueError:
            port = default_port
    if not host:
        raise ValueError("missing target host; pass --target-host or WebSocket ?host=")
    if port <= 0 or port > 65535:
        raise ValueError(f"invalid target port: {port}")
    return host, port


async def pipe_ws_to_tcp(
    ws_reader: asyncio.StreamReader,
    tcp_writer: asyncio.StreamWriter,
    label: str,
) -> None:
    while True:
        opcode, payload = await read_ws_frame(ws_reader)
        if opcode == 0x8:
            logging.info("%s websocket close", label)
            break
        if opcode == 0x9:
            continue
        if opcode not in (0x1, 0x2, 0x0):
            logging.debug("%s ignoring websocket opcode=%s bytes=%s", label, opcode, len(payload))
            continue
        if payload:
            tcp_writer.write(payload)
            await tcp_writer.drain()
            logging.debug("%s ws->tcp bytes=%s", label, len(payload))


async def pipe_tcp_to_ws(
    tcp_reader: asyncio.StreamReader,
    ws_writer: asyncio.StreamWriter,
    label: str,
) -> None:
    while True:
        payload = await tcp_reader.read(8192)
        if not payload:
            logging.info("%s tcp eof", label)
            break
        await write_ws_frame(ws_writer, 0x2, payload)
        logging.debug("%s tcp->ws bytes=%s", label, len(payload))


async def handle_client(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    default_host: str,
    default_port: int,
    allow_client_target: bool,
) -> None:
    peer = writer.get_extra_info("peername")
    label = f"{peer[0]}:{peer[1]}" if peer else "client"
    tcp_writer: Optional[asyncio.StreamWriter] = None
    try:
        path = await websocket_handshake(reader, writer)
        host, port = pick_target(path, default_host, default_port, allow_client_target)
        logging.info("%s connect target=%s:%s", label, host, port)
        tcp_reader, tcp_writer = await asyncio.open_connection(host, port)
        tasks = [
            asyncio.create_task(pipe_ws_to_tcp(reader, tcp_writer, label)),
            asyncio.create_task(pipe_tcp_to_ws(tcp_reader, writer, label)),
        ]
        done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for task in pending:
            task.cancel()
        for task in done:
            task.result()
    except Exception as exc:
        logging.warning("%s proxy error: %s", label, exc)
    finally:
        if tcp_writer:
            tcp_writer.close()
            await tcp_writer.wait_closed()
        try:
            await write_ws_frame(writer, 0x8, b"")
        except Exception:
            pass
        writer.close()
        await writer.wait_closed()
        logging.info("%s closed", label)


async def main() -> None:
    parser = argparse.ArgumentParser(description="WYD WebSocket to TCP debug proxy")
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, default=8282)
    parser.add_argument("--target-host", default="")
    parser.add_argument("--target-port", type=int, default=8281)
    parser.add_argument("--no-client-target", dest="allow_client_target", action="store_false")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="[%(asctime)s] %(levelname)s %(message)s",
    )

    server = await asyncio.start_server(
        lambda r, w: handle_client(r, w, args.target_host, args.target_port, args.allow_client_target),
        args.listen_host,
        args.listen_port,
    )
    sockets = ", ".join(str(sock.getsockname()) for sock in server.sockets or [])
    logging.info("listening on %s", sockets)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    asyncio.run(main())
