import unreal as u,json
from pathlib import Path
out=Path(u.Paths.project_dir())/'Saved/Go2Scan'
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
data={'actors':[],'materials':{},'collections':{}}
for a in actors:
    meshes=a.get_components_by_class(u.MeshComponent)
    data['actors'].append({'label':a.get_actor_label(),'path':a.get_path_name(),'class':a.get_class().get_name(),'pos':a.get_actor_location().to_tuple(),'tags':[str(t) for t in a.tags],'meshes':[{'name':m.get_name(),'asset':str(m.get_editor_property('static_mesh')) if isinstance(m,u.StaticMeshComponent) else '', 'pos':m.get_world_location().to_tuple()} for m in meshes]})
for p in ['/Game/ScanGridEffect/Materials/PPI_Scan01','/Game/ScanningSystem/Materials/Outline/MI_PP_OutlineShader','/Game/ScanningSystem/Materials/TranslucentMaterials/MI_TranslucentOverlay']:
    m=u.load_asset(p); rec={}
    for kind in ['scalar','vector','static_switch']:
        names=getattr(u.MaterialEditingLibrary,'get_'+kind+'_parameter_names')(m)
        rec[kind]={str(n):str(getattr(u.MaterialEditingLibrary,'get_material_instance_'+kind+'_parameter_value')(m,n)) for n in names}
    data['materials'][p]=rec
for p in ['/Game/ScanGridEffect/Materials/MaterialFunctions/MPC_Scan']:
    m=u.load_asset(p);data['collections'][p]={'scalar':str(m.get_editor_property('scalar_parameters')),'vector':str(m.get_editor_property('vector_parameters'))}
(out/'inspection.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
