"""Run with UnrealEditor-Cmd -ExecutePythonScript=... in an isolated host."""
import unreal

root = '/OntoTwinIndustrialBehavior/Materials'
tools = unreal.AssetToolsHelpers.get_asset_tools()
for name, translucent in [('M_Indicator', False), ('M_StatusOverlay', True)]:
    path = root + '/' + name
    material = unreal.load_asset(path)
    if not material:
        material = tools.create_asset(name, root, unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError('Cannot create ' + path)
    lib = unreal.MaterialEditingLibrary
    lib.delete_all_material_expressions(material)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    color = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -250, 0)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(1.0, .55, .02, 1.0))
    lib.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if translucent:
        material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
        opacity = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -250, 150)
        opacity.set_editor_property('parameter_name', 'Opacity')
        opacity.set_editor_property('default_value', .5)
        lib.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    lib.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material):
        raise RuntimeError('Cannot save ' + path)
    unreal.log('OT_PRESENTATION_ASSET_SAVED ' + path)
