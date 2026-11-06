"""Create a background-only sky from Example_Map_11; preserve authored cameras/lights."""
import json
import shutil
from datetime import datetime
from pathlib import Path
import unreal as u

root = Path('D:/0bay/OfficeShell')
out = root / 'Saved/SecuritySky' / datetime.now().strftime('%Y%m%d-%H%M%S')
out.mkdir(parents=True, exist_ok=True)
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
example = '/Game/HDRI_Futuristic_Technology/Example_Levels/Example_Map_11'
assert levels.load_level(example)
source = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == 'HDRIBackdrop')
cube = source.get_editor_property('Cubemap')
intensity = source.get_editor_property('Intensity')
geometry = source.get_component_by_class(u.StaticMeshComponent)
sky = geometry.get_material(0)
angle = sky.get_scalar_parameter_value('HDRI_Angle') if isinstance(sky, u.MaterialInstanceDynamic) else 0.0
material_path = '/Game/OfficeShell/Materials/MI_SecurityOverview_Example11'
material = u.load_asset(material_path) if u.EditorAssetLibrary.does_asset_exist(material_path) else u.AssetToolsHelpers.get_asset_tools().create_asset(
    'MI_SecurityOverview_Example11', '/Game/OfficeShell/Materials', u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
quiet_path = '/Game/OfficeShell/Materials/M_SecurityQuietHDRI'
parent_path = quiet_path if u.EditorAssetLibrary.does_asset_exist(quiet_path) else '/HDRIBackdrop/Materials/MI_HDRI_Sky'
u.MaterialEditingLibrary.set_material_instance_parent(material, u.load_asset(parent_path))
u.MaterialEditingLibrary.set_material_instance_texture_parameter_value(material, 'HDRI_Map', cube)
for key, value in [('Intensity', intensity), ('HDRI_Angle', angle), ('UseCameraPosition', 1.0)]:
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(material, key, value)
u.MaterialEditingLibrary.update_material_instance(material)
assert u.EditorAssetLibrary.save_loaded_asset(material)

main = '/Game/OfficeShell/Maps/Office_Demo_ArtMerged'
assert levels.load_level(main)
all_actors = actors.get_all_level_actors()
camera = next(a for a in all_actors if a.get_actor_label() == 'CameraActor_All')
camera_transform = camera.get_actor_transform().export_text()
backdrops = [a for a in all_actors if 'OntoTwinSecurityOverviewBackground' in [str(t) for t in a.tags]]
assert len(backdrops) <= 1
changed_packages = {main}
defaults = [a for a in all_actors if a.get_actor_label() in ['Atmospheric Skybox', 'Office Height Fog', 'SkyAtmosphere', 'VolumetricCloud']]
assert len(defaults) == 4
for a in defaults:
    changed_packages.add(a.get_path_name().split('.')[0])
for package in changed_packages:
    filename = root / 'Content' / (package.removeprefix('/Game/') + '.umap')
    shutil.copy2(filename, out / filename.name)
background = backdrops[0] if backdrops else actors.spawn_actor_from_class(u.StaticMeshActor, camera.get_actor_location())
assert background.get_path_name().split('.')[0] == main
background.set_actor_label('Security Overview - Example_Map_11')
background.set_editor_property('tags', ['OntoTwinSecurityOverviewBackground'])
component = background.static_mesh_component
component.set_mobility(u.ComponentMobility.MOVABLE)
component.set_static_mesh(u.load_asset('/Engine/EngineSky/SM_SkySphere'))
component.set_material(0, material)
component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
component.set_cast_shadow(False)
background.set_actor_location(camera.get_actor_location(), False, False)
background.set_actor_scale3d(u.Vector(1000, 1000, 1000))
background.set_actor_hidden_in_game(True)
background.set_is_temporarily_hidden_in_editor(True)
for a in defaults:
    a.set_editor_property('tags', list(dict.fromkeys([str(t) for t in a.tags] + ['OntoTwinDefaultBackground'])))
assert camera.get_actor_transform().export_text() == camera_transform
for package in changed_packages:
    assert u.EditorLoadingAndSavingUtils.save_map(u.load_asset(package), package)
record = dict(source=example, cubemap=cube.get_path_name(), intensity=intensity, angle=angle,
              material=material_path, background=background.get_path_name(),
              defaults=[a.get_path_name() for a in defaults], camera_transform=camera_transform)
(out / 'configuration.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
print('SECURITY_SKY_CONFIGURED', json.dumps(record))
