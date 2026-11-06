"""Build the world-space post-process material used by the manual F6 floor scan."""
import unreal as u

path = '/Game/Robots/UnitreeGo2/Scanning'
lib = u.EditorAssetLibrary
ml = u.MaterialEditingLibrary
master = '/Game/ScanGridEffect/Materials/PP_Scan'
source = '/Game/ScanGridEffect/Materials/PPI_Scan01'
dest = path + '/M_Go2NativeFloorScan'
mat = lib.load_asset(dest) if lib.does_asset_exist(dest) else lib.duplicate_asset(master, dest)
mat.set_editor_property('blendable_location', u.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)
prop = u.MaterialProperty.MP_EMISSIVE_COLOR
native = ml.get_material_property_input_node(mat, prop)
if isinstance(native, u.MaterialExpressionCustom) and native.get_editor_property('description') == 'Go2 floor bounds':
    native = ml.get_inputs_for_material_expression(mat, native)[0]

def node(cls, x, y):
    return ml.create_material_expression(mat, cls, x, y)

scene = node(u.MaterialExpressionSceneTexture, -800, 0)
scene.set_editor_property('scene_texture_id', u.SceneTextureId.PPI_POST_PROCESS_INPUT0)
position = node(u.MaterialExpressionWorldPosition, -800, 150)
inputs = [('NativeColor', native, ''), ('Scene', scene, 'Color'), ('P', position, '')]
for i, key in enumerate(['FloorMin', 'FloorMax', 'Origin']):
    param = node(u.MaterialExpressionVectorParameter, -800, 300 + i * 100)
    param.set_editor_property('parameter_name', key)
    inputs.append((key, param, ''))
radius = node(u.MaterialExpressionScalarParameter, -800, 620)
radius.set_editor_property('parameter_name', 'Radius')
inputs.append(('Radius', radius, ''))
custom = node(u.MaterialExpressionCustom, -300, 100)
custom.set_editor_property('description', 'Go2 floor bounds')
custom.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT3)
pins = []
for key, expr, output in inputs:
    pin = u.CustomInput()
    pin.set_editor_property('input_name', key)
    pins.append(pin)
custom.set_editor_property('inputs', pins)
custom.set_editor_property('code', """
float inside = all(P >= FloorMin.xyz) && all(P <= FloorMax.xyz);
float revealed = saturate((Radius - distance(P.xy, Origin.xy)) / 12.0);
return lerp(Scene.rgb, NativeColor.rgb, inside * revealed);
""")
for key, expr, output in inputs:
    assert ml.connect_material_expressions(expr, output, custom, key), key
assert ml.connect_material_property(custom, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
ml.recompile_material(mat)
assert lib.save_loaded_asset(mat)
instance_path = path + '/MI_Go2NativeFloorScan'
instance = lib.load_asset(instance_path) if lib.does_asset_exist(instance_path) else lib.duplicate_asset(source, instance_path)
ml.set_material_instance_parent(instance, mat)
ml.update_material_instance(instance)
assert lib.save_loaded_asset(instance)
print('GO2_NATIVE_FLOOR_MATERIAL_SAVED')

