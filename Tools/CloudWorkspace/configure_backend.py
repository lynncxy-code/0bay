import json, urllib.request, urllib.error, copy, sys
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
OUT=Path('D:/0bay/OfficeShell/Saved/CloudWorkspace')
PROJECT='ds_1788856616035'
RID='cloud.workspace'
op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def api(path,payload=None,method=None):
    req=urllib.request.Request('http://localhost:5000'+path,data=None if payload is None else json.dumps(payload).encode(),method=method,
        headers={'Content-Type':'application/json','X-OntoTwin-UE-Project-Id':'ueproj_0bay_office_opensteel_7e0ae9f7'})
    try:
        with op.open(req,timeout=40) as response:return json.load(response)
    except urllib.error.HTTPError as e:raise RuntimeError(e.read().decode()) from e
def save(name,data): (OUT/name).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
groups=json.loads((OUT/'groups.json').read_text(encoding='utf8'))
runtime=api('/api/v2/web-interactions/runtime');assert runtime['project_id']==PROJECT
types=api('/api/v2/ontology/types');instances=api('/api/v2/instances')
if not (OUT/'backend-before.json').exists():save('backend-before.json',dict(types=types,instances=instances,web=api('/api/v2/web-interactions')))
if RID not in {t['rid'] for t in types}:
    r=api('/api/v2/coord/types/commit',dict(mode='merge',merge_options=dict(target_dataset_id=PROJECT),conflict_strategy='skip',items=[dict(rid=RID,name='云上工作区',category='云上工作区',description='云上工作区服务与其场景组合对象；仅安全业务显示。',source='ue-authored-cloud-workspace',injected_interfaces=['I3D_Representable','I3D_Spatial','I3D_Overlay'])]),'POST')
    assert r['status']=='ok',r
known={i['id']:i for i in instances}
for g in groups:
    if g['instance_id'] not in known:
        r=api('/api/v2/instances',dict(instance_id=g['instance_id'],object_type_rid=RID,display_name=g['display_name'],initial_position=g['position'],hierarchy_path='云上工作区',expected_project_id=PROJECT),'POST')
        assert r['status']=='spawned',r
    else:assert known[g['instance_id']]['object_type_rid']==RID
t=api('/api/v2/ontology/types/'+RID)
revision=t.get('interface_configs',{}).get('I3D_Overlay',{}).get('revision',0)
config=dict(enabled=True,template_id='title_body',display_mode='always',anchor=dict(strategy='bounds_top',offset_cm=dict(x=0,y=0,z=30)),presentation=dict(quality_tier='balanced'),slots=dict(title=dict(required=True,binding=dict(source='instance',path='display_name'),format=dict(empty_text='--',max_length=80))))
r=api('/api/v2/overlays/object-types/'+RID,dict(config=config,expected_revision=revision,expected_project_id=PROJECT),'PUT');assert r['status']=='ok',r
runtime=api('/api/v2/web-interactions/runtime');config=copy.deepcopy(runtime['config'])
security=next(b for b in config['business_views'] if b['business_view_id']=='bv.network-security')
if not any(RID in g.get('object_type_rids',[]) for g in security['rule_groups']):
    security['rule_groups'].append(dict(object_type_rids=[RID],instance_ids=[],zone_ids=[]))
    validation=api('/api/v2/web-interactions/validate',dict(config=config),'POST');save('web-validation.json',validation);assert validation['valid'],validation
    r=api('/api/v2/web-interactions/apply',dict(config=config,expected_revision=runtime['revision'],confirm_warnings=True),'POST');assert r['applied'],r
snapshots=api('/api/v2/state/snapshots');selected=[s for s in snapshots if s['instanceId'] in {g['instance_id'] for g in groups}]
assert len(selected)==7
save('snapshots-after.json',selected)
current=api('/api/v2/web-interactions/runtime');assert all(g['instance_id'] in current['business_view_memberships']['bv.network-security'] for g in groups)
save('backend-verification.json',dict(type=api('/api/v2/ontology/types/'+RID),instance_count=len(selected),web_revision=current['revision'],security_membership_verified=True))
print('Configured 云上工作区: 7 instances, Always Display / display_name, security membership verified')
