"""Install a light-free Go2 route sublevel into the current default office map."""
import unreal as u,json,shutil,hashlib,datetime
from pathlib import Path
P=Path(u.Paths.project_dir()).resolve();OUT=P/'Saved/Go2'
MAIN='/Game/OfficeShell/Maps/Office_Demo_ArtMerged'
SOURCE='/Game/Robots/UnitreeGo2/Maps/L_Go2_RoutePreview'
SUB='/Game/Robots/UnitreeGo2/Maps/L_Go2_OfficeRoute'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
editor=u.get_editor_subsystem(u.UnrealEditorSubsystem)
lib=u.EditorAssetLibrary
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
original_preview_hash=sha(P/'Content/Robots/UnitreeGo2/Maps/L_Go2_RoutePreview.umap')
backup=OUT/('StreamingBackup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir(parents=True)
main_file=P/'Content/OfficeShell/Maps/Office_Demo_ArtMerged.umap';shutil.copy2(main_file,backup/main_file.name)
new=not lib.does_asset_exist(SUB)
if new:
    assert lib.duplicate_asset(SOURCE,SUB)
    assert levels.load_level(SUB)
    for a in actors.get_all_level_actors():
        if not isinstance(a,u.Go2RouteActor):actors.destroy_actor(a)
    dog=next(a for a in actors.get_all_level_actors() if isinstance(a,u.Go2RouteActor))
    dog.set_actor_label('Go2_OfficeRoute_Demo')
    dog.set_folder_path('Robots/Go2')
    dog.set_actor_location(u.Vector(0,0,0),False,False)
    dog.set_editor_property('auto_start',True);dog.set_editor_property('loop',False)
    dog.reset_route()
    assert levels.save_current_level()
assert levels.load_level(MAIN)
world=editor.get_editor_world()
before={a.get_path_name():a.get_actor_transform().export_text() for a in actors.get_all_level_actors()}
before_levels=sorted(set(a.get_outer().get_path_name() for a in actors.get_all_level_actors()))
# Place the 2 m test route along the open circulation strip, at the existing office floor datum.
placement=u.Vector(2500,-1400,4320)
stream=u.GameplayStatics.get_streaming_level(world,SUB)
if not stream:
    stream=u.EditorLevelUtils.add_level_to_world_with_transform(world,SUB,u.LevelStreamingAlwaysLoaded,u.Transform(location=placement))
assert isinstance(stream,u.LevelStreamingAlwaysLoaded)
assert levels.set_current_level_by_name('Office_Demo_ArtMerged')
dog=next(a for a in actors.get_all_level_actors() if isinstance(a,u.Go2RouteActor) and a.get_outer().get_path_name().startswith(SUB+'.'))
dog.reset_route()
after={a.get_path_name():a.get_actor_transform().export_text() for a in actors.get_all_level_actors()}
assert all(after.get(k)==v for k,v in before.items()),'Existing actor transforms changed'
assert levels.save_current_level()

# Reload saved packages to prove the persistent map retains its streaming reference and transform.
assert levels.load_level(MAIN);world=editor.get_editor_world()
stream=u.GameplayStatics.get_streaming_level(world,SUB);assert isinstance(stream,u.LevelStreamingAlwaysLoaded)
subactors=[a for a in actors.get_all_level_actors() if a.get_outer().get_path_name().startswith(SUB+'.')]
assert len(subactors)==1 and isinstance(subactors[0],u.Go2RouteActor),[(a.get_actor_label(),a.get_class().get_name()) for a in subactors]
dog=subactors[0];dog.reset_route()
assert (dog.get_actor_location()-placement).length()<.01,dog.get_actor_location()
assert dog.start_route(),dog.get_editor_property('status')
observed=[]
for i in range(1200):
    dog.advance_simulation(1/60)
    state=str(dog.get_editor_property('state'))
    if not observed or observed[-1]!=state:observed.append(state)
    if 'COMPLETE' in state:break
assert 'COMPLETE' in observed[-1],observed
assert abs(dog.get_editor_property('distance_cm')-200)<.01
feet=[dog.get_editor_property('robot_mesh').get_bone_location_by_name(leg+'_foot_joint',u.BoneSpaces.WORLD_SPACE).z for leg in ['FR','FL','RR','RL']]
assert max(abs(z-(placement.z+2.2)) for z in feet)<.01,feet
dog.reset_route()
assert sha(P/'Content/Robots/UnitreeGo2/Maps/L_Go2_RoutePreview.umap')==original_preview_hash
report={'passed':True,'main_map':MAIN,'sublevel':SUB,'streaming_class':stream.get_class().get_name(),
        'level_transform':str(stream.get_editor_property('level_transform')),'placement_cm':placement.to_tuple(),
        'sublevel_actor_count':len(subactors),'existing_actor_count':len(before),'existing_transforms_preserved':True,
        'prior_levels':before_levels,'source_preview_unchanged':True,'added_lights':0,'added_studio_floor':False,
        'auto_start_on_play':dog.get_editor_property('auto_start'),'sequence_states':observed,'final_feet_world_z_cm':feet,
        'backup':str(backup),'three_final_routes':'not yet specified'}
(OUT/'office_streaming_install.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('GO2_OFFICE_STREAMING_INSTALLED_AND_VERIFIED')
