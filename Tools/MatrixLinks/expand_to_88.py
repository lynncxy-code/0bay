import unreal as u,json,math,collections,shutil,datetime,traceback
from pathlib import Path
ROOT=Path('D:/0bay/OfficeShell');OUT=ROOT/'Saved/MatrixLinks'
def run():
 levels=u.get_editor_subsystem(u.LevelEditorSubsystem);actors=u.get_editor_subsystem(u.EditorActorSubsystem)
 assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo_ArtMerged')
 aa=actors.get_all_level_actors();by={a.get_path_name():a for a in aa}
 members=json.loads((OUT/'security_88_members.json').read_text(encoding='utf-8'))
 data=json.loads((OUT/'communication_demo.json').read_text(encoding='utf-8'))
 rows=data['connections'];assert len(rows)==10
 active=[a for a in aa if a.actor_has_tag('MatrixDeviceLink')]
 assert len(active)==10,('unexpected active links',len(active))
 groups=collections.defaultdict(list)
 for a in aa:
  for tag in a.tags:
   t=str(tag)
   if t.startswith('OntoTwinInstanceId=') and t.split('=',1)[1] in members:groups[t.split('=',1)[1]].append(a)
 devices={}
 for iid,group in groups.items():
  mins=[];maxs=[]
  for a in group:
   for c in a.get_components_by_class(u.StaticMeshComponent):
    if not c.get_editor_property('static_mesh'):continue
    center,extent,radius=u.SystemLibrary.get_component_bounds(c);mins.append(center-extent);maxs.append(center+extent)
  if not mins:continue
  lo=u.Vector(min(v.x for v in mins),min(v.y for v in mins),min(v.z for v in mins));hi=u.Vector(max(v.x for v in maxs),max(v.y for v in maxs),max(v.z for v in maxs))
  devices[iid]={'record':dict(id=iid,**members[iid],actors=[a.get_path_name() for a in group]),'center':(lo+hi)*.5}
 assert len(devices)>=70,('missing loaded devices',len(devices))
 parent={i:i for i in devices}
 def find(i):
  while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
  return i
 def union(a,b):parent[find(a)]=find(b)
 used=set();styles=collections.Counter();templates={}
 for row in rows:
  a,b=row['source']['id'],row['target']['id'];assert a in devices and b in devices
  used.add(tuple(sorted((a,b))));union(a,b);styles[row['pulse_style']]+=1
  actor=by[row['strips'][0]];assert actor.actor_has_tag('SecurityCommunicationVFX')
  templates[row['pulse_style']]=actor.get_component_by_class(u.SplineMeshComponent).get_material(0)
 candidates=[];ids=sorted(devices)
 for index,a in enumerate(ids):
  for b in ids[index+1:]:
   dist=(devices[b]['center']-devices[a]['center']).length()
   if (a,b) not in used and dist>10:candidates.append((dist,a,b))
 candidates.sort();new=[]
 for dist,a,b in candidates:
  if find(a)!=find(b):new.append((a,b));used.add((a,b));union(a,b)
 for dist,a,b in candidates:
  if len(rows)+len(new)>=88:break
  if (a,b) not in used:new.append((a,b));used.add((a,b))
 assert len(new)==78 and len({find(i) for i in ids})==1
 backup=OUT/('Links88Backup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir()
 shutil.copy2(ROOT/'Content/MatrixDeviceLinks/L_MatrixDeviceLinks.umap',backup/'L_MatrixDeviceLinks.umap');shutil.copy2(OUT/'communication_demo.json',backup/'communication_demo.json')
 assert levels.set_current_level_by_name('L_MatrixDeviceLinks')
 mesh=u.load_asset('/Game/MatrixDeviceLinks/SM_ArcRibbon');assert mesh
 cap=data['arc_roof_z']-30
 for a,b in new:
  start,end=devices[a]['center'],devices[b]['center'];d=end-start
  style=min(templates,key=lambda k:(styles[k],k));styles[style]+=1
  lo,hi=0.,max(0.,cap-min(start.z,end.z))
  for _ in range(40):
   h=(lo+hi)*.5;t=max(0.,min(1.,.5+d.z/(8*h))) if h else 0
   if start.z+d.z*t+4*h*t*(1-t)>cap:hi=h
   else:lo=h
  h=min(lo,max(20.,d.length()*.12));width=2.5 if int(style[1:])<=5 else 5.
  actor=actors.spawn_actor_from_class(u.SplineMeshActor,start);assert actor
  c=actor.get_component_by_class(u.SplineMeshComponent);c.set_static_mesh(mesh);c.set_material(0,templates[style]);c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);c.set_editor_property('cast_shadow',False)
  c.set_forward_axis(u.SplineMeshAxis.X,False);c.set_spline_up_dir(u.Vector(0,0,1),False);c.set_start_scale(u.Vector2D(width/100,width/100),False);c.set_end_scale(u.Vector2D(width/100,width/100),False)
  c.set_start_and_end(u.Vector(0,0,0),d+u.Vector(0,0,4*h),d,d-u.Vector(0,0,4*h),True)
  n=len(rows)+1;actor.set_actor_label('COMM_%02d_%s_normal_ARC'%(n,style));actor.set_folder_path('VFX/MatrixDeviceLinks/Communication')
  actor.tags=['MatrixDeviceLink','CommunicationDemo','SecurityCommunicationVFX','SimulatedState=normal','SourceId='+a,'TargetId='+b];actor.set_actor_hidden_in_game(True)
  rows.append(dict(line=n,source=devices[a]['record'],target=devices[b]['record'],start=start.to_tuple(),end=end.to_tuple(),pulse_style=style,width_cm=width,simulated_state='normal',alert_style=None,strips=[actor.get_path_name()],arc_height_cm=h,midpoint_z=(start.z+end.z)/2+h))
 live=[a for a in actors.get_all_level_actors() if a.actor_has_tag('MatrixDeviceLink')]
 assert len(live)==88
 pairs=set()
 for a in live:
  tags=[str(t) for t in a.tags];source=next(t.split('=',1)[1] for t in tags if t.startswith('SourceId='));target=next(t.split('=',1)[1] for t in tags if t.startswith('TargetId='))
  assert source in members and target in members and source!=target
  key=tuple(sorted((source,target)));assert key not in pairs;pairs.add(key)
  assert a.actor_has_tag('SecurityCommunicationVFX') and a.get_editor_property('hidden')
 assert levels.save_current_level()
 data.update(independent_lines=88,unique_devices=len(devices),normal_lines=85)
 (OUT/'communication_demo.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
 report=dict(ok=True,total_lines=88,added_lines=78,p01_p10_counts=dict(sorted(styles.items())),connected_instances=len(devices),unresolved_instance_ids=sorted(set(members)-set(devices)),unique_pairs=len(pairs),all_endpoints_in_security=True,default_hidden=True,backup=str(backup))
 (OUT/'links88-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print('LINKS88_SUCCESS '+json.dumps(report))
try:run()
except:
 (OUT/'links88-error.txt').write_text(traceback.format_exc(),encoding='utf-8');raise
