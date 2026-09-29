import urllib.error
import urllib.request

req = urllib.request.Request(
    "http://127.0.0.1:8092/v1/session/begin",
    data=b'{"agent_id":"office.agent.0"}',
    method="POST",
    headers={"Content-Type": "application/json"},
)
try:
    with urllib.request.urlopen(req, timeout=170) as response:
        print(response.status)
        print(response.read().decode("utf-8", "replace"))
except urllib.error.HTTPError as exc:
    print(exc.code)
    print(exc.read().decode("utf-8", "replace"))
except Exception as exc:
    print(type(exc).__name__, exc)
