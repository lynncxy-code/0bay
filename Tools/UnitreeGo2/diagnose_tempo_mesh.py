import unreal as u,json
from pathlib import Path
levels=u.get_editor_subsystem(u.LevelEditorSubsystem);levels.load_level('/Game/Robots/UnitreeGo2/Maps/L_Go2_TempoPreview')
a=next(a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if isinstance(a,u.TempoGo2Course));a.start_course()
m=a.get_editor_property('robot_mesh');s=m.get_editor_property('skeletal_mesh')
d={'mesh':str(s),'mesh_methods':[x for x in dir(s) if any(t in x for t in ['bound','lod','nanite','cpu'])]}
for name in ['visible','hidden_in_game','render_in_main_pass','component_use_fixed_skel_bounds','bounds_scale','nanite_override_materials','relative_scale3d','physics_asset_override']:
    try:d[name]=str(m.get_editor_property(name))
    except:pass
for name in ['get_bounds','get_imported_bounds','get_lod_num','get_lod_count','get_resource_size_bytes']:
    try:d[name]=str(getattr(s,name)())
    except Exception as e:d[name]=str(e)
d['body_transform']=str(m.get_socket_transform('body'))
d['component_bounds']=str(u.SystemLibrary.get_component_bounds(m))
Path(u.Paths.project_dir(),'SourceAssets/UnitreeGo2/Generated/tempo_mesh_diagnosis.json').write_text(json.dumps(d,indent=2))
