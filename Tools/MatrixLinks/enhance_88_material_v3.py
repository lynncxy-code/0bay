import unreal as u,json
from pathlib import Path
OUT=Path('D:/0bay/OfficeShell/Saved/MatrixLinks');lib=u.EditorAssetLibrary;ml=u.MaterialEditingLibrary;folder='/Game/MatrixDeviceLinks/Preview';lib.make_directory(folder)
mat=lib.load_asset(folder+'/M_CommunicationPulse_Bright')
if not mat:
 mat=u.AssetToolsHelpers.get_asset_tools().create_asset('M_CommunicationPulse_Bright',folder,u.Material,u.MaterialFactoryNew());assert mat
mat.set_editor_property('blend_mode',u.BlendMode.BLEND_ADDITIVE);mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT);mat.set_editor_property('two_sided',True)
def ex(cls,x,y):return ml.create_material_expression(mat,cls,x,y)
uv=ex(u.MaterialExpressionTextureCoordinate,-700,0);clock=ex(u.MaterialExpressionTime,-700,120);mode=ex(u.MaterialExpressionScalarParameter,-700,240);mode.set_editor_property('parameter_name','Mode');phase=ex(u.MaterialExpressionScalarParameter,-700,350);phase.set_editor_property('parameter_name','Phase')
custom=ex(u.MaterialExpressionCustom,-200,0);custom.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT1);ins=[]
for name in ['UV','Clock','Mode','Phase']:
 ci=u.CustomInput();ci.set_editor_property('input_name',name);ins.append(ci)
custom.set_editor_property('inputs',ins);custom.set_editor_property('code','''float t=Clock*.25+Phase; float width=.045; float x=UV.x; float p=frac(t); float d=abs(x-p); float packet=1.65*exp(-d*d/(width*width)); float edge=exp(-pow((UV.y-.5)*8,2)); float cross=lerp(edge,edge*1.35,saturate(packet)); float endFade=saturate(x*42)*saturate((1-x)*42); return saturate(packet)*cross*endFade;''')
for node,name in [(uv,'UV'),(clock,'Clock'),(mode,'Mode'),(phase,'Phase')]:ml.connect_material_expressions(node,'',custom,name)
color=ex(u.MaterialExpressionVectorParameter,-200,350);color.set_editor_property('parameter_name','Color');color.set_editor_property('default_value',u.LinearColor(0.15,4.5,1.0,1));ml.connect_material_property(color,'',u.MaterialProperty.MP_EMISSIVE_COLOR);ml.connect_material_property(custom,'',u.MaterialProperty.MP_OPACITY);ml.recompile_material(mat);lib.save_loaded_asset(mat)
updated=[]
for i in range(1,11):
 inst=lib.load_asset(folder+'/MI_Pulse_%02d'%i);assert inst
 ml.set_material_instance_parent(inst,mat);old=ml.get_material_instance_vector_parameter_value(inst,'Color');old.r*=1.45;old.g*=1.45;old.b*=1.45;old.a=1;ml.set_material_instance_vector_parameter_value(inst,'Color',old);lib.save_loaded_asset(inst);updated.append(inst.get_path_name())
(OUT/'links88-material-enhancement.json').write_text(json.dumps({'ok':True,'material':mat.get_path_name(),'instances':updated,'packet_gain':1.65,'edge_exponent':8,'endpoint_fade':42},ensure_ascii=False,indent=2),encoding='utf-8');print('MATERIAL_ENHANCEMENT_SAVED',len(updated))
