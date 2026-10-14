"""Rebuild the node graph of the projection material and of every per-camera copy, in place.

Same graph build_assets.py makes, plus the CameraPos input projection.hlsl needs since 2026-10-15
(to turn its geometric normal towards the viewer before rejecting surfaces that face away from the projector). Only these materials are
touched: no map, texture or other asset is created or saved.

Run with the editor closed:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<this file> -unattended -nosplash
or from an open editor: Tools > Execute Python Script.
"""
import unreal as u
from pathlib import Path

ROOT = Path(u.Paths.project_dir()).resolve()
BASE = '/Game/VideoTwinMVP/M_VideoProjection'
EDIT = u.MaterialEditingLibrary
LIB = u.EditorAssetLibrary
CODE = (ROOT / 'Tools/VideoTwin/projection.hlsl').read_text(encoding='utf-8')
tex = u.load_asset('/Game/VideoTwinMVP/T_Camera17_Reference')
depth = u.load_asset('/Game/VideoTwinMVP/RT_ProjectorDepth_Default')
assert tex and depth, 'reference texture or default depth target missing'


def rebuild(path):
    mat = u.load_asset(path)
    EDIT.delete_all_material_expressions(mat)
    mat.set_editor_property('material_domain', u.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property('blendable_location', u.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    custom = EDIT.create_material_expression(mat, u.MaterialExpressionCustom, 200, 0)
    custom.set_editor_property('code', CODE)
    custom.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT3)
    nodes = []

    def node(cls):
        n = EDIT.create_material_expression(mat, cls, -500, len(nodes) * 95)
        nodes.append(n)
        return n

    inputs = {}
    inputs['WorldPos'] = (node(u.MaterialExpressionWorldPosition), '')
    scene = node(u.MaterialExpressionSceneTexture)
    scene.set_editor_property('scene_texture_id', u.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    inputs['SceneColor'] = (scene, 'Color')
    inputs['CameraPos'] = (node(u.MaterialExpressionCameraPositionWS), '')
    inputs['ScreenUV'] = (node(u.MaterialExpressionScreenPosition), 'ViewportUV')
    for name, t in [('VideoTex', tex), ('ProjectorDepthTex', depth)]:
        n = node(u.MaterialExpressionTextureObjectParameter)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('texture', t)
        if name == 'ProjectorDepthTex':
            n.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        inputs[name] = (n, '')
    for name, default in {
        'ProjectorPosition': (0, 0, 0, 0), 'ProjectorForward': (1, 0, 0, 0), 'ProjectorRight': (0, 1, 0, 0),
        'ProjectorUp': (0, 0, 1, 0), 'ReceiverMin': (-100, -2500, 4315, 0), 'ReceiverMax': (2850, 300, 4740, 0),
        'Lens': (1, 16 / 9, .5, .5), 'Distortion': (0, 0, 0, 0), 'Options': (1, 6, 3000, 0), 'Display': (0, 0, 1, 0)
    }.items():
        n = node(u.MaterialExpressionVectorParameter)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('default_value', u.LinearColor(*default))
        if name in ['Lens', 'Options', 'Display', 'Distortion']:
            append = node(u.MaterialExpressionAppendVector)
            assert EDIT.connect_material_expressions(n, '', append, 'A')
            assert EDIT.connect_material_expressions(n, 'A', append, 'B')
            inputs[name] = (append, '')
        else:
            inputs[name] = (n, '')
    ci = []
    for name in inputs:
        i = u.CustomInput()
        i.set_editor_property('input_name', name)
        ci.append(i)
    custom.set_editor_property('inputs', ci)
    for name, (n, output) in inputs.items():
        assert EDIT.connect_material_expressions(n, output, custom, name), name
    assert EDIT.connect_material_property(custom, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(mat)
    return LIB.save_loaded_asset(mat)


done, failed = [], []
for path in [BASE] + ['%s_%d' % (BASE, cam) for cam in range(17, 31)]:
    if not LIB.does_asset_exist(path):
        continue
    try:
        (done if rebuild(path) else failed).append(path.rsplit('/', 1)[1])
    except Exception as e:  # keep going so one bad asset does not hide the state of the others
        failed.append('%s (%s)' % (path.rsplit('/', 1)[1], e))
u.log('VTREBUILD done=%d %s' % (len(done), done))
u.log('VTREBUILD failed=%d %s' % (len(failed), failed))
