"""Pure-data landmark overlay used by all rebuild/install paths."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / 'Art/HaijuNorthBeta/Landmarks'

def merge_shapes(existing):
    manifest_path = ROOT/'replacement_manifest.json'
    if not manifest_path.exists():
        return existing
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    replacements = json.loads((ROOT/'landmark_pick_shapes.json').read_text(encoding='utf-8'))
    removed = set(manifest['remove_pick_keys'])
    by_key = {s['key']: s for s in replacements}
    result = []
    for item in existing:
        if item['key'] in by_key:
            result.append(by_key.pop(item['key']))
        elif item['key'] not in removed:
            result.append(item)
    result.extend(by_key.values())
    assert len(result) == manifest['expected_total_pick_shapes_after']
    assert len({s['key'] for s in result}) == len(result)
    return result
