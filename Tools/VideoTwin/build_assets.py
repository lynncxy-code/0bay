"""Create isolated MVP assets/map. Never save the source Office_Demo map/sublevels."""
import unreal as u
import json
from pathlib import Path

ROOT = Path(u.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/VideoTwinMVP'
OUT.mkdir(parents=True,exist_ok=True)
BASE = '/Game/VideoTwinMVP'
TOOLS = u.AssetToolsHelpers.get_asset_tools()
EDIT = u.MaterialEditingLibrary
LIB = u.EditorAssetLibrary
ACTORS = u.get_editor_subsystem(u.EditorActorSubsystem)

def asset(name, cls, factory):
    path = BASE + '/' + name
    obj = u.load_asset(path)
    return obj if obj else TOOLS.create_asset(name, BASE, cls, factory)

task = u.AssetImportTask()
task.filename = str(ROOT/'Art/VideoTwinMVP/Camera17_Reference.jpg')
task.destination_path = BASE
task.destination_name = 'T_Camera17_Reference'
task.automated = True
task.replace_existing = True
task.save = True
TOOLS.import_asset_tasks([task])
tex = u.load_asset(BASE+'/T_Camera17_Reference')
assert tex
tex.set_editor_property('never_stream',True)
LIB.save_loaded_asset(tex)

depth = asset('RT_ProjectorDepth_Default',u.TextureRenderTarget2D,u.TextureRenderTargetFactoryNew())
depth.set_editor_property('render_target_format',u.TextureRenderTargetFormat.RTF_R32F)
depth.set_editor_property('size_x',1280)
depth.set_editor_property('size_y',720)
LIB.save_loaded_asset(depth)
mat = asset('M_VideoProjection',u.Material,u.MaterialFactoryNew())
EDIT.delete_all_material_expressions(mat)
mat.set_editor_property('material_domain',u.MaterialDomain.MD_POST_PROCESS)
mat.set_editor_property('blendable_location',u.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
custom = EDIT.create_material_expression(mat,u.MaterialExpressionCustom,200,0)
custom.set_editor_property('code',(ROOT/'Tools/VideoTwin/projection.hlsl').read_text(encoding='utf-8'))
custom.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
nodes = []
def node(cls):
    n=EDIT.create_material_expression(mat,cls,-500,len(nodes)*95)
    nodes.append(n)
    return n
inputs={}
inputs['WorldPos']=(node(u.MaterialExpressionWorldPosition),'')
scene=node(u.MaterialExpressionSceneTexture)
scene.set_editor_property('scene_texture_id',u.SceneTextureId.PPI_POST_PROCESS_INPUT0)
inputs['SceneColor']=(scene,'Color')
normal=node(u.MaterialExpressionSceneTexture)
normal.set_editor_property('scene_texture_id',u.SceneTextureId.PPI_WORLD_NORMAL)
inputs['WorldNormal']=(normal,'Color')
inputs['ScreenUV']=(node(u.MaterialExpressionScreenPosition),'ViewportUV')
for name,t in [('VideoTex',tex),('ProjectorDepthTex',depth)]:
    n=node(u.MaterialExpressionTextureObjectParameter)
    n.set_editor_property('parameter_name',name)
    n.set_editor_property('texture',t)
    if name=='ProjectorDepthTex': n.set_editor_property('sampler_type',u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    inputs[name]=(n,'')
for name,default in {
    'ProjectorPosition':(0,0,0,0),'ProjectorForward':(1,0,0,0),'ProjectorRight':(0,1,0,0),
    'ProjectorUp':(0,0,1,0),'ReceiverMin':(-100,-2500,4315,0),'ReceiverMax':(2850,300,4740,0),
    'Lens':(1,16/9,.5,.5),'Distortion':(0,0,0,0),'Options':(1,6,3000,0),'Display':(0,0,1,0)
}.items():
    n=node(u.MaterialExpressionVectorParameter)
    n.set_editor_property('parameter_name',name)
    n.set_editor_property('default_value',u.LinearColor(*default))
    if name in ['Lens','Options','Display','Distortion']:
        append=node(u.MaterialExpressionAppendVector)
        assert EDIT.connect_material_expressions(n,'',append,'A')
        assert EDIT.connect_material_expressions(n,'A',append,'B')
        inputs[name]=(append,'')
    else:
        inputs[name]=(n,'')
ci=[]
for name in inputs:
    i=u.CustomInput()
    i.set_editor_property('input_name',name)
    ci.append(i)
custom.set_editor_property('inputs',ci)
for name,(n,output) in inputs.items():
    assert EDIT.connect_material_expressions(n,output,custom,name),name
assert EDIT.connect_material_property(custom,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
EDIT.recompile_material(mat)
LIB.save_loaded_asset(mat)

source=asset('MS_Camera17_Mock',u.FileMediaSource,u.FileMediaSourceFactoryNew())
source.set_file_path(str(ROOT/'Content/Movies/Camera17_Mock.mp4'))
LIB.save_loaded_asset(source)

levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
original_actors=ACTORS.get_all_level_actors()
camera=next(a for a in original_actors if a.get_actor_label()=='OntoTwin_Camera_17')
# Saving as creates a new persistent map. Subsequent writes target only this map.
assert u.EditorLoadingAndSavingUtils.save_map(world,BASE+'/Maps/Office_VideoTwin_MVP')
assert levels.load_level(BASE+'/Maps/Office_VideoTwin_MVP')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
# Remove shared exterior sublevel references from the new demo only. Keep office lighting.
for level in list(u.EditorLevelUtils.get_levels(world)):
    if 'Office_VideoTwin_MVP' not in level.get_path_name() and 'L_Office_Light' not in level.get_path_name():
        u.EditorLevelUtils.remove_level_from_world(level)
# This standalone mock must not start the existing backend/target-sync workflows.
for a in list(ACTORS.get_all_level_actors()):
    if a.get_class().get_name() in ['TwinSceneManager','CampusInteraction','GrandNeoBayExperience','SkeletalMeshActor','SceneCapture2D']:
        ACTORS.destroy_actor(a)
        continue
    for c in a.get_components_by_class(u.ActorComponent):
        if any(word in c.get_class().get_name() for word in ['BridgeClient','Metaverse','DigitalTwinSync','UEAssetCatalogSync']):
            u.log('VIDEOTWIN_REMOVING_NETWORK_COMPONENT '+c.get_path_name())
            c.destroy_component(a)
component_report=[]
for a in ACTORS.get_all_level_actors():
    for c in a.get_components_by_class(u.StaticMeshComponent):
        mesh=c.get_editor_property('static_mesh')
        if not mesh: continue
        component_report.append(dict(actor=a.get_actor_label(),component=c.get_name(),mesh=mesh.get_name()))
(OUT/'mesh_components.json').write_text(json.dumps(component_report,ensure_ascii=False,indent=2),encoding='utf-8')
# Offline receiving geometry: desks are absent from the saved persistent level.
# Approximate their rows from the existing paired planter cabinets, independently
# editable under VideoTwinMVP/Receivers. These are intentionally marked as proxies.
cube=u.load_asset('/Engine/BasicShapes/Cube')
proxy_mat=asset('M_ProxyReceiver',u.Material,u.MaterialFactoryNew())
EDIT.delete_all_material_expressions(proxy_mat)
color=EDIT.create_material_expression(proxy_mat,u.MaterialExpressionConstant3Vector)
color.set_editor_property('constant',u.LinearColor(.55,.6,.62,1))
EDIT.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
rough=EDIT.create_material_expression(proxy_mat,u.MaterialExpressionConstant)
rough.set_editor_property('r',.8)
EDIT.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
EDIT.recompile_material(proxy_mat)
LIB.save_loaded_asset(proxy_mat)
def receiver(name,center,size):
    a=ACTORS.spawn_actor_from_class(u.StaticMeshActor,u.Vector(*center))
    a.set_actor_label('VT_PROXY_'+name)
    a.set_folder_path('VideoTwinMVP/Receivers_Approximate')
    a.static_mesh_component.set_static_mesh(cube)
    a.static_mesh_component.set_material(0,proxy_mat)
    a.set_actor_scale3d(u.Vector(*(v/100 for v in size)))
    a.tags=['VideoTwinApproximateReceiver']
    return a
for row,x in enumerate([1160,1560,2020,2400]):
    for column,y in enumerate([-1550,-1730,-1910]):
        receiver(f'Row{row+1}_Screen{column+1}',(x,y,4415),(3,178,40))
        for side in [-1,1]:
            receiver(f'Row{row+1}_Desk{column+1}_{side}',(x+side*42,y,4393.5),(80,178,3))
            for end in [-1,1]:
                receiver(f'Row{row+1}_Leg{column+1}_{side}_{end}',(x+side*65,y+end*72,4356),(4,4,72))
for a in ACTORS.get_all_level_actors():
    if a.get_actor_label().startswith('SM_Pothos') and a.get_actor_location().x<2700:
        a.set_actor_hidden_in_game(True)
        a.set_is_temporarily_hidden_in_editor(True)
world.get_world_settings().set_editor_property('default_game_mode',u.VideoTwinDemoGameMode)
camera=next(a for a in ACTORS.get_all_level_actors() if a.get_actor_label()=='OntoTwin_Camera_17')
projector=ACTORS.spawn_actor_from_class(u.VideoTwinProjector,camera.get_actor_location(),camera.get_actor_rotation())
projector.set_actor_label('VideoTwin_Projector_17_MOCK')
projector.set_editor_property('source_camera',camera)
projector.set_editor_property('reference_texture',tex)
projector.set_editor_property('media_source',source)
projector.set_editor_property('projection_material',mat)
# Persist the inventory, including each real authored camera pose; only camera 17 has media.
channels=[]
for a in ACTORS.get_all_level_actors():
    if not a.get_actor_label().startswith('OntoTwin_Camera_'): continue
    c=a.get_component_by_class(u.CameraComponent)
    p,r=c.get_world_location(),c.get_world_rotation()
    cid=a.get_actor_label().split('_')[-1]
    channels.append(dict(camera_id=cid,actor_label=a.get_actor_label(),position_cm=[p.x,p.y,p.z],
        rotation_deg=[r.pitch,r.yaw,r.roll],horizontal_fov=c.field_of_view,aspect=c.aspect_ratio,
        source_asset=source.get_path_name() if cid=='17' else None,
        status='mock' if cid=='17' else 'not_configured'))
channels.sort(key=lambda d:int(d['camera_id']))
assert len(channels)==14
(ROOT/'Tools/VideoTwin/channels.json').write_text(json.dumps(channels,ensure_ascii=False,indent=2),encoding='utf-8')
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(camera.get_actor_location(),camera.get_actor_rotation())
assert u.EditorLoadingAndSavingUtils.save_map(world,BASE+'/Maps/Office_VideoTwin_MVP')
(OUT/'asset_build.json').write_text(json.dumps(dict(map=world.get_path_name(),channels=len(channels),source_camera=camera.get_path_name(),projector=projector.get_path_name()),indent=2),encoding='utf-8')
u.log('VIDEOTWIN_BUILD_COMPLETE')
