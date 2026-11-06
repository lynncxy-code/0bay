import unreal as u,json,math,shutil,datetime,traceback
from pathlib import Path
root=Path('D:/0bay/OfficeShell');out=root/'Saved/MatrixLinks'
def revise():
 actors=u.get_editor_subsystem(u.EditorActorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
 assert not levels.is_in_play_in_editor(),'Stop play before editing'
 data=json.loads((out/'communication_demo.json').read_text(encoding='utf-8'));by={a.get_path_name():a for a in actors.get_all_level_actors()}
 bk=out/('LowArcBackup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));bk.mkdir();shutil.copy2(root/'Content/MatrixDeviceLinks/L_MatrixDeviceLinks.umap',bk/'L_MatrixDeviceLinks.umap');shutil.copy2(out/'communication_demo.json',bk/'communication_demo.json')
 roof=data['arc_roof_z'];cap=roof-30;report=[]
 for row in data['connections']:
  s=u.Vector(*row['start']);e=u.Vector(*row['end']);d=e-s
  assert max(s.z,e.z)<cap
  lo=0.;hi=cap-min(s.z,e.z)
  for j in range(48):
   h=(lo+hi)/2;t=max(0.,min(1.,.5+d.z/(8*h))) if h else 0
   peak=s.z+d.z*t+4*h*t*(1-t)
   if peak>cap:hi=h
   else:lo=h
  h=lo
  for path in row['strips']:
   c=by[path].get_component_by_class(u.SplineMeshComponent);assert c
   c.set_start_and_end(u.Vector(0,0,0),d+u.Vector(0,0,4*h),d,d-u.Vector(0,0,4*h),True)
  if row['alert_style']:
   effects=sorted([by[p] for p in data['native_alert_emitters'] if by[p].get_actor_label().startswith('COMM_%02d_'%row['line'])],key=lambda a:a.get_actor_label());assert len(effects)==3
   for effect,t in zip(effects,[.25,.55,.8]):
    effect.set_actor_location_and_rotation(s+d*t+u.Vector(0,0,4*h*t*(1-t)),u.MathLibrary.make_rot_from_z(-(d+u.Vector(0,0,4*h*(1-2*t)))),False,True)
    c=effect.get_component_by_class(u.NiagaraComponent)
    c.set_variable_vec3('User.Box Size',u.Vector(8,8,8))
    c.set_variable_vec2('User.Min/Max Symbols Size',u.Vector2D(.05,.08))
    c.reinitialize_system()
  row['arc_height_cm']=h;row['midpoint_z']=(s.z+e.z)/2+h;row['max_z']=cap
  report.append({'line':row['line'],'max_z':cap})
 assert levels.set_current_level_by_name('L_MatrixDeviceLinks');assert levels.save_current_level()
 data.update({'arc_clearance_cm':-30,'symbol_size_range':[.05,.08],'symbol_distribution_restored':True});data.pop('alert_symbol_width_factor',None)
 (out/'communication_demo.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
 (out/'low_arc_result.json').write_text(json.dumps({'ok':True,'links':len(report),'roof_z':roof,'max_z':cap,'symbol_size_range':[.05,.08],'distribution':[8,8,8]},indent=2),encoding='utf-8')
 print('LOW_ARCS_MICRO_SYMBOLS_SAVED')
try:revise()
except Exception:
 (out/'low_arc_error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
