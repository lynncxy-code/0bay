import unreal as u,json,random,math,shutil,datetime
from pathlib import Path
out=Path('D:/0bay/OfficeShell/Saved/MatrixLinks')
root=Path('D:/0bay/OfficeShell')
backup=out/('Backup_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir()
shutil.copy2(root/'Content/OfficeShell/Maps/Office_Demo_ArtMerged.umap',backup/'Office_Demo_ArtMerged.umap')
actors=u.get_editor_subsystem(u.EditorActorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem);lib=u.EditorAssetLibrary;ml=u.MaterialEditingLibrary
# Select distinct physical devices; multipart models at one position count once.
candidates=[];positions=set()
for a in actors.get_all_level_actors():
 for c in a.get_components_by_class(u.StaticMeshComponent):
  sm=c.get_editor_property('static_mesh')
  if not sm:continue
  path=sm.get_path_name()
  if 'OfficeDevices_20261103' not in path and 'SM_MonitorCamera_Frozen' not in path:continue
  p=c.get_world_location();key=tuple(round(v/10) for v in p.to_tuple())
  if p.z<4200 or key in positions:continue
  positions.add(key)
  # Bounds center is reliable for CAD meshes with offset pivots.
  center,extent,radius=u.SystemLibrary.get_component_bounds(c)
  candidates.append((a,center))
assert len(candidates)>=10
chosen=random.Random(11032026).sample(sorted(candidates,key=lambda x:x[0].get_path_name()),10)
# Minimum spanning tree connects all ten without an unreadable all-to-all web.
edges=[];connected={0}
while len(connected)<10:
 _,i,j=min(((chosen[i][1]-chosen[j][1]).length(),i,j) for i in connected for j in range(10) if j not in connected)
 edges.append((i,j));connected.add(j)
folder='/Game/MatrixDeviceLinks';lib.make_directory(folder)
assert not lib.does_asset_exist(folder+'/L_MatrixDeviceLinks'),'Demo already exists; inspect instead of duplicating'
mat=lib.duplicate_asset('/Game/RainSymbols_VFX/Materials/M_Symbols',folder+'/M_MatrixLinkFlow')
ml.delete_all_material_expressions(mat)
def expr(cls,x=0,y=0):return ml.create_material_expression(mat,cls,x,y)
uv=expr(u.MaterialExpressionTextureCoordinate,-800,0)
t=expr(u.MaterialExpressionTime,-800,160)
tex=expr(u.MaterialExpressionTextureObjectParameter,-800,320);tex.set_editor_property('parameter_name','Symbols');tex.set_editor_property('texture',u.load_asset('/Game/RainSymbols_VFX/Textures/Symbols/T_Symbols_EN-1'))
count=expr(u.MaterialExpressionScalarParameter,-800,480);count.set_editor_property('parameter_name','SymbolCount');count.set_editor_property('default_value',40)
speed=expr(u.MaterialExpressionScalarParameter,-800,600);speed.set_editor_property('parameter_name','FlowSpeed');speed.set_editor_property('default_value',2.5)
custom=expr(u.MaterialExpressionCustom,-400,0);custom.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT1)
inputs=[]
for name in ['UV','Clock','Atlas','Count','Speed']:
 i=u.CustomInput();i.set_editor_property('input_name',name);inputs.append(i)
custom.set_editor_property('inputs',inputs)
custom.set_editor_property('code','''float travel=UV.x*Count-Clock*Speed;
float cell=floor(travel);
float symbol=floor(frac(sin(cell*12.9898+7.17)*43758.5453)*48.0);
float2 tile=float2(fmod(symbol,8.0),floor(symbol/8.0));
float2 inner=float2(frac(travel),UV.y);
float2 atlasUV=(tile+inner)/float2(8.0,6.0);
float glyph=Texture2DSample(Atlas,AtlasSampler,atlasUV).a;
float trail=.25+.65*pow(frac(travel/14.0),2.0);
float ends=saturate(UV.x*40)*saturate((1-UV.x)*40);
return glyph*trail*ends;
''')
for node,name in [(uv,'UV'),(t,'Clock'),(tex,'Atlas'),(count,'Count'),(speed,'Speed')]:ml.connect_material_expressions(node,'',custom,name)
color=expr(u.MaterialExpressionVectorParameter,-400,400);color.set_editor_property('parameter_name','FlowColor');color.set_editor_property('default_value',u.LinearColor(.05,2.5,.35,1))
ml.connect_material_property(color,'',u.MaterialProperty.MP_EMISSIVE_COLOR);ml.connect_material_property(custom,'',u.MaterialProperty.MP_OPACITY)
ml.recompile_material(mat);lib.save_loaded_asset(mat)
stream=u.EditorLevelUtils.create_new_streaming_level(u.LevelStreamingAlwaysLoaded,folder+'/L_MatrixDeviceLinks',False)
assert stream and levels.set_current_level_by_name('L_MatrixDeviceLinks')
plane=u.load_asset('/Engine/BasicShapes/Plane')
created=[]
for e,(i,j) in enumerate(edges):
 a,b=chosen[i][1],chosen[j][1];d=b-a;length=d.length();mid=(a+b)*.5
 inst=u.AssetToolsHelpers.get_asset_tools().create_asset('MI_Link_%02d'%e,folder,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
 ml.set_material_instance_parent(inst,mat);ml.set_material_instance_scalar_parameter_value(inst,'SymbolCount',max(3,length/12));ml.set_material_instance_scalar_parameter_value(inst,'FlowSpeed',2.5);lib.save_loaded_asset(inst)
 # Two narrow crossed, double-sided strips preserve readability from side and above.
 for face in range(2):
  rotation=u.MathLibrary.make_rot_from_xz(d,u.Vector(0,0,1) if face==0 else u.Vector(0,1,0))
  actor=actors.spawn_actor_from_class(u.StaticMeshActor,mid,rotation);actor.set_actor_label('Matrix_Link_%02d_%s_to_%s_%d'%(e,chosen[i][0].get_actor_label(),chosen[j][0].get_actor_label(),face));actor.set_folder_path('VFX/MatrixDeviceLinks')
  mesh=actor.static_mesh_component;mesh.set_static_mesh(plane);mesh.set_material(0,inst);mesh.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);mesh.set_editor_property('cast_shadow',False)
  actor.set_actor_scale3d(u.Vector(length/100,12/100,1));actor.set_editor_property('tags',['MatrixDeviceLink']);created.append(actor)
assert levels.save_current_level()
u.EditorLoadingAndSavingUtils.save_dirty_packages(True,False)
report={'seed':11032026,'candidate_count':len(candidates),'selected':[{'actor':a.get_path_name(),'label':a.get_actor_label(),'center':p.to_tuple()} for a,p in chosen],'edges':edges,'link_count':len(edges),'actor_count':len(created),'level':folder+'/L_MatrixDeviceLinks','source':'RainSymbols_VFX M_Symbols + T_Symbols_EN-1, adapted for point-to-point flow'}
(out/'installed.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
center=sum((p for a,p in chosen),u.Vector())/10
view=center+u.Vector(600,1500,700)
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(view,u.MathLibrary.find_look_at_rotation(view,center))
actors.set_selected_level_actors([])
print('MATRIX_LINKS_INSTALLED')
