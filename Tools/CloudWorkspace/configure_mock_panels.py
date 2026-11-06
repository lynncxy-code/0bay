"""Configure seven authored mock panels through the revision-checked overlay API."""
import copy, json, sys, urllib.request, urllib.error
from pathlib import Path
from datetime import datetime
sys.stdout.reconfigure(encoding='utf-8')
OUT=Path('D:/0bay/OfficeShell/Saved/CloudWorkspace')/('mock-panels-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
OUT.mkdir(parents=True)
PROJECT='ds_1788856616035'
RID='cloud.workspace'
op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def api(path,payload=None,method=None):
    req=urllib.request.Request('http://localhost:5000'+path,data=None if payload is None else json.dumps(payload).encode(),method=method,
        headers={'Content-Type':'application/json','X-OntoTwin-UE-Project-Id':'ueproj_0bay_office_opensteel_7e0ae9f7'})
    try:
        with op.open(req,timeout=30) as response:return json.load(response)
    except urllib.error.HTTPError as e:raise RuntimeError(e.read().decode()) from e
def save(name,data):(OUT/name).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
def literal(value):return dict(source='literal',value=value)
DATA={
 'ai-assistant':('服务就绪',[('requests','今日问答',1286,'次',0),('latency','平均响应',1.2,'秒',1),('resolution','问题解决率',96.8,'%',1)]),
 'wifi-portal':('认证正常',[('clients','在线终端',86,'台',0),('success','认证成功率',99.7,'%',1),('latency','认证耗时',180,'ms',0)]),
 'research-library':('索引正常',[('documents','收录资料',12480,'份',0),('queries','今日检索',326,'次',0),('latency','检索耗时',240,'ms',0)]),
 'vision-service':('识别中',[('streams','接入视频',12,'路',0),('inference','推理耗时',38,'ms',0),('accuracy','识别准确率',98.6,'%',1)]),
 'visitor-service':('预约开放',[('appointments','今日预约',24,'人',0),('arrived','已到访',18,'人',0),('pending','待审核',3,'单',0)]),
 'dispatch-gateway':('调度正常',[('tasks','今日任务',368,'个',0),('queue','排队任务',4,'个',0),('success','执行成功率',99.2,'%',1)]),
 'operations-relay':('链路正常',[('nodes','接入节点',32,'个',0),('latency','转发延迟',24,'ms',0),('delivery','消息送达率',99.98,'%',2)]),
}
runtime=api('/api/v2/web-interactions/runtime');assert runtime['project_id']==PROJECT
types_before=api('/api/v2/ontology/types')
contexts={key:api('/api/v2/overlays/context?instance_id=cloud.workspace.'+key) for key in DATA}
save('before.json',dict(contexts=contexts,types=types_before,web=runtime))
base=copy.deepcopy(contexts['ai-assistant']['type_config']['values'])
base.update(template_id='title_status_metrics',enabled=True,display_mode='always')
title=dict(required=True,binding=dict(source='instance',path='display_name'),format=dict(empty_text='--',max_length=80))
def status(label):
    return dict(required=True,label_binding=literal(label+' · 固定模拟数据'),level_binding=literal('normal'),
        appearance=dict(normal=dict(label=label+'（模拟）',color='cyan'),
            offline=dict(label='模拟演示',color='cyan')))
base['slots']=dict(title=title,status=status('服务正常'),metrics=[])
prepared={}
for key,(label,metrics) in DATA.items():
    values=copy.deepcopy(contexts[key]['instance_override']['values'])
    values.update(template_id='title_status_metrics',enabled=True,
        slots=dict(title=copy.deepcopy(title),status=status(label),metrics=[dict(id=mid,label=mlabel,required=True,emphasized=index==0,
            binding=literal(value),format=dict(precision=precision,unit=unit,empty_text='--'))
            for index,(mid,mlabel,value,unit,precision) in enumerate(metrics)]))
    effective=copy.deepcopy(base);effective.update(values)
    api('/api/v2/overlays/preview',dict(object_type_rid=RID,instance_id='cloud.workspace.'+key,config=effective),'POST')
    prepared[key]=values
api('/api/v2/overlays/object-types/'+RID,dict(config=base,expected_revision=contexts['ai-assistant']['type_config']['revision'],expected_project_id=PROJECT),'PUT')
for key,values in prepared.items():
    api('/api/v2/overlays/instances/cloud.workspace.'+key,dict(override=values,expected_revision=contexts[key]['instance_override']['revision'],expected_project_id=PROJECT),'PUT')
snapshots=api('/api/v2/state/snapshots')
selected=[s for s in snapshots if s['instanceId'] in {'cloud.workspace.'+key for key in DATA}]
assert len(selected)==7
for s in selected:
    o=s['interfaces']['I3D_Overlay'];slots=o['resolved_slots']
    assert o['template_id']=='title_status_metrics' and o['display_mode']=='always' and o['enabled']
    assert slots['title']['display_value']==s['displayName']
    assert len(slots['metrics'])==3 and all(m['display_value']!='--' for m in slots['metrics'])
    assert '模拟' in slots['status']['detail_value']
web_after=api('/api/v2/web-interactions/runtime')
assert web_after['config']==runtime['config'] and web_after['revision']==runtime['revision']
types_after={t['rid']:t for t in api('/api/v2/ontology/types')}
assert all(t==types_after[t['rid']] for t in types_before if t['rid']!=RID)
save('snapshots-after.json',selected)
save('verification.json',dict(ok=True,panels=7,metrics_per_panel=3,template='title_status_metrics',business_config_unchanged=True,unrelated_types_unchanged=True))
print('Saved and verified 7 mock panels:',OUT)
for s in selected:
    slots=s['interfaces']['I3D_Overlay']['resolved_slots']
    print(s['displayName'],slots['status']['display_value'],[(m['label'],m['display_value']) for m in slots['metrics']])
