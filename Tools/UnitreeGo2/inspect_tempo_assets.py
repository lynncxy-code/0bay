import unreal as u,json,traceback
from pathlib import Path
out=Path(u.Paths.project_dir())/'SourceAssets/UnitreeGo2/Generated'
bp=u.load_asset('/TempoQuadruped/Blueprints/BP_Unitree_Go2');assert bp
u.BlueprintEditorLibrary.compile_blueprint(bp)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
a=actors.spawn_actor_from_class(bp.generated_class(),u.Vector(0,0,100))
report={'class':a.get_class().get_path_name(),'components':[]}
for c in a.get_components_by_class(u.ActorComponent):
    d={'name':c.get_name(),'class':c.get_class().get_name()}
    for p in ['relative_location','relative_rotation','relative_scale3d','skeletal_mesh','control_rig_class','capsule_half_height','capsule_radius','max_walk_speed','orient_rotation_to_movement','use_controller_desired_rotation','run_physics_with_no_controller','update_rig_on_tick','update_in_editor']:
        try:d[p]=str(c.get_editor_property(p))
        except Exception:pass
    if isinstance(c,u.ControlRigComponent):
        c.initialize();d['rig_controls']=str(c.get_control_rig().get_hierarchy().get_all_keys())
    if isinstance(c,u.SkeletalMeshComponent):
        d['bones']=[str(c.get_bone_name(i)) for i in range(c.get_num_bones())]
    report['components'].append(d)
rig=u.load_asset('/TempoQuadruped/Animations/SKM_Unitree_Go2_CtrlRig')
report['hierarchy']=[{'name':str(k.name),'type':str(k.type),'initial_global':str(rig.hierarchy.get_global_transform(k,True))} for k in rig.hierarchy.get_all_keys()]
try:
    model=rig.get_model();report['rig_nodes']=[]
    for n in model.get_nodes():
        report['rig_nodes'].append({'name':n.get_name(),'title':n.get_node_title(),'pins':[{'path':p.get_pin_path(),'default':p.get_default_value(),'links':[str(l) for l in p.get_links()]} for p in n.get_pins()]})
except Exception:report['graph_error']=traceback.format_exc()
try:report['blueprint_graphs']=str(bp.get_editor_property('ubergraph_pages'))
except Exception:pass
(out/'tempo_asset_inspection.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
actors.destroy_actor(a)
print('TEMPO_INSPECTION_COMPLETE')
