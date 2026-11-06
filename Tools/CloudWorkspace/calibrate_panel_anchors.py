"""Reposition only overlay proxies to the saved authored mesh bounds, in UE cm."""
import json,sys,urllib.request,urllib.error,math
from pathlib import Path
from datetime import datetime
sys.stdout.reconfigure(encoding='utf8')
root=Path('D:/0bay/OfficeShell/Saved/CloudWorkspace/calibration')
out=root/datetime.now().strftime('%Y%m%d-%H%M%S');out.mkdir()
records=json.loads((root/'mesh-bounds.json').read_text(encoding='utf8'))
op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def api(path,data=None):
    req=urllib.request.Request('http://localhost:5000'+path,data=None if data is None else json.dumps(data).encode(),headers={
        'Content-Type':'application/json','X-OntoTwin-UE-Project-Id':'ueproj_0bay_office_opensteel_7e0ae9f7'})
    try:
        with op.open(req,timeout=30) as r:return json.load(r)
    except urllib.error.HTTPError as e:raise RuntimeError(e.read().decode()) from e
def save(name,data):(out/name).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
web=api('/api/v2/web-interactions/runtime');assert web['project_id']=='ds_1788856616035'
before=api('/api/v2/state/snapshots');byid={s['instanceId']:s for s in before}
fields=dict(tx='translation_x',ty='translation_y',tz='translation_z',rx='rotation_x',ry='rotation_y',rz='rotation_z',sx='scale_x',sy='scale_y',sz='scale_z')
changes=[];report=[];configs={}
for r in records:
    ids=[t.split('=',1)[1] for t in r['tags'] if t.startswith('OntoTwinInstanceId=')];assert len(ids)==1
    iid=ids[0];s=byid[iid];assert s['objectTypeRid']=='cloud.workspace'
    meshes=[m for m in r['meshes'] if m['visible'] and not m['hidden']];assert meshes
    lo=[min(m['geometric_min'][i] for m in meshes) for i in range(3)]
    hi=[max(m['geometric_max'][i] for m in meshes) for i in range(3)]
    desired=[(lo[0]+hi[0])/2,(lo[1]+hi[1])/2,hi[2]]
    spatial=s['interfaces']['I3D_Spatial'];transform={k:spatial[v] for k,v in fields.items()}
    previous=[transform[k] for k in ['tx','ty','tz']]
    distance=math.dist(previous,desired)
    ctx=api('/api/v2/overlays/context?instance_id='+iid);configs[iid]=ctx['effective_config']
    assert ctx['effective_config']['anchor']=={'strategy':'bounds_top','offset_cm':{'x':0,'y':0,'z':30}}
    if distance>0.01:
        transform.update(zip(['tx','ty','tz'],desired))
        changes.append(dict(instance_id=iid,expected_state_hash=s['runtimeEditor']['stateHash'],transform=transform))
    report.append(dict(instance_id=iid,name=r['name'],before=previous,after=desired,shift_cm=round(distance,2),
        panel_anchor=[desired[0],desired[1],desired[2]+30],root_location=r['root_location']))
save('before.json',dict(snapshots=[byid[r['instance_id']] for r in report],overlay_configs=configs,web_revision=web['revision']))
save('plan.json',report)
if changes:
    result=api('/api/v2/state/writeback/batch',dict(changes=changes));assert result['status']=='ok',result
    save('writeback-result.json',result)
after={s['instanceId']:s for s in api('/api/v2/state/snapshots')}
for r in report:
    s=after[r['instance_id']]['interfaces']['I3D_Spatial']
    actual=[s[k] for k in ['translation_x','translation_y','translation_z']]
    assert math.dist(actual,r['after'])<0.01
    assert api('/api/v2/overlays/context?instance_id='+r['instance_id'])['effective_config']==configs[r['instance_id']]
web_after=api('/api/v2/web-interactions/runtime');assert web_after['config']==web['config'] and web_after['revision']==web['revision']
targets={r['instance_id'] for r in report}
assert all(s['interfaces'].get('I3D_Spatial')==after[s['instanceId']]['interfaces'].get('I3D_Spatial') for s in before if s['instanceId'] not in targets)
save('verification.json',dict(ok=True,checked=len(report),adjusted=len(changes),clearance_cm=30,models_untouched=True,overlay_content_unchanged=True,business_config_unchanged=True,report=report))
print('Verified calibration',out)
for r in report:print(r['name'],'shift',r['shift_cm'],'cm','anchor',[round(v,2) for v in r['panel_anchor']])
