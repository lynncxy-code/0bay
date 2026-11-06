import unreal as u,json,collections
from pathlib import Path
root=Path('D:/0bay/OfficeShell');out=root/'Saved/MatrixLinks'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
data=json.loads((out/'communication_demo.json').read_text(encoding='utf-8'))
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
by={a.get_path_name():a for a in actors}
cloud={'ai-assistant':{'wireless','agent'},'wifi-portal':{'wireless'},'research-library':{'wireless','agent'},'external-sharing':{'wireless','agent'},'dispatch-gateway':{'industrial'},'operations-relay':{'industrial'},'vision-service':{'embodied'},'visitor-service':{'embodied'}}
def categories(record):
    t=record['type']
    if t=='cloud.workspace':return cloud[record['id'].split('.')[-1]]
    if t=='security.device.servercabinet':return set()
    if t in ['security.device.hvac','security.device.arm','security.device.printer3d']:return {'industrial'}
    if t in ['camera','security.device.humanoid','security.device.display','security.device.monitor','security.device.videowall','security.device.interactive']:return {'embodied'}
    if t=='security.device.printer':return {'agent'}
    return {'wireless'}
pairs={}
counts=collections.Counter()
for row in data['connections']:
    tasks=categories(row['source'])|categories(row['target'])
    if not tasks:tasks={'wireless','industrial','embodied','agent'}
    row['task_chains']=sorted(tasks)
    pairs[tuple(sorted([row['source']['id'],row['target']['id']]))]=tasks
    counts.update(tasks)
tagged=0
for a in actors:
    if not a.actor_has_tag('CommunicationDemo'):continue
    tags=[str(t) for t in a.tags]
    ids=[t.split('=',1)[1] for t in tags if t.startswith(('SourceId=','TargetId='))]
    if len(ids)!=2:continue
    tasks=pairs.get(tuple(sorted(ids)))
    if not tasks:continue
    a.modify()
    a.tags=[t for t in tags if not t.startswith('CommunicationTask=')]+['CommunicationTask='+task for task in sorted(tasks)]
    tagged+=1
world=by[data['connections'][0]['strips'][0]].get_level().get_outer()
assert u.EditorAssetLibrary.save_loaded_asset(world,False)
(out/'communication_demo.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
(out/'task_chain_assignments.json').write_text(json.dumps({'lines':len(data['connections']),'counts':dict(counts),'tagged_actors':tagged},indent=2))
