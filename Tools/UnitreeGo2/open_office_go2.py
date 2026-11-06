"""Open and frame the streamed Go2 in the main office; leave normal Play behavior intact."""
import unreal as u,json,time,traceback
from pathlib import Path
out=Path(u.Paths.project_saved_dir())/'Go2'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
editor=u.get_editor_subsystem(u.UnrealEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
world=editor.get_editor_world()
if not world.get_path_name().startswith('/Game/OfficeShell/Maps/Office_Demo_ArtMerged.'):
    assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo_ArtMerged');world=editor.get_editor_world()
dog=next(a for a in actors.get_all_level_actors() if isinstance(a,u.Go2RouteActor))
dog.reset_route()
camera_pos=u.Vector(2420,-1700,4480)
camera_rotation=u.MathLibrary.find_look_at_rotation(camera_pos,u.Vector(2600,-1400,4345))
editor.set_level_viewport_camera_info(camera_pos,camera_rotation)
actors.set_selected_level_actors([dog])
report={'opened':True,'dog':dog.get_path_name(),'position':dog.get_actor_location().to_tuple(),'camera':camera_pos.to_tuple()}
try:
    report['trace_doc']=u.SystemLibrary.box_trace_multi.__doc__
    hits=u.SystemLibrary.box_trace_multi(world,u.Vector(2500,-1400,4350),u.Vector(2700,-1400,4350),
        u.Vector(35,20,20),u.Rotator(),u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[dog],u.DrawDebugTrace.NONE,True)
    report['route_obstruction_trace']=str(hits)
except Exception:report['trace_error']=traceback.format_exc()
(out/'office_visible_preview.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('GO2_OFFICE_MAIN_OPENED')
