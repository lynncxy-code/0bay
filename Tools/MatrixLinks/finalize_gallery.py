import unreal as u,json
from pathlib import Path
if not globals().get('gallery_final_layout',False):
 gallery_final_layout=True
 actors=u.get_editor_subsystem(u.EditorActorSubsystem);ml=u.MaterialEditingLibrary;lib=u.EditorAssetLibrary
 folder='/Game/MatrixDeviceLinks/Preview'
 dot=u.AssetToolsHelpers.get_asset_tools().create_asset('M_Endpoint',folder,u.Material,u.MaterialFactoryNew());dot.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
 color=ml.create_material_expression(dot,u.MaterialExpressionConstant3Vector);color.set_editor_property('constant',u.LinearColor(.15,.25,.3,1));ml.connect_material_property(color,'',u.MaterialProperty.MP_EMISSIVE_COLOR);ml.recompile_material(dot);lib.save_loaded_asset(dot)
 pulse_labels=[]
 for a in actors.get_all_level_actors():
  name=a.get_actor_label()
  if isinstance(a,u.TextRenderActor):
   a.set_actor_rotation(u.Rotator(pitch=0,yaw=180,roll=0),False)
   if not name.startswith('LABEL_'):pulse_labels.append(a)
  if isinstance(a,u.NiagaraActor):
   c=a.get_component_by_class(u.NiagaraComponent);c.set_editor_property('allow_scalability',False);c.set_preview_lod_distance(True,0,100000)
  if name.startswith('PULSE_'):
   i=int(name.split('_')[1])-1;center=u.Vector(0,(i%5)*850,-1650-(i//5)*450)
   if name.endswith('Independent_Link'):
    a.set_actor_location_and_rotation(center,u.MathLibrary.make_rot_from_xz(u.Vector(0,1,0),u.Vector(-1,0,0)),False,True);a.set_actor_scale3d(u.Vector(6,.025 if i<5 else .05,1))
   else:
    side=-1 if name.endswith('Source') else 1;a.set_actor_location(center+u.Vector(0,side*320,0),False,True);a.static_mesh_component.set_material(0,dot)
 for label in pulse_labels:
  text=str(label.text_render.get_editor_property('text'));i=int(text[1:3])-1
  center=u.Vector(-50,(i%5)*850,-1650-(i//5)*450-120);label.set_actor_location(center,False,True);label.text_render.set_world_size(35)
 u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(u.Vector(-2700,1750,550),u.Rotator())
 u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
 Path('D:/0bay/OfficeShell/Saved/MatrixLinks/gallery_validation.json').write_text(json.dumps({'native_effects':len([a for a in actors.get_all_level_actors() if isinstance(a,u.NiagaraActor)]),'independent_pulse_links':len([a for a in actors.get_all_level_actors() if a.get_actor_label().endswith('Independent_Link')]),'endpoints':len([a for a in actors.get_all_level_actors() if a.get_actor_label().endswith(('Source','Target'))]),'style_selection_pending':True},indent=2))
 u.EditorLevelLibrary.editor_play_simulate()
