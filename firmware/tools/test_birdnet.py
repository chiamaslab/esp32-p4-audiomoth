"""Exercise the configuration client's real HTTP/cookie handling over a local socket pair."""
import json
import socket
import threading
import unittest
from http.client import HTTPConnection
from http.server import BaseHTTPRequestHandler
from unittest.mock import patch

from enable_bats import enable_model


class EnableModelTest(unittest.TestCase):
    def test_csrf_session_and_non_destructive_retry(self):
        models = ["birdnet", "perch_v2"]
        writes = []

        class API(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def reply(self, status, data, headers=()):
                self.send_response(status)
                for key, value in headers:
                    self.send_header(key, value)
                self.end_headers()
                self.wfile.write(json.dumps(data).encode())

            def do_GET(self):
                if self.path == "/api/v2/app/config":
                    self.reply(200, {"csrfToken": "fixture"}, [("Set-Cookie", "_csrf=fixture; HttpOnly; Path=/")])
                elif self.path == "/api/v2/settings/models":
                    self.reply(200, {"enabled": models})
                elif self.path == "/api/v2/settings":
                    self.reply(200, {}, [("ETag", '"setup"')])
                else:
                    self.reply(404, {})

            def do_PATCH(self):
                if self.path != "/api/v2/settings/models":
                    return self.reply(404, {})
                if self.headers.get("Cookie") != "_csrf=fixture" or self.headers.get("X-CSRF-Token") != "fixture":
                    return self.reply(403, {"message": "Invalid CSRF token"})
                if self.headers.get("If-Match") != '"setup"':
                    return self.reply(412, {})
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                writes.append(body)
                models[:] = body["enabled"]
                self.reply(200, {"success": True})

        def connect(conn):
            conn.sock, peer = socket.socketpair()
            def serve():
                with peer:
                    API(peer, ("127.0.0.1", 0), None)
            threading.Thread(target=serve, daemon=True).start()

        with patch.object(HTTPConnection, "connect", connect):
            saved = enable_model("http://fixture.local:8080")
            self.assertEqual(saved, ["birdnet", "perch_v2", "bat"])
            self.assertEqual(enable_model("http://fixture.local:8080"), saved)
        self.assertEqual(writes, [{"enabled": saved}])


if __name__ == "__main__":
    unittest.main()
