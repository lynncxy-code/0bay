import unreal as u,json
from pathlib import Path
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);assert ls.load_level('/Game/MatrixDeviceLinks/L_MatrixDeviceLinks')
aa=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors();links=[a for a in aa if a.actor_has_tag('MatrixDeviceLink')]
assert len(links)==88
for a in links:
 c=a.get_component_by_class(u.SplineMeshComponent);assert c and c.get_material(0) and c.get_editor_property('static_mesh')
 assert a.actor_has_tag('SecurityCommunicationVFX') and a.get_editor_property('hidden')
p=Path('D:/0bay/OfficeShell/Saved/MatrixLinks/links88-verification.json');r=json.loads(p.read_text(encoding='utf-8'));r['saved_map_reopened_verified']=True;r['saved_spline_count']=len(links);p.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
