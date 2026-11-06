import unreal as u,json
from pathlib import Path
editor=u.get_editor_subsystem(u.UnrealEditorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem);actors=u.get_editor_subsystem(u.EditorActorSubsystem)
MAIN='/Game/OfficeShell/Maps/Office_Demo_ArtMerged'
if not editor.get_editor_world().get_path_name().startswith(MAIN+'.'):assert levels.load_level(MAIN)
dog=next(a for a in actors.get_all_level_actors() if isinstance(a,u.TempoGo2Course))
dog.reset_course()
pos=dog.get_actor_location();camera=pos+u.Vector(150,200,150)
editor.set_level_viewport_camera_info(camera,u.MathLibrary.find_look_at_rotation(camera,pos+u.Vector(0,0,25)))
assert levels.set_current_level_by_name('L_Go2_TempoOffice')
actors.set_selected_level_actors([dog])
Path(u.Paths.project_saved_dir(),'Go2/tempo_office_opened.json').write_text(json.dumps({'opened':True,'main_map':MAIN,'actor':dog.get_path_name(),'location':pos.to_tuple(),'current_level':'L_Go2_TempoOffice'},indent=2))
