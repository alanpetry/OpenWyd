#!/usr/bin/env python3
from __future__ import annotations

import asyncio
import hashlib
import json
import struct
import tempfile
import unittest
from pathlib import Path

from wyd_tcp_proxy import TransportTrace, pick_target, pipe_ws_to_tcp


class MemoryWriter:
    def __init__(self) -> None:
        self.data = bytearray()

    def write(self, payload: bytes) -> None:
        self.data.extend(payload)

    async def drain(self) -> None:
        return None


def masked_client_frame(opcode: int, payload: bytes) -> bytes:
    mask = b"\x12\x34\x56\x78"
    header = bytearray([0x80 | opcode])
    size = len(payload)
    if size < 126:
        header.append(0x80 | size)
    elif size <= 0xFFFF:
        header.append(0x80 | 126)
        header.extend(struct.pack("!H", size))
    else:
        header.append(0x80 | 127)
        header.extend(struct.pack("!Q", size))
    masked = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    return bytes(header) + mask + masked


class TransportTraceTests(unittest.TestCase):
    def test_trace_records_hash_and_size_without_payload(self) -> None:
        payload = bytes(range(256))
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "trace.jsonl"
            trace = TransportTrace(path)
            asyncio.run(
                trace.record(
                    connection_id=7,
                    peer="127.0.0.1:40000",
                    direction="websocket_to_tcp",
                    payload=payload,
                )
            )
            trace.close()

            entry = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(entry["schema"], "openwyd.transport-chunk.v1")
            self.assertEqual(entry["connection_id"], 7)
            self.assertEqual(entry["direction"], "websocket_to_tcp")
            self.assertEqual(entry["size"], len(payload))
            self.assertEqual(entry["sha256"], hashlib.sha256(payload).hexdigest())
            self.assertNotIn("payload", entry)

    def test_client_target_parsing_is_explicitly_disableable(self) -> None:
        path = "/?host=example.test&port=9000"
        self.assertEqual(
            pick_target(path, "127.0.0.1", 8281, allow_client_target=True),
            ("example.test", 9000),
        )
        self.assertEqual(
            pick_target(path, "127.0.0.1", 8281, allow_client_target=False),
            ("127.0.0.1", 8281),
        )

    def test_masked_websocket_binary_payload_reaches_tcp_byte_exact(self) -> None:
        payload = bytes(range(256)) * 3

        async def exercise() -> bytes:
            reader = asyncio.StreamReader()
            reader.feed_data(masked_client_frame(0x2, payload))
            reader.feed_data(masked_client_frame(0x8, b""))
            reader.feed_eof()
            writer = MemoryWriter()
            await pipe_ws_to_tcp(
                reader,
                writer,  # type: ignore[arg-type]
                "test-peer",
                connection_id=1,
                trace=None,
            )
            return bytes(writer.data)

        self.assertEqual(asyncio.run(exercise()), payload)


if __name__ == "__main__":
    unittest.main(verbosity=2)
