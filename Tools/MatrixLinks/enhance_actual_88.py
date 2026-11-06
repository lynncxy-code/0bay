import unreal as u,json,shutil,datetime
from pathlib import Path
R=Path('D:/0bay/OfficeShell');O=R/'Saved/MatrixLinks';L=u.EditorAssetLibrary;M=u.MaterialEditingLibrary
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);assert ls.load_level('/Game/MatrixDeviceLinks/L_MatrixDeviceLinks')
links=[a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if a.actor_has_tag('MatrixDeviceLink')];assert len(links)==88
mats={a.get_component_by_class(u.SplineMeshComponent).get_material(0).get_path_name():a.get_component_by_class(u.SplineMeshComponent).get_material(0) for a in links};assert len(mats)==10
backup=O/('BrightnessBackup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir()
for path,mat in mats.items():shutil.copy2(R/'Content'/(path.split('.')[0].removeprefix('/Game/')+'.uasset'),backup/(mat.get_name()+'.uasset'))
bright=L.load_asset('/Game/MatrixDeviceLinks/Preview/M_CommunicationPulse_Bright');assert bright
custom=M.get_material_property_input_node(bright,u.MaterialProperty.MP_OPACITY);assert isinstance(custom,u.MaterialExpressionCustom)
custom.set_editor_property('code','''float t=Clock*.25+Phase;
float width=.045; float x=UV.x;
float p=frac(t); float d=abs(x-p);
float packet=exp(-d*d/(width*width));
if(Mode>0.5 && Mode<1.5){float q=frac(t+.20);packet+=.6*exp(-pow((x-q)/width,2));}
if(Mode>1.5 && Mode<2.5){float q=1-frac(t);packet+=exp(-pow((x-q)/width,2));}
if(Mode>2.5 && Mode<3.5){packet*=step(.35,frac(Clock*.35));}
if(Mode>3.5){packet=pow(saturate(1-abs(frac(x*4-t)-.5)*8),2);}
float edge=1-smoothstep(.28,.48,abs(UV.y-.5));
return saturate(packet*1.3)*edge*saturate(x*35)*saturate((1-x)*35);
''')
bright.set_editor_property('used_with_spline_meshes',True);M.recompile_material(bright);assert L.save_loaded_asset(bright)
records=[]
for path,mat in sorted(mats.items()):
 old=M.get_material_instance_vector_parameter_value(mat,'Color');mode=M.get_material_instance_scalar_parameter_value(mat,'Mode');phase=M.get_material_instance_scalar_parameter_value(mat,'Phase')
 before=[old.r,old.g,old.b,old.a];new=u.LinearColor(old.r*1.65,old.g*1.65,old.b*1.65,1)
 M.set_material_instance_parent(mat,bright);M.set_material_instance_vector_parameter_value(mat,'Color',new);assert L.save_loaded_asset(mat)
 assert M.get_material_instance_scalar_parameter_value(mat,'Mode')==mode
 records.append(dict(asset=path,mode=mode,phase=phase,before=before,after=[new.r,new.g,new.b,1]))
# Restore the preview gallery changed by the earlier attempt; scene uses MI_Communication assets.
original=L.load_asset('/Game/MatrixDeviceLinks/Preview/M_CommunicationPulse')
colors=[(.1,3,.5),(.1,2,3),(3,1.5,.1),(2,.3,3),(2.5,2.5,2.5)]
for i in range(1,11):
 mat=L.load_asset('/Game/MatrixDeviceLinks/Preview/MI_Pulse_%02d'%i);M.set_material_instance_parent(mat,original);M.set_material_instance_vector_parameter_value(mat,'Color',u.LinearColor(*colors[(i-1)%5],1));assert L.save_loaded_asset(mat)
report=dict(ok=True,scene_lines=88,unique_scene_materials=len(mats),emissive_gain=1.65,packet_gain=1.3,edge='flat bright core with smooth edge falloff',all_modes_preserved=True,records=records,backup=str(backup))
(O/'links88-material-enhancement.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
