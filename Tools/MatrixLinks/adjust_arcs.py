import unreal as u,json,traceback,shutil,datetime
from pathlib import Path
root=Path('D:/0bay/OfficeShell');out=root/'Saved/MatrixLinks'
def apply_arcs():
 actors=u.get_editor_subsystem(u.EditorActorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem);lib=u.EditorAssetLibrary
 assert not levels.is_in_play_in_editor()
 data=json.loads((out/'communication_demo.json').read_text(encoding='utf-8'))
 aa=actors.get_all_level_actors();by={a.get_path_name():a for a in aa}
 old=[by[p] for row in data['connections'] for p in row['strips']]
 assert len(old)==20 and all(a.actor_has_tag('MatrixDeviceLink') for a in old)
 bk=out/('ArcBackup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));bk.mkdir()
 shutil.copy2(root/'Content/MatrixDeviceLinks/L_MatrixDeviceLinks.umap',bk/'L_MatrixDeviceLinks.umap');shutil.copy2(out/'communication_demo.json',bk/'communication_demo.json')
 roofs=[]
 for a in aa:
  label=a.get_actor_label().lower()
  if 'officeshell' in label and ('ceiling' in label or 'roof' in label):
   c,e=a.get_actor_bounds(False);top=c.z+e.z
   if 4600<top<6000:roofs.append((a.get_actor_label(),top))
 roof=max([v for k,v in roofs] or [4700.0]);apex=roof+180.0
 meshpath='/Game/MatrixDeviceLinks/SM_ArcRibbon'
 mesh=u.load_asset(meshpath)
 if not mesh:
  task=u.AssetImportTask();task.filename=str(root/'Tools/MatrixLinks/ArcRibbon.obj');task.destination_path='/Game/MatrixDeviceLinks';task.destination_name='SM_ArcRibbon';task.automated=True;task.save=True
  opt=u.FbxImportUI();opt.import_mesh=True;opt.import_materials=False;opt.import_textures=False;opt.import_as_skeletal=False;opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
  opt.static_mesh_import_data.set_editor_property('combine_meshes',True);opt.static_mesh_import_data.set_editor_property('generate_lightmap_u_vs',False);task.options=opt
  u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);mesh=u.load_asset(meshpath)
 assert mesh,'Ribbon import failed'
 bounds=mesh.get_bounds();assert abs(bounds.box_extent.x-50)<1,(str(bounds),'Unexpected ribbon axis')
 mat=u.load_asset('/Game/MatrixDeviceLinks/Preview/M_CommunicationPulse');mat.set_editor_property('used_with_spline_meshes',True);lib.save_loaded_asset(mat)
 assert levels.set_current_level_by_name('L_MatrixDeviceLinks')
 created=[];report=[]
 for row in data['connections']:
  start=u.Vector(*row['start']);end=u.Vector(*row['end']);delta=end-start;h=max(180,apex-(start.z+end.z)/2)
  a=actors.spawn_actor_from_class(u.SplineMeshActor,start);created.append(a)
  c=a.get_component_by_class(u.SplineMeshComponent);c.set_static_mesh(mesh);c.set_material(0,old[(row['line']-1)*2].static_mesh_component.get_material(0));c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);c.set_editor_property('cast_shadow',False)
  c.set_forward_axis(u.SplineMeshAxis.X,False);c.set_spline_up_dir(u.Vector(0,0,1),False);c.set_start_scale(u.Vector2D(row['width_cm']/100,row['width_cm']/100),False);c.set_end_scale(u.Vector2D(row['width_cm']/100,row['width_cm']/100),False)
  c.set_start_and_end(u.Vector(0,0,0),delta+u.Vector(0,0,4*h),delta,delta-u.Vector(0,0,4*h),True)
  a.set_actor_label('COMM_%02d_%s_%s_ARC'%(row['line'],row['pulse_style'],row['simulated_state']));a.set_folder_path('VFX/MatrixDeviceLinks/Communication');a.tags=list(old[(row['line']-1)*2].tags)
  if row['alert_style']:
   effects=sorted([by[p] for p in data['native_alert_emitters'] if by[p].get_actor_label().startswith('COMM_%02d_'%row['line'])],key=lambda x:x.get_actor_label());assert len(effects)==3
   for effect,t in zip(effects,[.25,.55,.8]):
    pos=start+delta*t+u.Vector(0,0,4*h*t*(1-t));tangent=delta+u.Vector(0,0,4*h*(1-2*t))
    effect.set_actor_location_and_rotation(pos,u.MathLibrary.make_rot_from_z(-tangent),False,True)
    nc=effect.get_component_by_class(u.NiagaraComponent);nc.set_variable_vec3('User.Box Size',u.Vector(1.6,1.6,8));nc.set_variable_vec2('User.Min/Max Symbols Size',u.Vector2D(3.6,4.8));nc.reinitialize_system()
  row['legacy_strips']=row['strips'];row['strips']=[a.get_path_name()];row['arc_height_cm']=h;row['midpoint_z']=(start.z+end.z)/2+h
  report.append({'line':row['line'],'midpoint_z':row['midpoint_z'],'roof_z':roof})
 # Retain originals hidden for reversible adjustment, without touching unrelated actors.
 for a in old:
  a.static_mesh_component.set_visibility(False,True);a.set_actor_hidden_in_game(True);a.tags=[t for t in a.tags if str(t)!='MatrixDeviceLink'];a.set_folder_path('VFX/MatrixDeviceLinks/ArchivedStraightLinks')
 assert len(created)==10 and all(r['midpoint_z']>=roof+180 for r in report)
 assert levels.save_current_level()
 data.update({'arc_roof_z':roof,'arc_clearance_cm':180,'alert_symbol_width_factor':.2,'geometry':'parabola'})
 (out/'communication_demo.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
 (out/'arc_adjustment_result.json').write_text(json.dumps({'ok':True,'links':10,'alert_emitters':9,'width_factor':.2,'roof_candidates':roofs,'arcs':report},ensure_ascii=False,indent=2),encoding='utf-8')
 print('ARC_ADJUSTMENT_SAVED')
try:apply_arcs()
except Exception:
 (out/'arc_adjustment_error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise

