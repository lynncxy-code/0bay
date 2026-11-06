import unreal as u, json
from pathlib import Path
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
names=['工区AI助手','无线认证门户','研发资料库','视觉识别服务','访客预约服务','调度网关','运维中继']
data=[]
for a in actors:
 if a.get_actor_label() in names or isinstance(a,u.TwinSceneManager):
  item=dict(label=a.get_actor_label(),name=a.get_name(),path=a.get_path_name(),cls=a.get_class().get_name(),tags=[str(t) for t in a.tags],transform=a.get_actor_transform().export_text())
  if isinstance(a,u.TwinSceneManager):item.update(backend=a.get_editor_property('backend_base_url'),project=a.get_editor_property('ue_project_id'))
  data.append(item)
Path('D:/0bay/OfficeShell/Saved/cloud-probe.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
