import unreal as u,json,traceback
from pathlib import Path
ROOT=Path('D:/0bay/OfficeShell');OUT=ROOT/'Saved/MatrixLinks';lib=u.EditorAssetLibrary;ml=u.MaterialEditingLibrary
report={}
mat=lib.load_asset('/Game/MatrixDeviceLinks/Preview/M_CommunicationPulse');assert mat
customs=mat.get_editor_property('expressions')
custom=[x for x in customs if isinstance(x,u.MaterialExpressionCustom)]
assert custom, 'custom pulse expression missing'
code=custom[0].get_editor_property('code')
Path(OUT/'material-before-brightness.txt').write_text(code,encoding='utf-8')
code=code.replace('packet=exp(-d*d/(width*width));','packet=1.65*exp(-d*d/(width*width));')
code=code.replace('float cross=exp(-pow((UV.y-.5)*5,2));','float edge=exp(-pow((UV.y-.5)*8,2));\nfloat cross=lerp(edge,edge*1.35,saturate(packet));')
code=code.replace('return saturate(packet)*cross*saturate(x*35)*saturate((1-x)*35);','float endFade=saturate(x*42)*saturate((1-x)*42);\nreturn saturate(packet)*cross*endFade;')
custom[0].set_editor_property('code',code);ml.recompile_material(mat);lib.save_loaded_asset(mat)
report['material_code_updated']=True
updated=[]
for i in range(1,11):
 inst=lib.load_asset('/Game/MatrixDeviceLinks/Preview/MI_Pulse_%02d'%i)
 if not inst:continue
 # Drive a brighter linear-space color; keep existing hue per P01-P10.
 old=ml.get_material_instance_vector_parameter_value(inst,'Color')
 c=old; c.r*=1.45;c.g*=1.45;c.b*=1.45;c.a=1.0
 ml.set_material_instance_vector_parameter_value(inst,'Color',c);lib.save_loaded_asset(inst)
 updated.append({'asset':inst.get_path_name(),'color':[c.r,c.g,c.b,c.a]})
report['bright_instances']=updated
(OUT/'links88-material-enhancement.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('MATERIAL_ENHANCEMENT_SAVED',len(updated))
