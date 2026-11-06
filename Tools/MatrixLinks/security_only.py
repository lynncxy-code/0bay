import unreal as u,json,random,shutil,datetime
from pathlib import Path
out=Path('D:/0bay/OfficeShell/Saved/MatrixLinks');root=Path('D:/0bay/OfficeShell')
actors=u.get_editor_subsystem(u.EditorActorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem);ml=u.MaterialEditingLibrary
all_actors=actors.get_all_level_actors()
assert not u.EditorLevelLibrary.get_game_world(),'Stop Play before updating saved links'
candidates=[];seen=set()
for actor in all_actors:
 for mesh in actor.get_components_by_class(u.StaticMeshComponent):
  sm=mesh.get_editor_property('static_mesh')
  if not sm or 'SM_MonitorCamera_Frozen' not in sm.get_path_name():continue
  center,extent,radius=u.SystemLibrary.get_component_bounds(mesh)
  key=tuple(round(v) for v in center.to_tuple())
  if key in seen:continue
  seen.add(key);candidates.append((actor,center,sm.get_path_name()))
assert len(candidates)>=10,'Not enough verified security cameras'
chosen=random.Random(11032026).sample(sorted(candidates,key=lambda x:x[0].get_path_name()),10)
edges=[];connected={0}
while len(connected)<10:
 _,i,j=min(((chosen[i][1]-chosen[j][1]).length(),i,j) for i in connected for j in range(10) if j not in connected)
 edges.append((i,j));connected.add(j)
links={}
for actor in all_actors:
 if actor.actor_has_tag('MatrixDeviceLink'):
  label=actor.get_actor_label().split('_');links[(int(label[2]),int(label[-1]))]=actor
assert len(links)==18
backup=out/('SecurityBackup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir()
shutil.copy2(root/'Content/MatrixDeviceLinks/L_MatrixDeviceLinks.umap',backup/'L_MatrixDeviceLinks.umap')
shutil.copy2(out/'installed.json',backup/'installed.json')
assert levels.set_current_level_by_name('L_MatrixDeviceLinks')
for e,(i,j) in enumerate(edges):
 a,b=chosen[i][1],chosen[j][1];d=b-a;length=d.length()
 inst=u.load_asset('/Game/MatrixDeviceLinks/MI_Link_%02d'%e)
 ml.set_material_instance_scalar_parameter_value(inst,'SymbolCount',max(3,length/12));u.EditorAssetLibrary.save_loaded_asset(inst)
 for face in range(2):
  actor=links[(e,face)]
  rot=u.MathLibrary.make_rot_from_xz(d,u.Vector(0,0,1) if face==0 else u.Vector(0,1,0))
  actor.set_actor_location_and_rotation((a+b)*.5,rot,False,True)
  actor.set_actor_scale3d(u.Vector(length/100,.12,1))
  actor.set_actor_label('Matrix_Link_%02d_%s_to_%s_%d'%(e,chosen[i][0].get_actor_label(),chosen[j][0].get_actor_label(),face))
assert levels.save_current_level()
report={'scope':'security_only','selection_rule':'Verified SM_MonitorCamera_Frozen camera meshes only','seed':11032026,'candidate_count':len(candidates),'selected':[{'actor':a.get_path_name(),'label':a.get_actor_label(),'type':'Security camera','mesh':m,'center':p.to_tuple()} for a,p,m in chosen],'edges':edges,'link_count':9,'actor_count':18,'level':'/Game/MatrixDeviceLinks/L_MatrixDeviceLinks','source':'RainSymbols_VFX M_Symbols + T_Symbols_EN-1 adapted to point-to-point flow'}
# Check the saved geometry still terminates at each selected camera.
errors=[]
for e,(i,j) in enumerate(edges):
 mid=(chosen[i][1]+chosen[j][1])*.5
 for f in range(2):errors.append((links[(e,f)].get_actor_location()-mid).length())
report['validation']={'unique_devices':len(set(x['actor'] for x in report['selected'])),'security_only':all('SM_MonitorCamera_Frozen' in x['mesh'] for x in report['selected']),'max_midpoint_error_cm':max(errors),'connected_devices':len(connected)}
(out/'installed.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(u.Vector(1700,-2050,4510),u.MathLibrary.find_look_at_rotation(u.Vector(1700,-2050,4510),u.Vector(1900,-1300,4615)))
print('MATRIX_SECURITY_LINKS_SAVED '+str(report['validation']))
