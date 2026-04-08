#!/usr/bin/env python3

from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parent.parent
WEB_ROOT = REPO_ROOT / "web"
DATA_ROOT = REPO_ROOT / "data"
TRACE_BINARY = REPO_ROOT / "mini_sql_trace"


def ensure_trace_binary() -> None:
    subprocess.run(["make", "mini_sql_trace"], cwd=REPO_ROOT, check=True)


def read_table(table_path: Path) -> dict[str, Any]:
    lines = table_path.read_text(encoding="utf-8").splitlines()
    header = lines[0].split(",") if lines else []
    rows = [line.split(",") for line in lines[1:] if line.strip()]
    return {
        "name": table_path.stem,
        "columns": header,
        "rows": rows,
        "row_count": len(rows),
    }


@dataclass
class DemoWorkspace:
    root: Path
    data_dir: Path

    @classmethod
    def create(cls) -> "DemoWorkspace":
        temp_root = Path(tempfile.mkdtemp(prefix="mini_sql_demo_"))
        workspace = cls(root=temp_root, data_dir=temp_root / "data")
        workspace.reset()
        return workspace

    def reset(self) -> None:
        if self.data_dir.exists():
            shutil.rmtree(self.data_dir)
        shutil.copytree(DATA_ROOT, self.data_dir)

    def snapshot(self) -> dict[str, Any]:
        tables = []
        for table_path in sorted(self.data_dir.glob("*.tbl")):
            tables.append(read_table(table_path))
        return {"tables": tables}

    def run(self, sql: str) -> dict[str, Any]:
        sql_path = self.root / "request.sql"
        sql_path.write_text(sql, encoding="utf-8")

        completed = subprocess.run(
            [str(TRACE_BINARY), "--data-dir", str(self.data_dir), str(sql_path)],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )

        try:
            trace = json.loads(completed.stdout)
        except json.JSONDecodeError:
            trace = {
                "ok": False,
                "sql": sql,
                "stages": {
                    "tokenizer": {"ok": False, "tokens": []},
                    "parser": {"ok": False, "statement": None},
                    "optimizer": {"ok": False, "statement": None},
                    "executor": {"ok": False, "output": ""},
                },
                "error": {
                    "stage": "server",
                    "message": completed.stderr.strip() or "trace 출력 파싱에 실패했습니다.",
                },
            }

        return {
            "trace": trace,
            "snapshot": self.snapshot(),
            "process": {
                "return_code": completed.returncode,
                "stderr": completed.stderr,
            },
        }


WORKSPACE = DemoWorkspace.create()


class DemoRequestHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/":
            self._serve_file(WEB_ROOT / "index.html", "text/html; charset=utf-8")
            return

        if self.path == "/styles.css":
            self._serve_file(WEB_ROOT / "styles.css", "text/css; charset=utf-8")
            return

        if self.path == "/app.js":
            self._serve_file(WEB_ROOT / "app.js", "application/javascript; charset=utf-8")
            return

        if self.path == "/api/state":
            self._send_json({"snapshot": WORKSPACE.snapshot()})
            return

        self.send_error(HTTPStatus.NOT_FOUND, "Not Found")

    def do_POST(self) -> None:  # noqa: N802
        if self.path == "/api/run":
            payload = self._read_json_body()
            sql = str(payload.get("sql", "")).strip()
            if not sql:
                self._send_json(
                    {
                        "trace": {
                            "ok": False,
                            "sql": "",
                            "stages": {
                                "tokenizer": {"ok": False, "tokens": []},
                                "parser": {"ok": False, "statement": None},
                                "optimizer": {"ok": False, "statement": None},
                                "executor": {"ok": False, "output": ""},
                            },
                            "error": {"stage": "input", "message": "실행할 SQL 문을 입력해 주세요."},
                        },
                        "snapshot": WORKSPACE.snapshot(),
                    },
                    status=HTTPStatus.BAD_REQUEST,
                )
                return

            self._send_json(WORKSPACE.run(sql))
            return

        if self.path == "/api/reset":
            WORKSPACE.reset()
            self._send_json({"snapshot": WORKSPACE.snapshot()})
            return

        self.send_error(HTTPStatus.NOT_FOUND, "Not Found")

    def log_message(self, fmt: str, *args: Any) -> None:
        return

    def _serve_file(self, path: Path, content_type: str) -> None:
        if not path.exists():
            self.send_error(HTTPStatus.NOT_FOUND, "Not Found")
            return

        body = path.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json_body(self) -> dict[str, Any]:
        content_length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(content_length) if content_length > 0 else b"{}"
        try:
            return json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            return {}

    def _send_json(self, payload: dict[str, Any], status: HTTPStatus = HTTPStatus.OK) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main() -> None:
    ensure_trace_binary()
    server = ThreadingHTTPServer(("127.0.0.1", 8000), DemoRequestHandler)
    print("mini_sql demo server running at http://127.0.0.1:8000")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nserver stopped")


if __name__ == "__main__":
    main()
