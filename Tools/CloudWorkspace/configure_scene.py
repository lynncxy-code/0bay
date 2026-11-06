import unreal as u,json,shutil
from pathlib import Path
root=Path('D:/0bay/OfficeShell');out=root/'Saved/CloudWorkspace'
groups=json.loads((out/'groups.json').read_text(encoding='utf8'))
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
by_path={a.get_path_name():a for a in actors}
manager=next(a for a in actors if isinstance(a,u.TwinSceneManager))
before_ids=list(manager.get_editor_property('level_geometry_dynamic_instance_ids'))
packages={manager.get_path_name().split('.')[0]}|{g['root'].split('.')[0] for g in groups}
for package in packages:
    file=root/'Content'/(package.removeprefix('/Game/')+'.umap')
    backup=out/(file.stem+'.before.umap')
    if not backup.exists():shutil.copy2(file,backup)
if not (out/'scene-before.json').exists():
    (out/'scene-before.json').write_text(json.dumps(dict(dynamic_ids=before_ids,groups=groups),ensure_ascii=False,indent=2),encoding='utf8')
for g in groups:
    for record in g['actors']:
        a=by_path[record['path']]
        assert a.get_actor_transform().export_text()==record['transform'],record['label']
        a.modify()
        tags=[str(t) for t in a.tags]
        tags+=['OntoTwinInstanceId='+g['instance_id'],'OntoTwinObjectType=cloud.workspace','OntoTwinBusiness=security','OntoTwinOverlaySource','OntoTwinSecurityOnly']
        a.tags=list(dict.fromkeys(tags))
        a.set_actor_hidden_in_game(True)
manager.modify()
manager.set_editor_property('level_geometry_dynamic_instance_ids',list(dict.fromkeys(before_ids+[g['instance_id'] for g in groups])))
for package in packages:
    a=next(a for a in actors if a.get_path_name().split('.')[0]==package)
    assert u.EditorLoadingAndSavingUtils.save_map(a.get_level().get_outer(),package)
for g in groups:
    for record in g['actors']:
        a=by_path[record['path']]
        assert a.get_actor_transform().export_text()==record['transform']
        assert a.get_editor_property('hidden')
        assert 'OntoTwinInstanceId='+g['instance_id'] in [str(t) for t in a.tags]
report=dict(instance_count=len(groups),actor_count=sum(len(g['actors']) for g in groups),transforms_unchanged=True,default_hidden=True,packages=sorted(packages),dynamic_ids=list(manager.get_editor_property('level_geometry_dynamic_instance_ids')))
(out/'scene-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('CLOUD_SCENE_SAVED',report)
