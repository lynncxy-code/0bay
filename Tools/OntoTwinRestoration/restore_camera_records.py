"""Restore existing camera IDs through supported APIs; retain Web bindings.

Run with --apply after export_live_cameras.py. No credentials or RTSP URLs here.
"""
import argparse
import json
import math
from pathlib import Path
import shutil
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/CameraDynamicRestore'
PROJECT = 'ds_1788856616035'
UE_PROJECT = 'ueproj_0bay_office_opensteel_7e0ae9f7'
STORE = Path('D:/tmp/digital_twin_aircraft-prod/01-source/backend/data/projects') / (PROJECT + '.json')
IDS = {str(n) for n in range(17, 31)}
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def api(path, payload=None, method=None):
    request = urllib.request.Request('http://localhost:5000' + path,
        data=None if payload is None else json.dumps(payload).encode(), method=method,
        headers={'Content-Type': 'application/json', 'X-OntoTwin-UE-Project-Id': UE_PROJECT})
    with opener.open(request, timeout=30) as response:
        return json.load(response)


def save(name, data):
    (OUT / name).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    source = json.loads((OUT / 'editor-before.json').read_text(encoding='utf-8'))
    cameras = source['cameras']
    assert len(cameras) == 14 and {c['instance_id'] for c in cameras} == IDS
    assert all(len(c['meshes']) == 1 and c['meshes'][0]['asset'] for c in cameras)
    web = api('/api/v2/web-interactions/runtime')
    assert web['project_id'] == PROJECT
    bindings = {b['scope']['instance_id']: b for b in web['config']['bindings']
                if b.get('trigger') == 'open_detail' and b.get('enabled') and b.get('scope', {}).get('instance_id') in IDS}
    assert set(bindings) == IDS, 'Existing click bindings must be complete before migration'
    baseline = json.loads(STORE.read_text(encoding='utf-8'))
    assert baseline['id'] == PROJECT
    summaries = {i: api('/api/v2/instances/' + i + '/model-binding') for i in sorted(IDS)}
    assert all(s['capability']['enabled'] and s['project_id'] == PROJECT for s in summaries.values())
    if not args.apply:
        print('Preflight OK: 14 existing IDs, models and click bindings; no writes')
        return
    backup = OUT / 'project-before.json'
    assert not backup.exists(), 'Migration already started; inspect existing backup before retrying'
    shutil.copy2(STORE, backup)
    save('web-before-apply.json', web)
    save('models-before-apply.json', summaries)
    results = []
    for camera in cameras:
        instance_id = camera['instance_id']
        result = api('/api/v2/instances/' + instance_id + '/model-binding',
                     {'expected_project_id': PROJECT, 'ue_asset_path': camera['meshes'][0]['asset']}, 'PUT')
        assert result['effective_model']['ue_asset_path'] == camera['meshes'][0]['asset']
        result = api('/api/v2/state/writeback', {'expected_project_id': PROJECT,
                     'instance_id': instance_id, 'transform': camera['transform']}, 'POST')
        assert result['status'] == 'ok'
        results.append(instance_id)
        save('write-progress.json', results)
    after = json.loads(STORE.read_text(encoding='utf-8'))
    assert set(after['instances']) == set(baseline['instances'])
    for instance_id, old in baseline['instances'].items():
        if instance_id not in IDS:
            assert after['instances'][instance_id] == old, 'Unrelated instance changed: ' + instance_id
    snapshots = api('/api/v2/state/snapshots')
    indexed = {s['instanceId']: s for s in snapshots}
    fields = dict(tx='translation_x', ty='translation_y', tz='translation_z', rx='rotation_x',
                  ry='rotation_y', rz='rotation_z', sx='scale_x', sy='scale_y', sz='scale_z')
    for camera in cameras:
        snapshot = indexed[camera['instance_id']]
        assert snapshot['interfaces']['I3D_Representable']['asset_id'] == camera['meshes'][0]['asset']
        for key, field in fields.items():
            assert math.isclose(snapshot['interfaces']['I3D_Spatial'][field], camera['transform'][key], abs_tol=1e-6)
    current_web = api('/api/v2/web-interactions/runtime')
    assert current_web['config'] == web['config'] and current_web['revision'] == web['revision']
    save('snapshots-after.json', snapshots)
    save('web-after.json', current_web)
    save('database-verification.json', dict(camera_count=14, instance_count=len(after['instances']),
        click_config_unchanged=True, web_revision=web['revision'], unrelated_instances_unchanged=True,
        project_id=PROJECT, restored_ids=sorted(IDS)))
    print('Verified: 14 camera records persisted; transforms/models match; Web config unchanged; other instances unchanged')


if __name__ == '__main__':
    main()
