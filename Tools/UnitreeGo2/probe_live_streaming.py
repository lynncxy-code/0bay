import unreal as u,json
from pathlib import Path
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
data={'world':world.get_path_name(),'route_class':str(u.load_class(None,'/Script/OfficeHost.Go2RouteActor')),
      'camera':str(u.get_editor_subsystem(u.UnrealEditorSubsystem).get_level_viewport_camera_info()),
      'actors':[],'streams':{}}
for a in actors:
    if 'UnitreeGo2' in a.get_path_name() or isinstance(a,u.PlayerStart):
        data['actors'].append({'label':a.get_actor_label(),'path':a.get_path_name(),'location':a.get_actor_location().to_tuple(),'class':a.get_class().get_name()})
for path in ['/Game/Robots/UnitreeGo2/Maps/L_Go2_Preview','/Game/Robots/UnitreeGo2/Maps/L_Go2_RoutePreview']:
    s=u.GameplayStatics.get_streaming_level(world,path)
    if s:data['streams'][path]={'class':s.get_class().get_name(),'transform':str(s.get_editor_property('level_transform'))}
(Path(u.Paths.project_saved_dir())/'Go2/live_streaming_probe.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
print('GO2_LIVE_STREAMING_PROBE_OK')
