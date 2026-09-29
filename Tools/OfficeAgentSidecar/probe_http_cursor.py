import json
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:8096"


def call(path, payload=None, timeout=180):
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        BASE + path,
        data=data,
        method="GET" if payload is None else "POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            body = response.read()
            if path.endswith(".wav"):
                print(path, response.status, "wav_bytes", len(body))
                return body
            text = body.decode("utf-8", "replace")
            print(path, response.status, text[:800])
            return json.loads(text)
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", "replace")
        print(path, exc.code, raw[:800])
        return None


call("/healthz")
call("/v1/session/begin", {"agent_id": "office.agent.0"})
result = call("/v1/session/prompt", {"text": "用一句话介绍这个办公室展厅，不要编造职位和姓名。"})
if result and result.get("tts_url"):
    call(result["tts_url"])
call("/v1/session/end", {})
