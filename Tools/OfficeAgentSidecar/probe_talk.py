import json
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:8095"


def call(path, payload=None):
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        BASE + path,
        data=data,
        method="GET" if payload is None else "POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=180) as response:
            body = response.read().decode("utf-8", "replace")
            print(path, response.status, body[:1200])
            return json.loads(body)
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        print(path, exc.code, body[:1200])
        return None


call("/healthz")
call("/v1/session/begin", {"agent_id": "office.agent.0"})
call("/v1/session/prompt", {"text": "你好，请用一句话介绍你自己。"})
