"""Background game test of the real detail-opening path and player camera output."""
import json
from pathlib import Path
import time
import traceback
import unreal as u

OUT = Path('D:/0bay/OfficeShell/Saved/CameraViewBinding')
STATE = dict(start=time.monotonic(), handle=None, next=17, pending=None, due=0, results=[], rapid=False, fallback=False)


def finish(result):
    (OUT / 'runtime-verification.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    u.unregister_slate_post_tick_callback(STATE['handle'])
    u.SystemLibrary.quit_editor()


def check_view(world, camera, instance_id):
    pc = u.GameplayStatics.get_player_controller(world, 0)
    view = u.GameplayStatics.get_player_camera_manager(world, 0)
    lens = camera.get_component_by_class(u.CameraComponent)
    position = view.get_camera_location()
    expected_position = lens.get_world_location()
    rotation = view.get_camera_rotation()
    expected_rotation = lens.get_world_rotation()
    distance = (position - expected_position).length()
    rotation_error = max(abs((a - b + 180) % 360 - 180) for a, b in zip(
        (rotation.roll, rotation.pitch, rotation.yaw), (expected_rotation.roll, expected_rotation.pitch, expected_rotation.yaw)))
    fov_error = abs(view.get_fov_angle() - lens.field_of_view)
    active_lens = pc.get_view_target().get_component_by_class(u.CameraComponent)
    assert distance < 0.1 and rotation_error < 0.01 and fov_error < 0.01, (instance_id, distance, rotation_error, fov_error)
    assert abs(active_lens.aspect_ratio - lens.aspect_ratio) < 1e-5
    assert active_lens.constrain_aspect_ratio == lens.constrain_aspect_ratio
    return dict(instance_id=instance_id, position_error_cm=distance, rotation_error_degrees=rotation_error,
                fov_error_degrees=fov_error, aspect_matches=True)


def tick(delta):
    try:
        now = time.monotonic()
        if now - STATE['start'] > 120: raise RuntimeError('View integration test timed out')
        world = u.find_object(None, '/Game/OfficeShell/Maps/Office_Demo_ArtMerged.Office_Demo_ArtMerged')
        if not world or now - STATE['start'] < 15: return
        instances = u.GameplayStatics.get_all_actors_of_class(world, u.TwinInstance)
        if len(instances) != 14: return
        managers = u.GameplayStatics.get_all_actors_of_class(world, u.TwinSceneManager)
        assert len(managers) == 1
        web = managers[0].get_editor_property('web_interaction_manager')
        cameras = u.GameplayStatics.get_all_actors_of_class(world, u.CameraActor)
        indexed = {}
        for camera in cameras:
            for tag in camera.tags:
                if str(tag).startswith('OntoTwinViewForInstance='):
                    instance_id = str(tag).split('=', 1)[1]
                    assert instance_id not in indexed
                    indexed[instance_id] = camera
        assert set(indexed) == {str(n) for n in range(17, 31)}
        if STATE['pending']:
            if now < STATE['due']: return
            instance_id = STATE['pending']
            STATE['results'].append(check_view(world, indexed[instance_id], instance_id))
            STATE['pending'] = None
        if STATE['next'] <= 30:
            instance_id = str(STATE['next'])
            assert web.open_instance_detail(instance_id)
            STATE['pending'] = instance_id
            STATE['next'] += 1
            STATE['due'] = now + 1.25
        elif not STATE['rapid']:
            # Return to person mode so the next clicks also exercise queued view changes.
            interaction = managers[0].get_editor_property('interaction_manager')
            interaction.toggle_camera_mode()
            for instance_id in ('17', '18', '30'):
                assert web.open_instance_detail(instance_id)
            STATE['pending'] = '30'
            STATE['rapid'] = True
            STATE['due'] = now + 2.5
        else:
            finish(dict(passed=True, authored_view_matches=14, rapid_last_selection='30', results=STATE['results']))
    except Exception:
        finish(dict(passed=False, error=traceback.format_exc()))


STATE['handle'] = u.register_slate_post_tick_callback(tick)
