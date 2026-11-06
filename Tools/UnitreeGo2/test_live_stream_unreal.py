"""Explicit synthetic UDP integration test in a temporary editor session; never saves test flags."""
import unreal,time,json,subprocess,traceback
from pathlib import Path
project=Path(unreal.Paths.project_dir());out=project/'SourceAssets/UnitreeGo2/Generated';tools=project/'Tools/UnitreeGo2'
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);assert levels.load_level('/Game/Robots/UnitreeGo2/Maps/L_Go2_Live')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='Go2_Live_ReadOnly')
actor.set_editor_property('listen_port',17712);actor.set_editor_property('allow_synthetic_data',True)
log=(out/'synthetic_bridge.log').open('w')
process=subprocess.Popen([str(tools/'.venv/Scripts/python.exe'),str(tools/'go2_telemetry_bridge.py'),'--synthetic','--duration','8','--port','17712','--status-file',str(project/'Saved/Go2/synthetic_status.json')],stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
start=time.monotonic();samples=[];last_sample=0
def tick(dt):
    global last_sample
    t=time.monotonic()-start
    try:
        actor.poll_telemetry(dt)
        if t-last_sample>0.2:
            last_sample=t;samples.append({'t':t,'frames':actor.get_editor_property('accepted_frames'),'status':actor.get_editor_property('connection_status'),'live':actor.get_editor_property('live_data_fresh'),'q':list(actor.get_editor_property('joint_angles_radians'))})
        if t>11:
            assert process.poll()==0,'Bridge failed or did not stop'
            assert samples[-1]['frames']>100,samples[-1]
            assert max(s['q'][1] for s in samples)-min(s['q'][1] for s in samples)>0.1,'Joint did not move'
            assert not any(s['live'] for s in samples),'Synthetic data mislabeled as live'
            assert samples[-1]['status']=='STALE - last pose held',samples[-1]
            frozen=[s['q'] for s in samples if s['t']>9]
            assert len(frozen)>=2 and max(abs(a-b) for a,b in zip(frozen[0],frozen[-1]))<1e-7,'Stale pose moved'
            (out/'live_udp_integration_test.json').write_text(json.dumps({'passed':True,'source':'synthetic_not_real_robot','frames_received':samples[-1]['frames'],'joint_motion_observed':True,'synthetic_never_labeled_live':True,'disconnect_holds_pose':True,'samples':samples},indent=2))
            unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
    except Exception:
        (out/'live_udp_integration_test.json').write_text(json.dumps({'passed':False,'error':traceback.format_exc(),'samples':samples},indent=2))
        if process.poll() is None:process.terminate()
        unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
