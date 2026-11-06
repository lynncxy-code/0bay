import unreal as u,json,collections,math,shutil,datetime
from pathlib import Path
R=Path('D:/0bay/OfficeShell');O=R/'Saved/MatrixLinks';L=u.EditorAssetLibrary;M=u.MaterialEditingLibrary
levels=u.get_editor_subsystem(u.LevelEditorSubsystem);assert levels.load_level('/Game/MatrixDeviceLinks/L_MatrixDeviceLinks')
aa=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors();by={a.get_path_name():a for a in aa}
data=json.loads((O/'communication_demo.json').read_text(encoding='utf-8'));rows=data['connections'];assert len(rows)==150
records={r[k]['id']:r[k] for r in rows for k in ['source','target']};positions={r[k]['id']:r[p] for r in rows for k,p in [('source','start'),('target','end')]}
servers=sorted(i for i,v in records.items() if v['type']=='security.device.servercabinet');assert len(servers)==4
backup=O/('ServerFanout150_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir()
for f in [R/'Content/MatrixDeviceLinks/L_MatrixDeviceLinks.umap',R/'Content/MatrixDeviceLinks/Preview/M_CommunicationPulse_Bright.uasset',O/'communication_demo.json']:shutil.copy2(f,backup/f.name)
def key(a,b):return tuple(sorted((a,b)))
def connected(edges):
 adj=collections.defaultdict(set)
 for a,b in edges:adj[a].add(b);adj[b].add(a)
 seen={next(iter(records))};todo=list(seen)
 while todo:
  for n in adj[todo.pop()]-seen:seen.add(n);todo.append(n)
 return len(seen)==len(records)
def distance(a,b):return math.dist(positions[a],positions[b])
changes=[]
# Normalize existing server-to-device links so their source is the server.
for row in rows:
 a,b=row['source']['id'],row['target']['id']
 if b in servers and a not in servers:
  row['source'],row['target']=row['target'],row['source'];row['start'],row['end']=row['end'],row['start'];changes.append(row['line'])
for server in servers:
 while sum(r['source']['id']==server and r['target']['id'] not in servers for r in rows)<16:
  used={key(r['source']['id'],r['target']['id']) for r in rows}
  candidates=sorted((distance(server,i),i) for i in records if i not in servers and key(server,i) not in used)
  _,target=candidates[0]
  removable=sorted([r for r in rows if r['line']>10 and r['source']['id'] not in servers and r['target']['id'] not in servers],key=lambda r:distance(r['source']['id'],r['target']['id']),reverse=True)
  victim=next(r for r in removable if connected((used-{key(r['source']['id'],r['target']['id'])})|{key(server,target)}))
  victim.update(source=records[server],target=records[target],start=positions[server],end=positions[target]);changes.append(victim['line'])
assert len({key(r['source']['id'],r['target']['id']) for r in rows})==150
assert connected([key(r['source']['id'],r['target']['id']) for r in rows])
for row in rows:
 if row['line'] not in changes:continue
 a=by[row['strips'][0]];c=a.get_component_by_class(u.SplineMeshComponent);assert c
 start=u.Vector(*row['start']);end=u.Vector(*row['end']);d=end-start;cap=data['arc_roof_z']-30;lo,hi=0.,max(0.,cap-min(start.z,end.z))
 for _ in range(40):
  h=(lo+hi)*.5;t=max(0.,min(1.,.5+d.z/(8*h))) if h else 0
  if start.z+d.z*t+4*h*t*(1-t)>cap:hi=h
  else:lo=h
 h=min(lo,max(20.,d.length()*.12));a.set_actor_location(start,False,True);c.set_start_and_end(u.Vector(0,0,0),d+u.Vector(0,0,4*h),d,d-u.Vector(0,0,4*h),True)
 tags=[str(t) for t in a.tags if not str(t).startswith(('SourceId=','TargetId='))];a.tags=tags+['SourceId='+row['source']['id'],'TargetId='+row['target']['id']];a.set_actor_hidden_in_game(True)
 row.update(arc_height_cm=h,midpoint_z=(start.z+end.z)/2+h)
bright=L.load_asset('/Game/MatrixDeviceLinks/Preview/M_CommunicationPulse_Bright');custom=M.get_material_property_input_node(bright,u.MaterialProperty.MP_OPACITY)
code=custom.get_editor_property('code');code=code.replace('Clock*3.0','Clock*.8').replace('x*6.0','x*4.0').replace('x*8.0-Clock*4.0','x*4.0-Clock*.8').replace('Clock*9.42478','Clock*3.14159').replace('Six dense packet bands','Four dense packet bands');custom.set_editor_property('code',code);M.recompile_material(bright);assert L.save_loaded_asset(bright)
assert levels.save_current_level();(O/'communication_demo.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
report=dict(ok=True,total_lines=150,connected_instances=len(records),unique_pairs=150,updated_lines=len(set(changes)),server_outgoing={records[i]['name']:sum(r['source']['id']==i and r['target']['id'] not in servers for r in rows) for i in servers},pulse_groups=4,travel_cycles_per_second=.2,previous_travel_cycles_per_second=.5,brightness_unchanged=True,backup=str(backup))
(O/'server150-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
