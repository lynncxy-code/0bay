import unreal as u,time,json,traceback
from pathlib import Path
OUT=Path('D:/0bay/OfficeShell/Saved/MatrixLinks/runtime-88-verification.json')
started=None;stage=0;handle=None;comps=[];report={'expected':88,'phase':'after_material_enhancement'}
def tick(dt):
 global started,stage,handle,comps,report
 try:
  w=u.EditorLevelLibrary.get_game_world()
  if not w:return
  if started is None:started=time.monotonic()
  if time.monotonic()-started<5:return
  items=u.GameplayStatics.get_all_actors_with_tag(w,'SecurityCommunicationVFX'); lines=[a for a in items if a.actor_has_tag('MatrixDeviceLink')]
  if stage==0:
   report['startup']={'total':len(lines),'vfx_total':len(items),'hidden':sum(a.get_editor_property('hidden') for a in lines),'active':sum(c.is_active() for a in items for c in a.get_components_by_class(u.NiagaraComponent))}
   if len(lines)!=88:raise RuntimeError('expected 88 lines, got %d'%len(lines))
   comps=[c for a in u.GameplayStatics.get_all_actors_of_class(w,u.Actor) for c in a.get_components_by_class(u.OntoTwinWebInteractionComponent)]
   if not comps:raise RuntimeError('business component missing')
   report['open_call']=bool(comps[0].open_business_view('bv.network-security','')); stage=1; started=time.monotonic(); return
  if stage==1:
   mesh_checks=[]
   for a in lines:
    c=a.get_component_by_class(u.SplineMeshComponent);mat=c.get_material(0)
    mesh_checks.append({'actor':a.get_name(),'visible':c.is_visible(),'hidden_in_game':c.get_editor_property('hidden_in_game'),'mesh':bool(c.get_editor_property('static_mesh')),'material_parent':mat.get_editor_property('parent').get_path_name()})
   report['mesh_checks']=mesh_checks
   assert all(x['visible'] and not x['hidden_in_game'] and x['mesh'] and 'M_CommunicationPulse_Bright' in x['material_parent'] for x in mesh_checks)
   report['security_open']={'total':len(lines),'vfx_total':len(items),'visible':sum(not a.get_editor_property('hidden') for a in lines),'active':sum(c.is_active() for a in items for c in a.get_components_by_class(u.NiagaraComponent))}; comps[0].back(); stage=2; started=time.monotonic(); return
  report['after_back']={'total':len(lines),'vfx_total':len(items),'hidden':sum(a.get_editor_property('hidden') for a in lines),'active':sum(c.is_active() for a in items for c in a.get_components_by_class(u.NiagaraComponent))}; report['ok']=report['startup']['total']==88 and report['security_open']['visible']==88 and report['after_back']['hidden']==88
  OUT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8'); u.unregister_slate_post_tick_callback(handle); u.EditorLevelLibrary.editor_end_play()
 except Exception:
  report['error']=traceback.format_exc(); OUT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8'); u.unregister_slate_post_tick_callback(handle); u.EditorLevelLibrary.editor_end_play()
handle=u.register_slate_post_tick_callback(tick); u.get_editor_subsystem(u.LevelEditorSubsystem).editor_play_simulate()
