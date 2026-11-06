import unreal,time,json,traceback
from pathlib import Path
OUT=Path(unreal.Paths.project_dir())/'SourceAssets/UnitreeGo2/Generated'
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Robots/UnitreeGo2/Maps/L_Go2_Preview')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for command in ['r.EyeAdaptationQuality 0','r.DefaultFeature.AutoExposure 0','r.ScreenPercentage 100']:
    unreal.SystemLibrary.execute_console_command(world,command)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
camera=next(a for a in actors if a.get_actor_label()=='Go2_Preview_Camera')
camera.set_actor_location(unreal.Vector(155,-240,115),False,False)
direction=unreal.Vector(0,0,20)-camera.get_actor_location()
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),unreal.Vector(0,0,20)),False)
camera.camera_component.set_field_of_view(45.0)
unreal.EditorLevelLibrary.set_level_viewport_camera_info(camera.get_actor_location(),camera.get_actor_rotation())
levels.save_current_level()
for a in actors:
    if a.get_actor_label().startswith('Go2_'):
        c=a.get_component_by_class(unreal.SkeletalMeshComponent)
        if c:c.set_update_animation_in_editor(True)
start=time.monotonic();captured=False
def tick(dt):
    global captured
    elapsed=time.monotonic()-start
    try:
        if elapsed>20 and not captured:
            unreal.AutomationLibrary.take_high_res_screenshot(1280,900,str(OUT/'Go2_Unreal_Preview.png'),camera=camera,delay=2.0)
            captured=True
        if elapsed>35 and (OUT/'Go2_Unreal_Preview.png').exists():
            unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
        elif elapsed>90:
            (OUT/'render_error.txt').write_text('Screenshot timeout')
            unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
    except Exception:
        (OUT/'render_error.txt').write_text(traceback.format_exc());unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
