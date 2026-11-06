import unreal as u,json,random,shutil,datetime
from pathlib import Path
root=Path('D:/0bay/OfficeShell');out=root/'Saved/MatrixLinks'
actors=u.get_editor_subsystem(u.EditorActorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem);lib=u.EditorAssetLibrary;ml=u.MaterialEditingLibrary
assert not levels.is_in_play_in_editor()
assert hasattr(u.NiagaraComponent,'set_variable_vec2') and hasattr(u.NiagaraComponent,'set_variable_vec3')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/OfficeShell/Maps/Office_Demo_ArtMerged.')
all_actors=actors.get_all_level_actors();by_path={a.get_path_name():a for a in all_actors}
plan=json.loads((out/'security_business_pairs.json').read_text(encoding='utf-8'))
assert len(plan['pairs'])==10
# Require actual current security-business tags, not a device-name guess.
def resolve(record):
 group=[by_path[p] for p in record['actors']]
 for a in group:
  tags=[str(t) for t in a.tags]
  assert 'OntoTwinBusiness=security' in tags,(record['name'],'not bound to security business')
  assert 'OntoTwinInstanceId='+record['id'] in tags,(record['name'],'ID mismatch')
 mins=[];maxs=[]
 for a in group:
  for mesh in a.get_components_by_class(u.StaticMeshComponent):
   if not mesh.get_editor_property('static_mesh'):continue
   c,e,r=u.SystemLibrary.get_component_bounds(mesh);mins.append(c-e);maxs.append(c+e)
 assert mins
 lo=u.Vector(min(v.x for v in mins),min(v.y for v in mins),min(v.z for v in mins))
 hi=u.Vector(max(v.x for v in maxs),max(v.y for v in maxs),max(v.z for v in maxs))
 return (lo+hi)*.5
resolved=[(resolve(p['source']),resolve(p['target'])) for p in plan['pairs']]
# Preserve the current map and all user-added actors in the VFX sublevel.
backup=out/('CommunicationBackup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir()
for rel in ['Content/MatrixDeviceLinks/L_MatrixDeviceLinks.umap','Saved/MatrixLinks/installed.json']:
 src=root/rel
 if src.exists():shutil.copy2(src,backup/src.name)
existing=sorted([a for a in all_actors if a.actor_has_tag('MatrixDeviceLink')],key=lambda a:a.get_actor_label())
assert len(existing)==18,'Expected the original 18 strips; inspect before replacing'
assert levels.set_current_level_by_name('L_MatrixDeviceLinks')
plane=u.load_asset('/Engine/BasicShapes/Plane')
rng=random.Random(11032027);styles=list(range(1,11));rng.shuffle(styles)
anomaly_indices=rng.sample(range(10),3);anomaly_styles=[2,13,14];rng.shuffle(anomaly_styles)
anomalies=dict(zip(anomaly_indices,anomaly_styles));report=[];created_vfx=[]
for i,(pair,(start,end)) in enumerate(zip(plan['pairs'],resolved)):
 direction=end-start;length=direction.length();assert length>1
 style=styles[i];width=2.5 if style<=5 else 5.0
 inst=lib.duplicate_asset('/Game/MatrixDeviceLinks/Preview/MI_Pulse_%02d'%style,'/Game/MatrixDeviceLinks/MI_Communication_%02d'%(i+1));assert inst
 ml.set_material_instance_scalar_parameter_value(inst,'Phase',rng.random())
 # Higher emission keeps thin packets readable under office daylight.
 colors=[u.LinearColor(.2,10,1.5,1),u.LinearColor(.2,6,10,1),u.LinearColor(10,5,.3,1),u.LinearColor(6,1,10,1),u.LinearColor(8,8,8,1)]
 ml.set_material_instance_vector_parameter_value(inst,'Color',colors[(style-1)%5]);lib.save_loaded_asset(inst)
 state='normal';native=anomalies.get(i)
 if native:state='fault' if native==13 else 'high_risk'
 strips=[]
 for face in range(2):
  idx=i*2+face
  actor=existing[idx] if idx<len(existing) else actors.spawn_actor_from_class(u.StaticMeshActor,(start+end)*.5)
  rot=u.MathLibrary.make_rot_from_xz(direction,u.Vector(0,0,1) if face==0 else u.Vector(0,1,0))
  actor.set_actor_location_and_rotation((start+end)*.5,rot,False,True);actor.set_actor_scale3d(u.Vector(length/100,width/100,1));actor.set_actor_label('COMM_%02d_P%02d_%s_%d'%(i+1,style,state,face));actor.set_folder_path('VFX/MatrixDeviceLinks/Communication')
  c=actor.static_mesh_component;c.set_static_mesh(plane);c.set_material(0,inst);c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);c.set_editor_property('cast_shadow',False)
  actor.set_editor_property('tags',['MatrixDeviceLink','CommunicationDemo','SimulatedState='+state,'SourceId='+pair['source']['id'],'TargetId='+pair['target']['id']]);strips.append(actor.get_path_name())
 if native:
  # Native symbol streams are localized along this connection, directed from source to target.
  system=u.load_asset('/Game/RainSymbols_VFX/Niagara_VFX/NS_Symbols_%02d'%native);assert system
  for k,fraction in enumerate([.25,.55,.8]):
   effect=actors.spawn_actor_from_class(u.NiagaraActor,start+direction*fraction,u.MathLibrary.make_rot_from_z(-direction))
   effect.set_actor_label('COMM_%02d_DEMO_%s_STYLE_%02d_%d'%(i+1,state,native,k));effect.set_folder_path('VFX/MatrixDeviceLinks/SimulatedAlerts');effect.set_actor_scale3d(u.Vector(.18,.18,.18))
   c=effect.get_component_by_class(u.NiagaraComponent);c.set_asset(system);c.set_variable_vec3('User.Box Size',u.Vector(8,8,8));c.set_variable_float('User.- Global Density (0-1)',.3)
   c.set_variable_vec2('User.Min/Max Symbols Size',u.Vector2D(18,24));c.set_variable_vec2('User.Min/Max Symbols Lifetime',u.Vector2D(.5,.9));c.set_editor_property('allow_scalability',False);c.set_preview_lod_distance(True,0,100000);c.activate(True)
   effect.set_editor_property('tags',['CommunicationDemo','SimulatedState='+state,'SourceId='+pair['source']['id'],'TargetId='+pair['target']['id']]);created_vfx.append(effect.get_path_name())
 report.append({'line':i+1,'source':pair['source'],'target':pair['target'],'start':start.to_tuple(),'end':end.to_tuple(),'pulse_style':'P%02d'%style,'width_cm':width,'simulated_state':state,'alert_style':('%02d'%native) if native else None,'strips':strips})
assert levels.save_current_level()
result={'simulation_only':True,'scope':'OntoTwinBusiness=security','independent_lines':10,'unique_devices':len({r[k]['id'] for r in report for k in ['source','target']}),'normal_lines':7,'simulated_alert_lines':3,'native_alert_emitters':created_vfx,'connections':report,'all_source_business_tags_verified':True}
(out/'communication_demo.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
# Stage a review view near the first simulated alert; do not change gameplay camera settings.
focus=resolved[anomaly_indices[0]][0];view=focus+u.Vector(350,550,80)
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(view,u.MathLibrary.find_look_at_rotation(view,focus))
actors.set_selected_level_actors([])
print('COMMUNICATION_DEMO_READY: 10 independent links, 20 security-business devices, 3 simulated alerts')
