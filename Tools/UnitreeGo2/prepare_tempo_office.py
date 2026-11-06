import unreal as u,json,shutil,datetime,hashlib,math
from pathlib import Path
P=Path(u.Paths.project_dir()).resolve();OUT=P/'Saved/Go2'
MAIN='/Game/OfficeShell/Maps/Office_Demo_ArtMerged'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem);actors=u.get_editor_subsystem(u.EditorActorSubsystem);editor=u.get_editor_subsystem(u.UnrealEditorSubsystem)
assert levels.load_level(MAIN);world=editor.get_editor_world()
allactors=actors.get_all_level_actors()
mesh_editor=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
seen=set()
for a in allactors:
    for c in a.get_components_by_class(u.StaticMeshComponent):
        m=c.get_editor_property('static_mesh')
        if mesh_editor and m and m.get_path_name() not in seen:
            seen.add(m.get_path_name());mesh_editor.get_number_verts(m,0)
backup=OUT/('TempoOfficeBackup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir(parents=True)
packages=sorted(set([MAIN]+[a.get_outer().get_path_name().split('.')[0] for a in allactors]))
files=[]
for package in packages:
    if not package.startswith('/Game/'):continue
    source=P/'Content'/(package[6:]+'.umap')
    if source.exists():
        target=backup/source.relative_to(P);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
        files.append({'file':str(source),'backup':str(target),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
inventory=[{'path':a.get_path_name(),'label':a.get_actor_label(),'class':a.get_class().get_name(),'transform':a.get_actor_transform().export_text(),'location':a.get_actor_location().to_tuple()} for a in allactors]
(OUT/'tempo_office_before.json').write_text(json.dumps({'backup':str(backup),'files':files,'actors':inventory},ensure_ascii=False,indent=2),encoding='utf-8')
course=actors.spawn_actor_from_class(u.load_class(None,'/Script/OfficeHost.TempoGo2Course'),u.Vector(0,0,4320));course.set_editor_property('auto_start',False)
route=course.get_editor_property('route');length=route.get_spline_length()
samples=[route.get_location_at_distance_along_spline(length*i/60,u.SplineCoordinateSpace.LOCAL) for i in range(61)]
dogs=[a for a in allactors if 'Go2' in a.get_class().get_name()]+[course]
best=[];attempts=0
# Prefer the circulation area close to the previous dog; keep the original curve
# when possible, otherwise compress only the route width (never robot scale).
candidates=sorted([(x,y,yaw,sy) for x in range(1800,6101,300) for y in [-1750,-1450,-1150,-850,-550] for yaw in [0,90,180,270] for sy in [1,.65,.4]],key=lambda c:math.hypot(c[0]-2600,c[1]+1300)+(1-c[3])*1500)
for x,y,yaw,sy in candidates:
    attempts+=1;r=math.radians(yaw);c=math.cos(r);s=math.sin(r)
    pts=[u.Vector(x+c*p.x-s*p.y*sy,y+s*p.x+c*p.y*sy,4320) for p in samples]
    if any(p.x<100 or p.x>6450 or p.y<-1990 or p.y>-100 for p in pts):continue
    valid=True
    for i in range(0,len(pts)-1,2):
        p=pts[i];q=pts[min(i+2,len(pts)-1)]
        hits=u.SystemLibrary.box_trace_multi(world,p+u.Vector(0,0,42),q+u.Vector(0,0,42),u.Vector(50,35,35),u.MathLibrary.find_look_at_rotation(p,q),u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,dogs,u.DrawDebugTrace.NONE,True)
        if hits:valid=False;break
        hit=u.SystemLibrary.line_trace_single(world,p+u.Vector(0,0,15),p-u.Vector(0,0,20),u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,dogs,u.DrawDebugTrace.NONE,True)
        if not hit or abs(hit.to_tuple()[5].z-4320)>2:valid=False;break
    if valid:
        best.append({'translation':[x,y,4320],'yaw':yaw,'route_width_scale':sy,'score':math.hypot(x-2600,y+1300)+(1-sy)*1500})
        if len(best)>=4:break
result={'attempts':attempts,'candidates':best,'route_points_local':[p.to_tuple() for p in samples],'backup':str(backup)}
(OUT/'tempo_office_placement.json').write_text(json.dumps(result,indent=2))
assert best,'No clear candidate course found'
print('TEMPO_OFFICE_PLACEMENT',best[0])
