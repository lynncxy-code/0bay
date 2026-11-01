"""Generate the real procedural belt material in an isolated UE host only."""
import unreal

root = '/OntoTwinIndustrialBehavior/Materials'
name = 'M_BeltScroll'
material = unreal.load_asset(root + '/' + name)
if not material:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, root, unreal.Material, unreal.MaterialFactoryNew())
lib = unreal.MaterialEditingLibrary
lib.delete_all_material_expressions(material)
material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
uv = lib.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -900, 0)
mask = lib.create_material_expression(material, unreal.MaterialExpressionComponentMask, -750, 0)
mask.set_editor_property('r', True)
mask.set_editor_property('g', False)
mask.set_editor_property('b', False)
mask.set_editor_property('a', False)
lib.connect_material_expressions(uv, '', mask, 'Input')
multiply = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -600, 0)
multiply.set_editor_property('const_b', 8)
lib.connect_material_expressions(mask, '', multiply, 'A')
offset = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, 150)
offset.set_editor_property('parameter_name', 'Offset')
offset.set_editor_property('default_value', 0)
add = lib.create_material_expression(material, unreal.MaterialExpressionAdd, -400, 0)
lib.connect_material_expressions(multiply, '', add, 'A')
lib.connect_material_expressions(offset, '', add, 'B')
frac = lib.create_material_expression(material, unreal.MaterialExpressionFrac, -250, 0)
lib.connect_material_expressions(add, '', frac, 'Input')
blend = lib.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -100, 0)
blend.set_editor_property('const_a', .03)
blend.set_editor_property('const_b', .35)
lib.connect_material_expressions(frac, '', blend, 'Alpha')
lib.connect_material_property(blend, '', unreal.MaterialProperty.MP_BASE_COLOR)
lib.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material):
    raise RuntimeError('Belt material save failed')
unreal.log('OT_BELT_ASSET_SAVED ' + root + '/' + name)
