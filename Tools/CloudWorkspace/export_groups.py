import unreal as u, json
from pathlib import Path
out=Path('D:/0bay/OfficeShell/Saved/CloudWorkspace')
names={'工区AI助手':'ai-assistant','无线认证门户':'wifi-portal','研发资料库':'research-library','视觉识别服务':'vision-service','访客预约服务':'visitor-service','调度网关':'dispatch-gateway','运维中继':'operations-relay','外部共享端':'external-sharing'}
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
def descendants(a):
    return [a]+[x for c in a.get_attached_actors() for x in descendants(c)]
records=[]
for label,key in names.items():
    roots=[a for a in actors if a.get_actor_label()==label]
    assert len(roots)==1,label
    root=roots[0];group=descendants(root);mins=[];maxs=[]
    for a in group:
        for c in a.get_components_by_class(u.MeshComponent):
            if isinstance(c,(u.StaticMeshComponent,u.SkeletalMeshComponent)):
                center,extent,_=u.SystemLibrary.get_component_bounds(c)
                mins.append(center-extent);maxs.append(center+extent)
    assert mins,label
    lo=u.Vector(min(v.x for v in mins),min(v.y for v in mins),min(v.z for v in mins))
    hi=u.Vector(max(v.x for v in maxs),max(v.y for v in maxs),max(v.z for v in maxs))
    records.append(dict(instance_id='cloud.workspace.'+key,display_name=label,root=root.get_path_name(),
        position=dict(x=(lo.x+hi.x)/2,y=(lo.y+hi.y)/2,z=hi.z),
        actors=[dict(path=a.get_path_name(),label=a.get_actor_label(),tags=[str(t) for t in a.tags],
            hidden=a.get_editor_property('hidden'),transform=a.get_actor_transform().export_text()) for a in group]))
(out/'groups.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf8')
print('CLOUD_GROUPS_EXPORTED',len(records),sum(len(r['actors']) for r in records))
