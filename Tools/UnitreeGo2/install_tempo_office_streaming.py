"""Replace old office Go2 instances with an editable always-loaded Tempo sublevel."""
import unreal as u,json,hashlib
from pathlib import Path
P=Path(u.Paths.project_dir()).resolve();OUT=P/'Saved/Go2'
MAIN='/Game/OfficeShell/Maps/Office_Demo_ArtMerged'
SOURCE='/Game/Robots/UnitreeGo2/Maps/L_Go2_TempoPreview'
OLD='/Game/Robots/UnitreeGo2/Maps/L_Go2_OfficeRoute'
SUB='/Game/Robots/UnitreeGo2/Maps/L_Go2_TempoOffice'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem);actors=u.get_editor_subsystem(u.EditorActorSubsystem);editor=u.get_editor_subsystem(u.UnrealEditorSubsystem);lib=u.EditorAssetLibrary
checkpoint=json.loads((OUT/'tempo_office_before.json').read_text(encoding='utf-8'))
assert all(Path(x['backup']).exists() for x in checkpoint['files'])
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source_file=P/'Content/Robots/UnitreeGo2/Maps/L_Go2_TempoPreview.umap';source_hash=sha(source_file)
old_file=P/'Content/Robots/UnitreeGo2/Maps/L_Go2_OfficeRoute.umap';old_hash=sha(old_file)
if not lib.does_asset_exist(SUB):assert lib.duplicate_asset(SOURCE,SUB)
assert levels.load_level(SUB)
for a in actors.get_all_level_actors():
    if not isinstance(a,u.TempoGo2Course):actors.destroy_actor(a)
dog=next(a for a in actors.get_all_level_actors() if isinstance(a,u.TempoGo2Course))
dog.set_actor_label('Go2_Tempo_OfficeRoute');dog.set_folder_path('Robots/Go2')
dog.set_actor_location(u.Vector(),False,False);dog.set_actor_rotation(u.Rotator(),False)
dog.set_editor_property('use_preview_camera',False);dog.set_editor_property('show_status',False)
dog.set_editor_property('show_route',True);dog.set_editor_property('auto_start',True);dog.set_editor_property('repeat',True)
editor.get_editor_world().get_world_settings().set_editor_property('default_game_mode',None)
dog.reset_course();assert levels.save_current_level()
assert levels.load_level(MAIN);world=editor.get_editor_world()
before={a.get_path_name():a.get_actor_transform().export_text() for a in actors.get_all_level_actors()}
old_dogs=[a for a in actors.get_all_level_actors() if isinstance(a,u.Go2RouteActor) or a.get_class().get_name()=='BP_Go2_C']
removed=[{'path':a.get_path_name(),'label':a.get_actor_label(),'location':a.get_actor_location().to_tuple()} for a in old_dogs]
previous_route=next((a for a in old_dogs if isinstance(a,u.Go2RouteActor)),None)
placement=previous_route.get_actor_location() if previous_route else u.Vector(2500,-1300,4320)
# User will place/shape the route. Preserve the old dog's current world position.
stream_old=u.GameplayStatics.get_streaming_level(world,OLD)
for a in old_dogs:
    if not a.get_outer().get_path_name().startswith(OLD+'.'):actors.destroy_actor(a)
if stream_old:
    loaded=stream_old.get_loaded_level();assert loaded
    assert u.EditorLevelUtils.remove_level_from_world(loaded)
stream=u.GameplayStatics.get_streaming_level(world,SUB)
if not stream:stream=u.EditorLevelUtils.add_level_to_world_with_transform(world,SUB,u.LevelStreamingAlwaysLoaded,u.Transform(location=placement))
assert isinstance(stream,u.LevelStreamingAlwaysLoaded)
dog=next(a for a in actors.get_all_level_actors() if isinstance(a,u.TempoGo2Course) and a.get_outer().get_path_name().startswith(SUB+'.'))
assert (dog.get_actor_location()-placement).length()<.1
assert not dog.get_editor_property('use_preview_camera')
dog.set_editor_property('repeat',False);assert dog.start_course()
samples=[]
for i in range(6000):
    dog.advance_course(1/60)
    if i%60==0:
        body=dog.get_editor_property('body_world_position')
        samples.append({'t':(i+1)/60,'state':dog.get_editor_property('state'),'body':body.to_tuple(),'distance':dog.get_editor_property('distance_cm')})
        assert (body-placement).length()<1200,'World/rig coordinate mismatch'
    if not dog.get_editor_property('running'):break
assert dog.get_editor_property('completed_stops')==4
assert dog.get_editor_property('state')=='Complete',dog.get_editor_property('state')
dog.reset_course();dog.set_editor_property('repeat',True)
after={a.get_path_name():a.get_actor_transform().export_text() for a in actors.get_all_level_actors()}
removed_paths={a['path'] for a in removed}
assert all(after.get(k)==v for k,v in before.items() if k not in removed_paths),'Unrelated scene actor changed'
assert len([a for a in actors.get_all_level_actors() if 'Go2' in a.get_class().get_name()])==1
assert levels.set_current_level_by_name('Office_Demo_ArtMerged');assert levels.save_current_level()
# Verify saved streaming reference and absence of the previous route.
assert levels.load_level(MAIN);world=editor.get_editor_world()
stream=u.GameplayStatics.get_streaming_level(world,SUB)
assert isinstance(stream,u.LevelStreamingAlwaysLoaded)
assert not u.GameplayStatics.get_streaming_level(world,OLD)
dogs=[a for a in actors.get_all_level_actors() if 'Go2' in a.get_class().get_name()]
assert len(dogs)==1 and isinstance(dogs[0],u.TempoGo2Course)
assert sha(source_file)==source_hash and sha(old_file)==old_hash
report={'passed':True,'main_map':MAIN,'sublevel':SUB,'actor_label':dogs[0].get_actor_label(),'streaming_class':stream.get_class().get_name(),
 'placement_cm':placement.to_tuple(),'removed_old_instances':removed,'existing_actors_preserved':len(before)-len(removed),
 'source_preview_unchanged':True,'old_sublevel_asset_preserved':True,'extra_floor_lights_camera_actors':0,'preview_camera_control':False,
 'route_length_cm':dogs[0].get_editor_property('route').get_spline_length(),'auto_start':True,'repeat':True,'validation_stops':4,
 'samples':samples,'backup':checkpoint['backup'],'position_and_route_adjustment':'user will adjust'}
(OUT/'tempo_office_streaming_install.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('TEMPO_OFFICE_STREAMING_INSTALLED',placement)
