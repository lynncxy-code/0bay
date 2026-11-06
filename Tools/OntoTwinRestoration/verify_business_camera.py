"""Standalone -game smoke test; changes only transient runtime camera tags."""
import json
import time
import traceback
from pathlib import Path
import unreal as u

out = Path('D:/0bay/OfficeShell/Saved/BusinessCameraBinding')
state = dict(start=time.monotonic(), stage=0, due=0, handle=None, results=[])
tag = 'OntoTwinViewForBusiness=bv.network-security'

def finish(passed, error=None):
    (out / 'runtime-verification.json').write_text(json.dumps(dict(passed=passed, error=error,
        results=state['results']), indent=2), encoding='utf-8')
    u.unregister_slate_post_tick_callback(state['handle'])
    u.SystemLibrary.quit_editor()

def view_record(world, camera):
    view = u.GameplayStatics.get_player_camera_manager(world, 0)
    lens = camera.get_component_by_class(u.CameraComponent)
    position_error = (view.get_camera_location() - lens.get_world_location()).length()
    rotation = view.get_camera_rotation()
    expected = lens.get_world_rotation()
    rotation_error = max(abs((a-b+180)%360-180) for a,b in zip(
        (rotation.roll,rotation.pitch,rotation.yaw),(expected.roll,expected.pitch,expected.yaw)))
    return dict(position_error_cm=position_error, rotation_error_degrees=rotation_error,
                fov_error_degrees=abs(view.get_fov_angle()-lens.field_of_view))

def tick(delta):
    try:
        now = time.monotonic()
        if now-state['start'] > 150: raise RuntimeError('Business camera test timed out')
        if now-state['start'] < 18 or now < state['due']: return
        world = u.find_object(None, '/Game/OfficeShell/Maps/Office_Demo_ArtMerged.Office_Demo_ArtMerged')
        if not world: return
        managers = u.GameplayStatics.get_all_actors_of_class(world, u.TwinSceneManager)
        if len(managers) != 1: return
        web = managers[0].get_editor_property('web_interaction_manager')
        if state['stage'] == 0:
            names = ['L_GrandNeoBay_Building','L_HaijuNorthBeta_Exterior','L_Campus500_Details','L_RefinedLandmarks']
            state['exteriors'] = [u.GameplayStatics.get_streaming_level(world, name) for name in names]
            assert all(state['exteriors']), 'All four environment descriptors must exist before entry'
            state['satellites'] = u.GameplayStatics.get_all_actors_of_class(world,u.LandscapeProxy)
            assert state['satellites'], 'Satellite landscape must exist'
            state['satellite_hidden'] = [a.get_editor_property('hidden') for a in state['satellites']]
            cameras = [a for a in u.GameplayStatics.get_all_actors_of_class(world,u.CameraActor) if tag in [str(t) for t in a.tags]]
            assert len(cameras)==1, 'Saved business binding must survive map reload'
            state['camera']=cameras[0]
            state['transform']=cameras[0].get_actor_transform().export_text()
            assert web.open_business_view('bv.network-security','')
            state['stage']=1; state['due']=now+23
            return
        camera = state['camera']
        if state['stage'] != 4:
            assert all(not level.get_loaded_level() for level in state['exteriors']), 'Exterior levels must actually unload'
            assert all(a.get_editor_property('hidden') for a in state['satellites']), 'Satellite map must be off'
        if state['stage'] in (1,3,5):
            record = view_record(world,camera)
            assert record['position_error_cm']<.1 and record['rotation_error_degrees']<.01 and record['fov_error_degrees']<.01, record
            assert camera.get_actor_transform().export_text()==state['transform'], 'Authored transform changed'
            record['stage']=state['stage']; state['results'].append(record)
        if state['stage']==1:
            camera.set_editor_property('tags',[str(t) for t in camera.tags if str(t)!=tag])
            assert web.open_business_view('bv.network-security','')
            state['stage']=2; state['due']=now+23
        elif state['stage']==2:
            record=view_record(world,camera)
            assert record['position_error_cm']>1, 'Missing binding must use automatic framing'
            assert camera.get_actor_transform().export_text()==state['transform']
            state['results'].append(dict(stage=2,automatic_fallback=True,authored_camera_unchanged=True))
            camera.set_editor_property('tags',[str(t) for t in camera.tags]+[tag])
            assert web.open_business_view('bv.network-security','')
            state['stage']=3; state['due']=now+23
        elif state['stage']==3:
            assert web.open_business_view('bv.custom.mu0xsto9','')
            state['stage']=4; state['due']=now+3
        elif state['stage']==4:
            assert all(level.get_loaded_level() for level in state['exteriors']), 'Environment must reload outside security'
            assert [a.get_editor_property('hidden') for a in state['satellites']] == state['satellite_hidden'], 'Satellite visibility must restore'
            record=view_record(world,camera)
            assert record['position_error_cm']<.1, 'Highlight entry must preserve current camera'
            state['results'].append(dict(stage=4,highlight_keeps_camera=True))
            web.back()
            state['stage']=5; state['due']=now+3
        elif state['stage']==5:
            assert web.open_instance_detail('17')
            state['stage']=6; state['due']=now+3
        elif state['stage']==6:
            state['results'].append(dict(exterior_levels_unloaded=4,satellite_off=True,
                environment_restored_outside_security=True,detail_keeps_environment_off=True))
            finish(True)
    except Exception:
        finish(False,traceback.format_exc())

state['handle']=u.register_slate_post_tick_callback(tick)



