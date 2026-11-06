import unreal as u,json
from pathlib import Path
out=Path('D:/0bay/OfficeShell/Saved/CloudWorkspace/calibration');out.mkdir(exist_ok=True)
assert u.get_editor_subsystem(u.LevelEditorSubsystem).load_level('/Game/OfficeShell/Maps/Office_Demo_ArtMerged')
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
names=['工区AI助手','无线认证门户','研发资料库','视觉识别服务','访客预约服务','调度网关','运维中继']
def vec(v):return [v.x,v.y,v.z]
def descendants(a):return [a]+[x for c in a.get_attached_actors() for x in descendants(c)]
records=[]
for name in names:
    roots=[a for a in actors if a.get_actor_label()==name];assert len(roots)==1,(name,len(roots))
    root=roots[0];meshes=[]
    for a in descendants(root):
        for c in a.get_components_by_class(u.StaticMeshComponent):
            if not c.static_mesh:continue
            center,extent,_=u.SystemLibrary.get_component_bounds(c)
            lo,hi=c.get_local_bounds();t=c.get_world_transform()
            corners=[u.MathLibrary.transform_location(t,u.Vector(x,y,z)) for x in [lo.x,hi.x] for y in [lo.y,hi.y] for z in [lo.z,hi.z]]
            meshes.append(dict(actor=a.get_path_name(),label=a.get_actor_label(),component=c.get_name(),asset=c.static_mesh.get_path_name(),
                visible=c.is_visible(),hidden=c.get_editor_property('hidden_in_game'),location=vec(c.get_world_location()),
                bounds_center=vec(center),bounds_extent=vec(extent),local_min=vec(lo),local_max=vec(hi),
                geometric_min=[min(getattr(v,k) for v in corners) for k in ['x','y','z']],
                geometric_max=[max(getattr(v,k) for v in corners) for k in ['x','y','z']]))
    records.append(dict(name=name,root=root.get_path_name(),root_location=vec(root.get_actor_location()),tags=[str(t) for t in root.tags],meshes=meshes))
(out/'mesh-bounds.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf8')
print('PANEL_BOUNDS_EXPORTED',len(records))
