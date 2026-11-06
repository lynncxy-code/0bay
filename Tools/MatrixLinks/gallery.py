import unreal as u,json,random
from pathlib import Path
out=Path('D:/0bay/OfficeShell/Saved/MatrixLinks');folder='/Game/MatrixDeviceLinks/Preview'
lib=u.EditorAssetLibrary;ml=u.MaterialEditingLibrary;actors=u.get_editor_subsystem(u.EditorActorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
lib.make_directory(folder)
exec(open('D:/0bay/OfficeShell/Tools/Go2Scan/live_bridge.py',encoding='utf-8-sig').read())
assert levels.new_level(folder+'/L_MatrixStyleGallery_v2')
# Native authored systems, arranged in 18 independent numbered preview bays.
for i in range(18):
 col=i%6;row=i//6;pos=u.Vector(0,col*1050,(2-row)*1100)
 actor=actors.spawn_actor_from_class(u.NiagaraActor,pos)
 actor.set_actor_label('STYLE_%02d_NS_Symbols_%02d'%(i+1,i+1));actor.set_folder_path('01_OriginalStyles')
 comp=actor.get_component_by_class(u.NiagaraComponent);comp.set_asset(u.load_asset('/Game/RainSymbols_VFX/Niagara_VFX/NS_Symbols_%02d'%(i+1)))
 actor.set_actor_scale3d(u.Vector(.22,.22,.22));comp.activate(True)
 label=actors.spawn_actor_from_class(u.TextRenderActor,pos+u.Vector(-350,0,-400),u.Rotator(0,180,0));label.set_actor_label('LABEL_%02d'%(i+1));c=label.text_render;c.set_text('%02d  NS_Symbols_%02d'%(i+1,i+1));c.set_world_size(54);c.set_horizontal_alignment(u.HorizTextAligment.EHTA_CENTER)
# New pulse material; no solid full-length stripe, only short moving packets.
mat=u.AssetToolsHelpers.get_asset_tools().create_asset('M_CommunicationPulse',folder,u.Material,u.MaterialFactoryNew());mat.set_editor_property('blend_mode',u.BlendMode.BLEND_ADDITIVE);mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT);mat.set_editor_property('two_sided',True)
def expr(cls,x,y):return ml.create_material_expression(mat,cls,x,y)
uv=expr(u.MaterialExpressionTextureCoordinate,-600,0);clock=expr(u.MaterialExpressionTime,-600,120)
mode=expr(u.MaterialExpressionScalarParameter,-600,240);mode.set_editor_property('parameter_name','Mode');mode.set_editor_property('default_value',0)
phase=expr(u.MaterialExpressionScalarParameter,-600,350);phase.set_editor_property('parameter_name','Phase');phase.set_editor_property('default_value',0)
custom=expr(u.MaterialExpressionCustom,-200,0);custom.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT1)
inputs=[]
for name in ['UV','Clock','Mode','Phase']:
 i=u.CustomInput();i.set_editor_property('input_name',name);inputs.append(i)
custom.set_editor_property('inputs',inputs)
custom.set_editor_property('code','''float t=Clock*.25+Phase;
float width=.045; float x=UV.x;
float p=frac(t); float d=abs(x-p);
float packet=exp(-d*d/(width*width));
if(Mode>0.5 && Mode<1.5){float q=frac(t+.20);packet+=.6*exp(-pow((x-q)/width,2));}
if(Mode>1.5 && Mode<2.5){float q=1-frac(t);packet+=exp(-pow((x-q)/width,2));}
if(Mode>2.5 && Mode<3.5){packet*=step(.35,frac(Clock*.35));}
if(Mode>3.5){packet=pow(saturate(1-abs(frac(x*4-t)-.5)*8),2);}
float cross=exp(-pow((UV.y-.5)*5,2));
return saturate(packet)*cross*saturate(x*35)*saturate((1-x)*35);
''')
for node,name in [(uv,'UV'),(clock,'Clock'),(mode,'Mode'),(phase,'Phase')]:ml.connect_material_expressions(node,'',custom,name)
color=expr(u.MaterialExpressionVectorParameter,-200,350);color.set_editor_property('parameter_name','Color');color.set_editor_property('default_value',u.LinearColor(.1,3,.6,1));ml.connect_material_property(color,'',u.MaterialProperty.MP_EMISSIVE_COLOR);ml.connect_material_property(custom,'',u.MaterialProperty.MP_OPACITY);ml.recompile_material(mat);lib.save_loaded_asset(mat)
plane=u.load_asset('/Engine/BasicShapes/Plane');sphere=u.load_asset('/Engine/BasicShapes/Sphere')
# Ten independent pairs: distinct endpoints for every sample, not a chained network.
colors=[u.LinearColor(.1,3,.5,1),u.LinearColor(.1,2,3,1),u.LinearColor(3,1.5,.1,1),u.LinearColor(2,.3,3,1),u.LinearColor(2.5,2.5,2.5,1)]
labels=['Single packet','Packet pair','Bidirectional','Intermittent','Packet train']
for i in range(10):
 col=i%5;row=i//5;center=u.Vector(0,col*1250,-1650-row*600)
 inst=u.AssetToolsHelpers.get_asset_tools().create_asset('MI_Pulse_%02d'%(i+1),folder,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew());ml.set_material_instance_parent(inst,mat);ml.set_material_instance_scalar_parameter_value(inst,'Mode',i%5);ml.set_material_instance_scalar_parameter_value(inst,'Phase',i*.13);ml.set_material_instance_vector_parameter_value(inst,'Color',colors[i%5]);lib.save_loaded_asset(inst)
 link=actors.spawn_actor_from_class(u.StaticMeshActor,center,u.Rotator(0,90,90));link.set_actor_label('PULSE_%02d_Independent_Link'%(i+1));link.set_folder_path('02_ThinPulseLinks');c=link.static_mesh_component;c.set_static_mesh(plane);c.set_material(0,inst);c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);link.set_actor_scale3d(u.Vector(8,.025 if row==0 else .05,1))
 for side in [-1,1]:
  node=actors.spawn_actor_from_class(u.StaticMeshActor,center+u.Vector(0,side*430,0));node.set_actor_label('PULSE_%02d_%s'%(i+1,'Source' if side<0 else 'Target'));node.static_mesh_component.set_static_mesh(sphere);node.set_actor_scale3d(u.Vector(.25,.25,.25));node.static_mesh_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
 label=actors.spawn_actor_from_class(u.TextRenderActor,center+u.Vector(-50,0,-120),u.Rotator(0,180,0));c=label.text_render;c.set_text('P%02d  %s  %s cm'%(i+1,labels[i%5],'2.5' if row==0 else '5'));c.set_world_size(42);c.set_horizontal_alignment(u.HorizTextAligment.EHTA_CENTER)
# Fixed exposure, unobstructed overview, no changes to the office map or scanner.
pp=actors.spawn_actor_from_class(u.PostProcessVolume,u.Vector());pp.set_editor_property('unbound',True);s=pp.get_editor_property('settings');s.set_editor_property('override_auto_exposure_method',True);s.set_editor_property('auto_exposure_method',u.AutoExposureMethod.AEM_MANUAL);s.set_editor_property('override_auto_exposure_bias',True);s.set_editor_property('auto_exposure_bias',0);pp.set_editor_property('settings',s)
cam=actors.spawn_actor_from_class(u.CameraActor,u.Vector(-6800,2600,150),u.Rotator(0,0,0));cam.set_actor_label('Gallery_Overview');cam.camera_component.set_field_of_view(65)
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(cam.get_actor_location(),cam.get_actor_rotation())
assert levels.save_current_level()
(out/'gallery.json').write_text(json.dumps({'map':folder+'/L_MatrixStyleGallery_v2','original_styles':18,'independent_pulse_links':10,'widths_cm':[2.5,5],'office_unchanged':True},indent=2))
exec(open('D:/0bay/OfficeShell/Tools/Go2Scan/live_bridge.py',encoding='utf-8-sig').read())
print('MATRIX_STYLE_GALLERY_READY')
