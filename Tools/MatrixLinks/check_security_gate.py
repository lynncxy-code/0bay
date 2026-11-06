import unreal as u,json,time
from pathlib import Path
check_out=Path('D:/0bay/OfficeShell/Saved/MatrixLinks/security_gate_check.json');gate_started=time.monotonic();gate_stage=0;gate_report={}
u.EditorLevelLibrary.editor_play_simulate()
def gate_check_tick(dt):
 global gate_stage,gate_started,gate_check_handle
 if time.monotonic()-gate_started<3:return
 try:
  world=u.EditorLevelLibrary.get_game_world()
  if not world:return
  items=u.GameplayStatics.get_all_actors_with_tag(world,'SecurityCommunicationVFX')
  comps=[c for a in u.GameplayStatics.get_all_actors_of_class(world,u.Actor) for c in a.get_components_by_class(u.OntoTwinWebInteractionComponent)]
  if not comps:raise RuntimeError('No business component')
  comp=comps[0]
  if gate_stage==0:
   gate_report['startup']={'actors':len(items),'hidden':sum(a.get_editor_property('hidden') for a in items)}
   assert len(items)==19 and all(a.get_editor_property('hidden') for a in items)
   gate_report['opened']=comp.open_business_view('bv.network-security','');assert gate_report['opened']
   gate_stage=1;gate_started=time.monotonic();return
  if gate_stage==1:
   gate_report['security']={'visible':sum(not a.get_editor_property('hidden') for a in items),'active_symbols':sum(c.is_active() for a in items for c in a.get_components_by_class(u.NiagaraComponent))}
   assert all(not a.get_editor_property('hidden') for a in items)
   comp.back();gate_stage=2;gate_started=time.monotonic();return
  gate_report['exit']={'hidden':sum(a.get_editor_property('hidden') for a in items)};assert all(a.get_editor_property('hidden') for a in items)
  gate_report['ok']=True;check_out.write_text(json.dumps(gate_report),encoding='utf-8');u.unregister_slate_post_tick_callback(gate_check_handle);u.EditorLevelLibrary.editor_end_play()
 except:
  import traceback
  gate_report['error']=traceback.format_exc();check_out.write_text(json.dumps(gate_report),encoding='utf-8');u.unregister_slate_post_tick_callback(gate_check_handle);u.EditorLevelLibrary.editor_end_play()
gate_check_handle=u.register_slate_post_tick_callback(gate_check_tick)


