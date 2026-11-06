import unreal,time,traceback,json
from pathlib import Path
OUT=Path(unreal.Paths.project_dir())/'SourceAssets/UnitreeGo2/Generated'
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Robots/UnitreeGo2/Maps/L_Go2_RoutePreview')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
dog=next(a for a in actors if isinstance(a,unreal.Go2RouteActor))
assert dog.start_route()
for _ in range(315):dog.advance_simulation(1/60)
camera=next(a for a in actors if a.get_actor_label()=='Go2_Preview_Camera')
body=dog.get_editor_property('robot_mesh').get_world_location()
focus=unreal.Vector(body.x,body.y,22)
camera.set_actor_location(focus+unreal.Vector(140,-200,90),False,False)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),focus),False)
camera.camera_component.set_field_of_view(40)
unreal.EditorLevelLibrary.set_level_viewport_camera_info(camera.get_actor_location(),camera.get_actor_rotation())
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for cmd in ['r.EyeAdaptationQuality 0','r.DefaultFeature.AutoExposure 0','r.ScreenPercentage 100']:
    unreal.SystemLibrary.execute_console_command(world,cmd)
start=time.monotonic();captured=False
def render_tick(dt):
    global captured
    try:
        elapsed=time.monotonic()-start
        if elapsed>15 and not captured:
            unreal.AutomationLibrary.take_high_res_screenshot(1200,800,str(OUT/'Go2_Offline_Walk_Preview.png'),camera=camera,delay=2)
            captured=True
        if elapsed>26 and (OUT/'Go2_Offline_Walk_Preview.png').exists():
            (OUT/'route_render_validation.json').write_text(json.dumps({'screenshot':'Go2_Offline_Walk_Preview.png','state':str(dog.get_editor_property('state')),'distance_cm':dog.get_editor_property('distance_cm')},indent=2))
            unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
        elif elapsed>70:
            raise RuntimeError('Offline route screenshot timeout')
    except Exception:
        (OUT/'route_render_error.txt').write_text(traceback.format_exc())
        unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(render_tick)
