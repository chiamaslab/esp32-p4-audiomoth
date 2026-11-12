"""Enable the already-installed bat model and verify the two live board streams."""
import argparse
import http.cookiejar
import json
from pathlib import Path
import urllib.request
from urllib.error import HTTPError

from check_stream import run


def enable_model(base):
    base = base.rstrip("/")
    client = urllib.request.build_opener(urllib.request.ProxyHandler({}),
                                         urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    section = base + "/api/v2/settings/models"
    for attempt in range(3):
        # Some releases supply a whole-config ETag. Only the response headers
        # are needed. Do not log, store or read the full settings document.
        with client.open(base + "/api/v2/settings", timeout=8) as response:
            etag = response.headers.get("ETag")
        with client.open(section, timeout=8) as response:
            enabled = json.load(response)["enabled"]
        if not isinstance(enabled, list) or not all(isinstance(v, str) for v in enabled):
            raise ValueError("Unexpected models.enabled response")
        if "bat" in enabled:
            return enabled
        with client.open(base + "/api/v2/app/config", timeout=8) as response:
            token = json.load(response).get("csrfToken")
        if not isinstance(token, str) or not token:
            raise RuntimeError("BirdNET-Go did not issue a CSRF token for this session")
        headers = {"Content-Type": "application/json", "X-CSRF-Token": token, "Origin": base}
        if etag:
            headers["If-Match"] = etag
        data = json.dumps({"enabled": enabled + ["bat"]}).encode()
        req = urllib.request.Request(section, data=data, headers=headers, method="PATCH")
        try:
            with client.open(req, timeout=20) as response:
                response.read()
        except HTTPError as exc:
            if exc.code == 412 and attempt < 2:
                continue
            raise RuntimeError(f"BirdNET-Go returned HTTP {exc.code}: "
                               f"{exc.read(500).decode(errors='replace')}") from None
        with client.open(section, timeout=8) as response:
            saved = json.load(response)["enabled"]
        if not all(v in saved for v in enabled + ["bat"]):
            raise RuntimeError(f"BirdNET-Go did not retain the model update: {saved}")
        return saved
    raise RuntimeError("Model settings changed concurrently")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--birdnet", required=True, help="BirdNET-Go base URL, including port")
    parser.add_argument("--node", required=True, help="ESP32-P4 base URL")
    args = parser.parse_args()
    bench = Path(__file__).resolve().parents[1] / "bench"
    bench.mkdir(exist_ok=True)
    try:
        enabled = enable_model(args.birdnet)
    except (ValueError, OSError, RuntimeError) as error:
        raise SystemExit(str(error)) from None
    print("Enabled models:", enabled, flush=True)
    print("Reading both streams for 60 seconds...", flush=True)
    results = run(args.node.rstrip("/"), 60, bench / "native-test.wav")
    report = json.dumps(results, indent=2)
    (bench / "stream-check.json").write_text(report + "\n")
    print(report)
