"""Real rendered runtime test. Does not save or modify authored levels."""
import json
import time
import traceback
from pathlib import Path
import unreal as u

out = Path('D:/0bay/OfficeShell/Saved/SecuritySky')
s = dict(start=time.monotonic(), stage=0, due=0, checks=[], handle=None)

def finish(passed, error=None):
    (out / 'runtime-verification.json').write_text(json.dumps(dict(passed=passed, error=error, checks=s['checks']), indent=2))
    u.unregister_slate_post_tick_callback(s['handle'])
    u.SystemLibrary.quit_editor()

def hidden(a):
    return a.get_editor_property('hidden')

def tick(dt):
    try:
        if time.monotonic() - s['start'] > 300: raise RuntimeError('Sky test timeout')
        world = u.find_object(None, '/Game/OfficeShell/Maps/Office_Demo_ArtMerged.Office_Demo_ArtMerged')
        if not world: return
        now = u.GameplayStatics.get_time_seconds(world)
        if now < s['due'] or now < 18: return
        if s['stage'] == 0:
            managers = u.GameplayStatics.get_all_actors_of_class(world, u.TwinSceneManager)
            if len(managers) != 1: return
            s['web'] = managers[0].web_interaction_manager
            actors = u.GameplayStatics.get_all_actors_of_class(world, u.Actor)
            s['background'] = next(a for a in actors if 'OntoTwinSecurityOverviewBackground' in [str(t) for t in a.tags])
            mat=s['background'].get_component_by_class(u.StaticMeshComponent).get_material(0)
            assert mat.get_name() == 'MI_SecurityTexturedHDRI', str(mat)
            assert mat.get_editor_property('parent').get_name() == 'M_SecurityTexturedHDRI', str(mat)
            s['defaults'] = [(a, hidden(a)) for a in actors if 'OntoTwinDefaultBackground' in [str(t) for t in a.tags]]
            assert len(s['defaults']) == 4
            s['camera'] = next(a for a in actors if 'OntoTwinViewForBusiness=bv.network-security' in [str(t) for t in a.tags])
            s['camera_transform'] = s['camera'].get_actor_transform().export_text()
            s['scanner'] = u.GameplayStatics.get_all_actors_of_class(world, u.Go2DeviceScanner)[0]
            assert hidden(s['background'])
            assert s['web'].open_business_view('bv.network-security', '')
            s['checks'].append('Saved background starts hidden; original skies retain their states')
            s.update(stage=1, due=now+2)
        elif s['stage'] == 1:
            assert s['scanner'].security_intro_active
            assert not hidden(s['background']), 'Security intro must use quiet background'
            s['checks'].append('Security follow-camera intro uses quiet background')
            s.update(stage=2, due=now+12)
        elif s['stage'] in (2, 4, 6):
            lens=s['camera'].get_component_by_class(u.CameraComponent)
            pcm=u.GameplayStatics.get_player_camera_manager(world,0)
            if s['scanner'].security_intro_active or (pcm.get_camera_location()-lens.get_world_location()).length()>2:
                return
            assert not hidden(s['background']), 'Overview background must be visible'
            assert all(hidden(a) for a, _ in s['defaults'])
            assert s['camera'].get_actor_transform().export_text() == s['camera_transform']
            s['checks'].append('Overview shows Example_Map_11 background with authored camera unchanged, stage ' + str(s['stage']))
            if s['stage'] == 2:
                u.SystemLibrary.execute_console_command(world, 'HighResShot 1 filename=D:/0bay/OfficeShell/Saved/SecuritySky/overview.png')
                s.update(stage=3, due=now+3)
            elif s['stage'] == 4:
                assert s['web'].open_instance_detail('17')
                s.update(stage=5, due=now+3)
            else:
                assert s['web'].open_business_view('bv.custom.mu0xsto9', '')
                s.update(stage=7, due=now+3)
        elif s['stage'] == 3:
            s['scanner'].set_follow_camera(True)
            s.update(stage=30, due=now+2)
        elif s['stage'] == 30:
            assert not hidden(s['background'])
            assert all(hidden(a) for a, _ in s['defaults'])
            s['checks'].append('Manual follow camera retains Security background')
            u.SystemLibrary.execute_console_command(world, 'HighResShot 1 filename=D:/0bay/OfficeShell/Saved/SecuritySky/follow.png')
            assert s['web'].open_business_view('bv.network-security', '')
            s.update(stage=4, due=now+14)
        elif s['stage'] == 5:
            assert not hidden(s['background'])
            s['checks'].append('Device detail retains Security background')
            u.SystemLibrary.execute_console_command(world, 'HighResShot 1 filename=D:/0bay/OfficeShell/Saved/SecuritySky/detail.png')
            s['web'].back()
            s.update(stage=6, due=now+14)
        elif s['stage'] == 7:
            assert hidden(s['background'])
            assert all(hidden(a) == before for a, before in s['defaults'])
            s['checks'].append('Other business restores original sky including originally hidden mesh')
            s['web'].close()
            assert hidden(s['background'])
            assert all(hidden(a) == before for a, before in s['defaults'])
            s['checks'].append('Closing the business view retains restored background')
            finish(True)
    except Exception:
        finish(False, traceback.format_exc())

s['handle'] = u.register_slate_post_tick_callback(tick)
