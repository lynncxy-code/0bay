"""Real-map CLI integration test; all course/clock changes are runtime-only."""
import json
import time
import traceback
from pathlib import Path
import unreal as u

out=Path('D:/0bay/OfficeShell/Saved/SecurityIntroBuild')
state=dict(stage=0,start=time.monotonic(),due=0,handle=None,checks=[],samples=[])

def finish(passed,error=None):
    (out/'runtime-sequence.json').write_text(json.dumps(dict(passed=passed,error=error,
        checks=state['checks'],samples=state['samples']),indent=2),encoding='utf-8')
    u.unregister_slate_post_tick_callback(state['handle'])
    u.SystemLibrary.quit_editor()

def camera_matches(world,lens):
    view=u.GameplayStatics.get_player_camera_manager(world,0)
    distance=(view.get_camera_location()-lens.get_world_location()).length()
    actual=view.get_camera_rotation();expected=lens.get_world_rotation()
    error=max(abs((a-b+180)%360-180) for a,b in zip((actual.roll,actual.pitch,actual.yaw),(expected.roll,expected.pitch,expected.yaw)))
    assert distance<1 and error<.05,(distance,error)

def open_security(now):
    state['scan_before']=state['scanner'].scan_count
    assert state['web'].open_business_view('bv.network-security','')
    state['entry']=now

def tick(delta):
    try:
        if time.monotonic()-state['start']>300:raise RuntimeError('Security sequence test timeout')
        if time.monotonic()-state['start']<18:return
        world=u.find_object(None,'/Game/OfficeShell/Maps/Office_Demo_ArtMerged.Office_Demo_ArtMerged')
        if not world:return
        now=u.GameplayStatics.get_time_seconds(world)
        if now<state['due']:return
        pc=u.GameplayStatics.get_player_controller(world,0)
        if state['stage']==0:
            managers=u.GameplayStatics.get_all_actors_of_class(world,u.TwinSceneManager)
            dogs=u.GameplayStatics.get_all_actors_of_class(world,u.TempoGo2Course)
            scanners=u.GameplayStatics.get_all_actors_of_class(world,u.Go2DeviceScanner)
            if len(managers)!=1 or len(dogs)!=1 or len(scanners)!=1:return
            state.update(web=managers[0].web_interaction_manager,interaction=managers[0].interaction_manager,
                dog=dogs[0],scanner=scanners[0])
            if not dogs[0].paused: dogs[0].toggle_pause()
            assert len(dogs[0].legs)==6
            state['route_length']=dogs[0].route.get_spline_length()
            state['camera']=next(a for a in u.GameplayStatics.get_all_actors_of_class(world,u.CameraActor)
                if 'OntoTwinViewForBusiness=bv.network-security' in [str(t) for t in a.tags])
            pawn=u.GameplayStatics.get_player_pawn(world,0)
            if pawn and ('TwinRoaming' in pawn.get_class().get_name() or 'TwinGodView' in pawn.get_class().get_name()):
                state['interaction'].toggle_roaming()
            state['stage']=1;state['due']=now+1
            return
        dog=state['dog'];scanner=state['scanner'];web=state['web']
        if state['stage']==1:
            state['interaction'].toggle_roaming()
            state['stage']=2;state['due']=now+2
        elif state['stage']==2:
            pawn=u.GameplayStatics.get_player_pawn(world,0)
            assert pawn and ('TwinRoaming' in pawn.get_class().get_name() or 'TwinGodView' in pawn.get_class().get_name())
            assert not scanner.use_demo_camera
            state['checks'].append('F7 enters roaming without taking the dog-follow camera')
            open_security(now)
            state['stage']=3;state['due']=now+1.5
        elif state['stage']==3:
            assert scanner.security_intro_active and scanner.use_demo_camera
            assert pc.get_view_target()==scanner
            assert scanner.scan_count==state['scan_before']
            camera_matches(world,scanner.demo_camera)
            state['checks'].append('Security entry blends to F8 follow without an immediate scan')
            state['stage']=31;state['due']=state['entry']+7.5
        elif state['stage']==31:
            assert scanner.scan_count==state['scan_before']
            state['stage']=32;state['due']=state['entry']+8.5
        elif state['stage']==32:
            assert scanner.scan_count==state['scan_before']+1 and scanner.scanning
            state['checks'].append('Exactly one scan starts between 7.5 and 8.5 seconds')
            state['stage']=4;state['due']=state['entry']+19
        elif state['stage']==4:
            assert scanner.security_intro_active and pc.get_view_target()==scanner
            state['checks'].append('Follow remains active at 19 seconds')
            state['stage']=5;state['due']=state['entry']+21
        elif state['stage']==5:
            assert not scanner.security_intro_active and not scanner.use_demo_camera
            camera_matches(world,state['camera'].camera_component)
            state['checks'].append('After 20-second timer the view matches CameraActor_All')
            open_security(now)
            state['stage']=6;state['due']=now+1.5
        elif state['stage']==6:
            assert pc.get_view_target()==scanner and scanner.security_intro_active
            state['stage']=7;state['due']=state['entry']+21
        elif state['stage']==7:
            camera_matches(world,state['camera'].camera_component)
            assert not scanner.use_demo_camera
            state['checks'].append('Re-entry from god view returns correctly, without camera contention')
            open_security(now)
            state['stage']=8;state['due']=now+1.5
        elif state['stage']==8:
            assert web.open_business_view('bv.custom.mu0xsto9','')
            assert not scanner.security_intro_active
            scanner.set_follow_camera(True)
            state['stage']=9;state['due']=now+21
        elif state['stage']==9:
            assert pc.get_view_target()==scanner and scanner.use_demo_camera
            assert scanner.scan_count==state['scan_before']
            state['checks'].append('Leaving Security cancels both delayed scan and overview and respects manual follow')
            scanner.set_follow_camera(False)
            dog.set_editor_property('repeat',False)
            state['stop_base']=scanner.stop_scan_requests
            state['scan_base']=scanner.scan_count
            assert dog.start_course()
            u.GameplayStatics.set_global_time_dilation(world,8)
            state['stage']=10
        elif state['stage']==10:
            stops=scanner.stop_scan_requests-state['stop_base']
            if not state['samples'] or stops!=state['samples'][-1]['stop_requests']:
                state['samples'].append(dict(stop_requests=stops,scan_count=scanner.scan_count-state['scan_base'],
                    route_state=dog.state,distance=dog.distance_cm))
            assert stops<=6
            if dog.running:return
            assert dog.state=='Complete' and dog.completed_stops==6
            assert stops==6 and scanner.scan_count-state['scan_base']==0 and not scanner.scanning
            assert abs(dog.route.get_spline_length()-state['route_length'])<.001
            state['checks'].append('All six stops pause without scans; route geometry unchanged')
            finish(True)
    except Exception:
        finish(False,traceback.format_exc())

state['handle']=u.register_slate_post_tick_callback(tick)




