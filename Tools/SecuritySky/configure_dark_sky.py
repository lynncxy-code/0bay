"""Sample only a quiet patch of Example_Map_11, with editable angle/tone controls."""
import json
import shutil
from datetime import datetime
from pathlib import Path
import unreal as u

root=Path('D:/0bay/OfficeShell')
out=root/'Saved/SecuritySky'/datetime.now().strftime('%Y%m%d-%H%M%S-dark')
out.mkdir(parents=True,exist_ok=True)
folder='/Game/OfficeShell/Materials'
name='M_SecurityTexturedHDRI_v2'
for asset in ['MI_SecurityOverview_Example11',name]:
    file=root/'Content/OfficeShell/Materials'/(asset+'.uasset')
    if file.exists(): shutil.copy2(file,out/file.name)
path=folder+'/'+name
m=u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else u.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,u.Material,u.MaterialFactoryNew())
u.MaterialEditingLibrary.delete_all_material_expressions(m)
m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('two_sided',True)
m.set_editor_property('is_sky',True)
def node(cls,x,y): return u.MaterialEditingLibrary.create_material_expression(m,cls,x,y)
texture=node(u.MaterialExpressionTextureObjectParameter,-600,-300)
texture.set_editor_property('parameter_name','HDRI_Map')
texture.set_editor_property('texture',u.load_asset('/Game/HDRI_Futuristic_Technology/HDRI/HDR_FT_02'))
view=node(u.MaterialExpressionCameraVectorWS,-600,-100)
defaults=dict(YawDegrees=120.0,PitchDegrees=65.0,ViewSpread=0.52,Brightness=0.95,Saturation=0.55,HighlightLimit=0.18)
inputs={'Tex':texture,'View':view}
for i,(key,value) in enumerate(defaults.items()):
    n=node(u.MaterialExpressionScalarParameter,-600,100+i*100)
    n.set_editor_property('parameter_name',key)
    n.set_editor_property('default_value',value)
    n.set_editor_property('group','Quiet Background')
    inputs[key]=n
custom=node(u.MaterialExpressionCustom,0,0)
custom.set_editor_property('description','Quiet HDRI patch: editable yaw / pitch, muted highlights')
custom.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
custom_inputs=[]
for key in inputs:
    ci=u.CustomInput()
    ci.set_editor_property('input_name',key)
    custom_inputs.append(ci)
custom.set_editor_property('inputs',custom_inputs)
custom.set_editor_property('code','''
float yaw = radians(YawDegrees);
float pitch = radians(PitchDegrees);
float3 center = float3(cos(pitch)*cos(yaw), cos(pitch)*sin(yaw), sin(pitch));
// Keep enough angular range for the HDRI's shapes to read, while aiming at its quiet sky region.
float3 direction = normalize(center - View * clamp(ViewSpread, 0.0, 0.75));
float3 color = TextureCubeSampleLevel(Tex, TexSampler, direction, 0.5).rgb;
float gray = dot(color, float3(0.2126,0.7152,0.0722));
color = lerp(gray.xxx, color, saturate(Saturation));
color = pow(max(color, 0.0), 0.82);
float peak = max(color.r, max(color.g, color.b));
color /= 1.0 + peak / max(HighlightLimit, 0.001);
// Blue-black grade that preserves luminance texture instead of flattening to a solid tint.
color = lerp(color, float3(gray * 0.20, gray * 0.42, gray * 0.72), 0.48);
return (color + float3(0.0005, 0.0015, 0.0035)) * max(Brightness, 0.0);
''')
for key,n in inputs.items(): assert u.MaterialEditingLibrary.connect_material_expressions(n,'',custom,key)
assert u.MaterialEditingLibrary.connect_material_property(custom,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
u.MaterialEditingLibrary.recompile_material(m)
assert u.EditorAssetLibrary.save_loaded_asset(m)
mi_path=folder+'/MI_SecurityTexturedHDRI_v2'
mi=u.load_asset(mi_path) if u.EditorAssetLibrary.does_asset_exist(mi_path) else u.AssetToolsHelpers.get_asset_tools().create_asset(
    'MI_SecurityTexturedHDRI_v2', folder, u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
u.MaterialEditingLibrary.set_material_instance_parent(mi,m)
for key,value in defaults.items(): u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi,key,value)
u.MaterialEditingLibrary.update_material_instance(mi)
assert u.EditorAssetLibrary.save_loaded_asset(mi)
(out/'settings.json').write_text(json.dumps(defaults,indent=2))
print('QUIET_SKY_CONFIGURED',json.dumps(defaults))

