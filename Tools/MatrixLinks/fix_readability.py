import unreal as u
if not globals().get('gallery_readability_fixed',False):
 gallery_readability_fixed=True
 ml=u.MaterialEditingLibrary;lib=u.EditorAssetLibrary
 textmat=lib.duplicate_asset('/Engine/EngineMaterials/DefaultTextMaterialOpaque','/Game/MatrixDeviceLinks/Preview/M_PreviewLabel')
 source=ml.get_material_property_input_node(textmat,u.MaterialProperty.MP_BASE_COLOR)
 output=ml.get_material_property_input_node_output_name(textmat,u.MaterialProperty.MP_BASE_COLOR)
 textmat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
 assert source
 ml.connect_material_property(source,output,u.MaterialProperty.MP_EMISSIVE_COLOR);ml.recompile_material(textmat);lib.save_loaded_asset(textmat)
 for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
  if isinstance(a,u.TextRenderActor):
   a.text_render.set_text_material(textmat);a.set_actor_rotation(u.Rotator(pitch=0,yaw=180,roll=0),False)
  if isinstance(a,u.PostProcessVolume):
   s=a.get_editor_property('settings');s.set_editor_property('override_auto_exposure_bias',True);s.set_editor_property('auto_exposure_bias',0);s.set_editor_property('override_auto_exposure_apply_physical_camera_exposure',True);s.set_editor_property('auto_exposure_apply_physical_camera_exposure',False);a.set_editor_property('settings',s)
  if isinstance(a,u.CameraActor):a.set_actor_location(u.Vector(-2700,1750,550),False,True)
 u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
 u.EditorLevelLibrary.editor_play_simulate()
