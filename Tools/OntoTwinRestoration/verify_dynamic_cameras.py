"""Read-only integration check in a background UE -game -RenderOffscreen run."""
import json
from pathlib import Path
import time
import traceback
import unreal as u

OUT = Path('D:/0bay/OfficeShell/Saved/CameraDynamicRestore')
SOURCE = json.loads((OUT / 'editor-before.json').read_text(encoding='utf-8'))
EXPECTED = {c['instance_id']: c for c in SOURCE['cameras']}
STATE = dict(start=time.monotonic(), handle=None, checked=False, next_camera=17, last_open=0, results=[])


def finish(result):
    (OUT / 'runtime-verification.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    u.unregister_slate_post_tick_callback(STATE['handle'])
    u.SystemLibrary.quit_editor()


def tick(delta):
    try:
        now = time.monotonic()
        world = u.find_object(None, '/Game/OfficeShell/Maps/Office_Demo_ArtMerged.Office_Demo_ArtMerged')
        if not world:
            if now - STATE['start'] > 90: raise RuntimeError('No game world')
            return
        actors = u.GameplayStatics.get_all_actors_of_class(world, u.Actor)
        instances = [a for a in actors if isinstance(a, u.TwinInstance)]
        if len(instances) != 14 or now - STATE['start'] < 15:
            if now - STATE['start'] > 90: raise RuntimeError('Expected 14 dynamic instances; got ' + str(len(instances)))
            return
        indexed = {a.get_editor_property('instance_id'): a for a in instances}
        assert set(indexed) == set(EXPECTED), list(indexed)
        managers = [a for a in actors if isinstance(a, u.TwinSceneManager)]
        assert len(managers) == 1
        web = managers[0].get_editor_property('web_interaction_manager')
        if not STATE['checked']:
            backup_count = 0
            for actor in actors:
                tags = [str(t) for t in actor.tags]
                ids = [t.split('=', 1)[1] for t in tags if t.startswith('OntoTwinInstanceId=')]
                if not isinstance(actor, u.TwinInstance) and ids and ids[0] in EXPECTED:
                    assert actor.get_editor_property('hidden'), 'Static duplicate visible: ' + ids[0]
                    assert not actor.get_actor_enable_collision()
                    backup_count += 1
            assert backup_count == 14, backup_count
            for instance_id, actor in indexed.items():
                expected = EXPECTED[instance_id]
                t = actor.get_actor_transform()
                r, s, p = t.rotation.rotator(), t.scale3d, t.translation
                actual = dict(tx=p.x, ty=p.y, tz=p.z, rx=r.roll, ry=r.pitch, rz=r.yaw, sx=s.x, sy=s.y, sz=s.z)
                assert all(abs(actual[k] - v) < 1e-4 for k, v in expected['transform'].items()), (instance_id, actual)
                mesh = actor.get_editor_property('mesh_component')
                assert mesh.static_mesh.get_path_name() == expected['meshes'][0]['asset']
                assert [m.get_path_name() if m else None for m in mesh.get_materials()] == expected['meshes'][0]['materials']
                # Probe from each side through the actual camera geometry, ignoring unrelated occluders.
                center, extent = actor.get_actor_bounds(False)
                hit_actor = None
                ignore = [a for a in actors if a != actor]
                distance = max(extent.x, extent.y, extent.z) * 2 + 20
                for axis in [u.Vector(distance, 0, 0), u.Vector(0, distance, 0), u.Vector(0, 0, distance)]:
                    hit = u.SystemLibrary.line_trace_single(world, center + axis, center - axis,
                        u.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ignore, u.DrawDebugTrace.NONE, True)
                    if hit:
                        info = hit.to_tuple()
                        if info[0]:
                            hit_actor = info[9]
                            break
                assert hit_actor == actor, 'Visibility ray did not select dynamic camera ' + instance_id
            STATE['checked'] = True
        if now - STATE['last_open'] < 2:
            return
        instance_id = str(STATE['next_camera'])
        if STATE['next_camera'] > 30:
            finish(dict(passed=True, dynamic_count=14, hidden_static_backups=14,
                transforms_materials_models_match=True, visibility_rays_passed=14,
                detail_open_results=STATE['results']))
            return
        opened = web.open_instance_detail(instance_id)
        assert opened, 'OpenInstanceDetail failed for ' + instance_id
        STATE['results'].append(dict(instance_id=instance_id, opened=opened))
        STATE['next_camera'] += 1
        STATE['last_open'] = now
    except Exception:
        finish(dict(passed=False, error=traceback.format_exc()))


STATE['handle'] = u.register_slate_post_tick_callback(tick)
