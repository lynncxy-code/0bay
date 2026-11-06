import unreal as u,time,traceback,json
from pathlib import Path
OUT=Path(u.Paths.project_dir())/'SourceAssets/UnitreeGo2/Generated'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem);assert levels.load_level('/Game/Robots/UnitreeGo2/Maps/L_Go2_TempoPreview')
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
course=next(a for a in actors if isinstance(a,u.TempoGo2Course));assert course.start_course()
for i in range(18*60):course.advance_course(1/60)
mesh=course.get_editor_property('robot_mesh')

pos=course.get_editor_property('body_world_position')
camera=next(a for a in actors if isinstance(a,u.CameraActor))
camera.set_actor_location(pos+u.Vector(140,-220,100),False,False)
camera.set_actor_rotation(u.MathLibrary.find_look_at_rotation(camera.get_actor_location(),pos),False)
camera.camera_component.set_field_of_view(45)
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(camera.get_actor_location(),camera.get_actor_rotation())
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
for cmd in ['r.EyeAdaptationQuality 0','r.DefaultFeature.AutoExposure 0','r.ScreenPercentage 100','t.MaxFPS 30']:u.SystemLibrary.execute_console_command(world,cmd)
started=time.monotonic();captured=False
def tick(dt):
    global captured
    try:
        elapsed=time.monotonic()-started
        if elapsed>11 and elapsed<12:
            (OUT/'tempo_render_state.json').write_text(json.dumps({'body':mesh.get_socket_location('body').to_tuple(),'bounds':[str(x) for x in course.get_actor_bounds(False)]},indent=2))
        if elapsed>12 and not captured:
            u.AutomationLibrary.take_high_res_screenshot(1200,800,str(OUT/'Go2_Tempo_Turning.png'),camera=camera,delay=1);captured=True
        if elapsed>22 and (OUT/'Go2_Tempo_Turning.png').exists():
            u.unregister_slate_post_tick_callback(handle);u.SystemLibrary.quit_editor()
        elif elapsed>70:raise RuntimeError('Tempo screenshot timed out')
    except Exception:
        (OUT/'tempo_render_error.txt').write_text(traceback.format_exc());u.unregister_slate_post_tick_callback(handle);u.SystemLibrary.quit_editor()
handle=u.register_slate_post_tick_callback(tick)



