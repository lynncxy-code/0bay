import unreal as u,json,shutil,datetime
from pathlib import Path
R=Path('D:/0bay/OfficeShell');O=R/'Saved/MatrixLinks';L=u.EditorAssetLibrary;M=u.MaterialEditingLibrary
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);assert ls.load_level('/Game/MatrixDeviceLinks/L_MatrixDeviceLinks')
links=[a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if a.actor_has_tag('MatrixDeviceLink')];assert len(links)==150
mats={a.get_component_by_class(u.SplineMeshComponent).get_material(0).get_path_name():a.get_component_by_class(u.SplineMeshComponent).get_material(0) for a in links};assert len(mats)==10
bright=L.load_asset('/Game/MatrixDeviceLinks/Preview/M_CommunicationPulse_Bright');assert bright
backup=O/('Dense150Backup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir()
for mat in [bright]+list(mats.values()):shutil.copy2(R/'Content'/(mat.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset'),backup/(mat.get_name()+'.uasset'))
custom=M.get_material_property_input_node(bright,u.MaterialProperty.MP_OPACITY);assert isinstance(custom,u.MaterialExpressionCustom)
custom.set_editor_property('code','''// Six dense packet bands, 72-84 percent longitudinal coverage.
float x=UV.x; float t=Clock*3.0+Phase;
float q=frac(x*6.0-t); float d=abs(q-.5);
float packet=1-smoothstep(.34,.38,d);
if(Mode>.5 && Mode<1.5){packet=1-smoothstep(.38,.42,d);packet*=.85+.15*cos(q*12.56637);}
if(Mode>1.5 && Mode<2.5){float qr=frac(x*6.0+t+.5);float reverse=1-smoothstep(.34,.38,abs(qr-.5));packet=lerp(packet,reverse,step(.5,UV.y));}
if(Mode>2.5 && Mode<3.5){packet*=.80+.20*sin(Clock*9.42478+Phase*6.28318);}
if(Mode>3.5){q=frac(x*8.0-Clock*4.0+Phase);packet=1-smoothstep(.36,.40,abs(q-.5));}
float edge=1-smoothstep(.30,.49,abs(UV.y-.5));
return saturate(packet)*edge*saturate(x*45)*saturate((1-x)*45);
''')
M.recompile_material(bright);assert L.save_loaded_asset(bright)
records=[]
for path,mat in sorted(mats.items()):
 old=M.get_material_instance_vector_parameter_value(mat,'Color');peak=max(old.r,old.g,old.b)
 # Brighter, lighter colors with modest white mixing, preserving hue families.
 new=u.LinearColor((old.r*.88+peak*.12)*1.3,(old.g*.88+peak*.12)*1.3,(old.b*.88+peak*.12)*1.3,1)
 M.set_material_instance_vector_parameter_value(mat,'Color',new);assert L.save_loaded_asset(mat)
 records.append(dict(asset=path,before=[old.r,old.g,old.b],after=[new.r,new.g,new.b]))
(O/'links150-material-enhancement.json').write_text(json.dumps(dict(ok=True,lines=150,packet_bands='6-8',coverage='approximately 72-80% excluding endpoint fade',packet_passes_per_second='3-4',travel_cycles_per_second=.5,emissive_gain_vs_previous=1.3,white_mix=.12,records=records,backup=str(backup)),ensure_ascii=False,indent=2),encoding='utf-8')
