import os
import unreal

# Push Tools/VideoTwin/projection.hlsl into the Custom node of the projection material and of every
# per-camera copy (they must stay identical; see dup_materials.py for why there is one per camera).
BASE = '/Game/VideoTwinMVP/M_VideoProjection'
hlsl = os.path.join(unreal.Paths.project_dir(), 'Tools', 'VideoTwin', 'projection.hlsl')
with open(hlsl, 'r', encoding='utf-8') as f:
    code = f.read()

done, failed = [], []
for path in [BASE] + ['%s_%d' % (BASE, cam) for cam in range(17, 31)]:
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        continue
    mat = unreal.EditorAssetLibrary.load_asset(path)
    node = unreal.MaterialEditingLibrary.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if not isinstance(node, unreal.MaterialExpressionCustom):
        failed.append('%s (emissive input is %s)' % (path, type(node).__name__))
        continue
    node.set_editor_property('code', code)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    if unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
        done.append(path.rsplit('/', 1)[1])
    else:
        failed.append(path + ' (save failed)')
unreal.log('VTCODE updated=%d %s' % (len(done), done))
unreal.log('VTCODE failed=%d %s' % (len(failed), failed))
