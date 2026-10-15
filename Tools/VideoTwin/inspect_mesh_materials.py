"""Print the materials of a static mesh and their blend modes (headless). Used 2026-10-16 to find out why the
potted plants could not be kept out of the video by a world box.
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<this file> -unattended -nosplash
"""
import unreal as u

for name in ['SM_Pothos_Potted']:
    paths = [a.package_name for a in u.AssetRegistryHelpers.get_asset_registry().get_assets_by_class(u.TopLevelAssetPath('/Script/Engine', 'StaticMesh'))
             if str(a.asset_name) == name]
    for p in paths:
        mesh = u.load_asset(str(p))
        for i, sm in enumerate(mesh.get_editor_property('static_materials')):
            mi = sm.get_editor_property('material_interface')
            base = mi.get_base_material() if mi else None
            u.log('VTMAT %s slot %d %s base=%s blend=%s twosided=%s' % (p, i, mi.get_path_name() if mi else None,
                  base.get_path_name() if base else None, base.get_editor_property('blend_mode') if base else None,
                  base.get_editor_property('two_sided') if base else None))
