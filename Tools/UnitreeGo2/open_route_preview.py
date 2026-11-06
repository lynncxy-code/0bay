"""Visible editor preview. Replays the test route without changing final sequence presets or saving the map."""
import unreal, time, json, traceback
from pathlib import Path

OUT=Path(unreal.Paths.project_dir())/'Saved/Go2'
OUT.mkdir(parents=True,exist_ok=True)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Robots/UnitreeGo2/Maps/L_Go2_RoutePreview')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
dog=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.Go2RouteActor))
dog.reset_route()
camera_location=unreal.Vector(190,-390,195)
camera_rotation=unreal.MathLibrary.find_look_at_rotation(camera_location,unreal.Vector(0,0,25))
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(camera_location,camera_rotation)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for command in ['r.EyeAdaptationQuality 0','r.DefaultFeature.AutoExposure 0','r.ScreenPercentage 100','t.MaxFPS 45']:
    unreal.SystemLibrary.execute_console_command(world,command)
label=actors.spawn_actor_from_class(unreal.TextRenderActor,unreal.Vector(0,25,105),unreal.Rotator(),True)
label.set_actor_label('Go2_Preview_Status_Transient')
label.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(label.get_actor_location(),camera_location),False)
text=label.get_component_by_class(unreal.TextRenderComponent)
text.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
text.set_world_size(7)
text.set_text_render_color(unreal.Color(215,235,255,255))
text.set_text('Go2 / 2 m test route\nReady - preview repeats')
actors.set_selected_level_actors([])
start=time.monotonic();finished_at=None;cycles=0;last_state=None;history=[]
names={'CROUCHED':'Ready','STANDING_UP':'Stand up','WALKING':'Walk along route',
       'SETTLING_FEET':'Stop and settle','STANDING_PAUSE':'Stand still','LYING_DOWN':'Lie down','COMPLETE':'Finished - replay in 3 seconds'}
def preview_tick(dt):
    global finished_at,cycles,last_state
    try:
        if not unreal.SystemLibrary.is_valid(dog):
            unreal.unregister_slate_post_tick_callback(handle);return
        if unreal.EditorLevelLibrary.get_game_world() is not None:return
        now=time.monotonic()
        if now-start<5:return
        state=str(dog.get_editor_property('state')).split('.')[-1].split(':')[0].rstrip('>')
        if state=='CROUCHED' or (finished_at is not None and now-finished_at>=3):
            assert dog.start_route(),dog.get_editor_property('status')
            finished_at=None;cycles+=1
        dog.advance_simulation(min(dt,.1))
        state=str(dog.get_editor_property('state')).split('.')[-1].split(':')[0].rstrip('>')
        if state=='COMPLETE' and finished_at is None:finished_at=now
        if state!=last_state:
            last_state=state;history.append({'cycle':cycles,'state':state,'elapsed_s':round(now-start,2)})
            text.set_text('Go2 / 2 m test route\n'+names.get(state,state))
            (OUT/'desktop_preview_status.json').write_text(json.dumps({'running':True,'cycle':cycles,'state':state,'history':history[-30:]},indent=2))
    except Exception:
        (OUT/'desktop_preview_error.txt').write_text(traceback.format_exc())
        unreal.unregister_slate_post_tick_callback(handle)
handle=unreal.register_slate_post_tick_callback(preview_tick)
unreal.log('GO2_VISIBLE_ROUTE_PREVIEW_READY')
