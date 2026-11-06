import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Saved/Go2'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo_ArtMerged')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
data=[]
for a in actors:
    center,extent=a.get_actor_bounds(False)
    item={'label':a.get_actor_label(),'class':a.get_class().get_name(),'level':a.get_outer().get_path_name(),
          'location':a.get_actor_location().to_tuple(),'rotation':str(a.get_actor_rotation()),'center':center.to_tuple(),'extent':extent.to_tuple()}
    if isinstance(a,u.CameraActor):item['fov']=a.camera_component.field_of_view
    data.append(item)
(out/'main_scene_inventory.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
result={'actor_count':len(data),'levels':sorted(set(x['level'] for x in data)),
        'editor_camera':str(u.get_editor_subsystem(u.UnrealEditorSubsystem).get_level_viewport_camera_info()),
        'focus_actors':[x for x in data if any(k in x['class'] for k in ['Camera','PlayerStart','Light','Go2'])]}
(out/'main_streaming_inspection.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print('GO2_MAIN_INSPECTION_OK',len(data))
