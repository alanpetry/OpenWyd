from __future__ import annotations

import contextlib
import io
import json
import socket
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from tools.openwyd_compare.controller import (
    CAPTURE_HELPER,
    RunFailed,
    doctor,
    main,
    run_controller,
)
from tools.openwyd_compare.frame_schema import (
    FRAME_FIELDS,
    FrameSchemaError,
    new_frame_record,
    validate_frame_record,
)


class ControllerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_script(self, name: str, source: str) -> Path:
        path = self.root / name
        path.write_text(source, encoding="utf-8")
        return path

    def write_config(self, value: dict[str, object], name: str = "run.json") -> Path:
        path = self.root / name
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def reserve_tcp_port(self) -> int:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            return int(listener.getsockname()[1])

    def synthetic_pair(self) -> tuple[Path, Path]:
        reference = self.root / "reference.png"
        candidate = self.root / "candidate.png"
        Image.new("RGBA", (4, 4), (10, 20, 30, 255)).save(reference)
        changed = Image.new("RGBA", (4, 4), (10, 20, 30, 255))
        changed.putpixel((2, 1), (12, 20, 30, 255))
        changed.save(candidate)
        return reference, candidate

    def process_config(self, *, bad_log_readiness: bool = False) -> Path:
        port = self.reserve_tcp_port()
        tcp_script = self.write_script(
            "tcp_service.py",
            """
import socket

listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
listener.bind(("127.0.0.1", int(__import__("sys").argv[1])))
listener.listen()
print("TCP READY", flush=True)
while True:
    connection, _ = listener.accept()
    connection.close()
""".lstrip(),
        )
        log_script = self.write_script(
            "log_service.py",
            """
import time

print("SERVICE READY", flush=True)
time.sleep(60)
""".lstrip(),
        )
        alive_script = self.write_script(
            "alive_service.py",
            """
import time

time.sleep(60)
""".lstrip(),
        )
        reference, candidate = self.synthetic_pair()
        return self.write_config(
            {
                "version": 1,
                "processes": [
                    {
                        "name": "dbsrv-dummy",
                        "role": "DBSrv",
                        "command": [sys.executable, str(tcp_script), str(port)],
                        "readiness": {
                            "type": "tcp",
                            "host": "127.0.0.1",
                            "port": port,
                            "timeout_seconds": 5,
                        },
                    },
                    {
                        "name": "tmsrv-dummy",
                        "role": "TMSrv",
                        "command": [sys.executable, str(log_script)],
                        "readiness": {
                            "type": "log",
                            "pattern": (
                                "NEVER MATCH"
                                if bad_log_readiness
                                else "SERVICE READY"
                            ),
                            "timeout_seconds": 0.3 if bad_log_readiness else 5,
                        },
                    },
                    {
                        "name": "proxy-dummy",
                        "role": "proxy",
                        "command": [sys.executable, str(alive_script)],
                        "readiness": {
                            "type": "process",
                            "min_uptime_seconds": 0.05,
                            "timeout_seconds": 5,
                        },
                    },
                ],
                "comparisons": [
                    {
                        "frame_id": "synthetic-7",
                        "name": "synthetic",
                        "reference_png": str(reference),
                        "candidate_png": str(candidate),
                    }
                ],
            }
        )

    def test_shared_frame_contract_has_all_cross_runtime_sections(self) -> None:
        record = new_frame_record(
            7,
            state=3,
            ticks={"simulation": 11},
            extensions={"native": {"draw_calls": 2}},
        )
        validate_frame_record(record)
        self.assertTrue(set(FRAME_FIELDS).issubset(record))
        self.assertEqual(record["frame_id"], 7)

        invalid = dict(record)
        invalid["unversioned_field"] = True
        with self.assertRaises(FrameSchemaError):
            validate_frame_record(invalid)

        json_schema = json.loads(
            (CAPTURE_HELPER.parent / "frame.schema.json").read_text("utf-8")
        )
        self.assertTrue(set(FRAME_FIELDS).issubset(json_schema["required"]))
        self.assertFalse(json_schema["additionalProperties"])

    def test_run_waits_all_readiness_modes_compares_and_stops_in_reverse(self) -> None:
        config_path = self.process_config()
        doctor_report = doctor(config_path)
        self.assertTrue(doctor_report["ok"], doctor_report)

        manifest = run_controller(
            config_path,
            run_root=self.root / "controller-runs",
        )
        run_dir = Path(manifest["run_dir"])

        self.assertEqual(manifest["status"], "complete")
        self.assertEqual(
            manifest["shutdown_order"],
            ["proxy-dummy", "tmsrv-dummy", "dbsrv-dummy"],
        )
        self.assertEqual(
            [process["readiness"][0]["type"] for process in manifest["processes"]],
            ["tcp", "log", "process"],
        )
        self.assertTrue(
            all(
                isinstance(process["pid"], int) and process["status"] == "stopped"
                for process in manifest["processes"]
            )
        )
        self.assertEqual(manifest["processes"][0]["env"], {})
        self.assertTrue(all(process["cwd"] for process in manifest["processes"]))

        comparison = next(
            action
            for action in manifest["actions"]
            if action["kind"] == "frame-comparison"
        )
        self.assertEqual(comparison["metrics"]["changed_pixels"], 1)
        self.assertTrue((run_dir / comparison["report"]).is_file())
        self.assertTrue((run_dir / "config.resolved.json").is_file())
        persisted = json.loads((run_dir / "run.json").read_text("utf-8"))
        self.assertEqual(persisted["status"], "complete")
        self.assertEqual(persisted["shutdown_order"], manifest["shutdown_order"])

    def test_readiness_failure_still_stops_every_started_process(self) -> None:
        config_path = self.process_config(bad_log_readiness=True)
        run_root = self.root / "failed-runs"
        with self.assertRaises(RunFailed) as caught:
            run_controller(config_path, run_root=run_root)

        run_dir = caught.exception.run_dir
        manifest = json.loads((run_dir / "run.json").read_text("utf-8"))
        self.assertEqual(manifest["status"], "failed")
        self.assertEqual(
            manifest["shutdown_order"],
            ["tmsrv-dummy", "dbsrv-dummy"],
        )
        self.assertTrue(
            all(process["status"] == "stopped" for process in manifest["processes"])
        )

    def test_capture_helper_serializes_backing_canvas_not_css_screenshot(self) -> None:
        source = CAPTURE_HELPER.read_text("utf-8")
        self.assertIn("canvas.toBlob", source)
        self.assertIn("canvas.toDataURL", source)
        self.assertIn("canvas.width !== expectedWidth", source)
        self.assertNotIn("page.screenshot", source)

    def test_explicit_compare_subcommand_uses_existing_frame_comparator(self) -> None:
        reference, candidate = self.synthetic_pair()
        output = self.root / "cli-compare"
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            return_code = main(
                [
                    "compare",
                    str(reference),
                    str(candidate),
                    "--frame-id",
                    "9",
                    "--output-dir",
                    str(output),
                ]
            )
        self.assertEqual(return_code, 0)
        self.assertEqual(json.loads(stdout.getvalue())["frame_id"], "9")
        self.assertTrue((output / "report.json").is_file())


if __name__ == "__main__":
    unittest.main()
