import unreal as u,json
from pathlib import Path
out=Path(u.Paths.project_dir())/'Saved/Go2Scan'
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
editor=u.get_editor_subsystem(u.UnrealEditorSubsystem)
world=editor.get_editor_world()
assert world.get_path_name().startswith('/Game/OfficeShell/Maps/Office_Demo_ArtMerged.')
dogs=[a for a in actors.get_all_level_actors() if isinstance(a,u.TempoGo2Course)]
assert len(dogs)==1, 'Expected exactly one Tempo dog'
dog=dogs[0]
lib=u.EditorAssetLibrary;matlib=u.MaterialEditingLibrary
folder='/Game/Robots/UnitreeGo2/Scanning'
lib.make_directory(folder)
source='/Game/ScanGridEffect/Materials/PPI_Scan01';target=folder+'/MI_Go2_ScanGrid'
scan=u.load_asset(target) if lib.does_asset_exist(target) else lib.duplicate_asset(source,target)
for name,value in {'Base-Desaturation':0,'Distortion-InnerAmount':0,'Distortion-OuterAmount':0,'Grid-Thickness':.35,'Dots-Thickness':.008,'Visibility-OuterRadius':1,'Visibility-InnerRadius':1}.items():
 matlib.set_material_instance_scalar_parameter_value(scan,name,value)
for name,value in {'Base-Color RGBA':u.LinearColor(1,1,1,1),'Grid RGBA':u.LinearColor(.1,.8,1.2,.12),'Overlay-Color RGBA':u.LinearColor(.01,.025,.04,.12),'Dots-Color RGBA':u.LinearColor(.1,1,1.5,.08)}.items():
 matlib.set_material_instance_vector_parameter_value(scan,name,value)
for name in ['Voxelize-Use?','Character Outline-Use?','Flash-Use?','Overlay-Use?','Facets-Use?','Fresnel-Use?']:
 matlib.set_material_instance_static_switch_parameter_value(scan,name,False)
matlib.update_material_instance(scan);lib.save_loaded_asset(scan)
source='/Game/ScanningSystem/Materials/TranslucentMaterials/MI_TranslucentOverlay';target=folder+'/MI_Go2_DeviceFound'
highlight=u.load_asset(target) if lib.does_asset_exist(target) else lib.duplicate_asset(source,target)
matlib.set_material_instance_vector_parameter_value(highlight,'Colour',u.LinearColor(3,27,7.5,1))
matlib.set_material_instance_scalar_parameter_value(highlight,'Exponent',1.35)
matlib.update_material_instance(highlight);lib.save_loaded_asset(highlight)
assert levels.set_current_level_by_name('L_Go2_TempoOffice')
scanner=next((a for a in actors.get_all_level_actors() if isinstance(a,u.Go2DeviceScanner)),None)
if scanner is None:scanner=actors.spawn_actor_from_class(u.Go2DeviceScanner,dog.get_actor_location())
scanner.set_actor_label('Go2_Scan_Devices');scanner.set_folder_path('Robots/Go2')
scanner.set_editor_property('robot',dog)
scanner.set_editor_property('scan_material',scan);scanner.set_editor_property('highlight_material',highlight)
scanner.set_editor_property('use_demo_camera',True)
scanner.set_editor_property('auto_scan',False)
scanner.set_editor_property('scan_intensity',.18)
scanner.set_editor_property('scan_radius',850.0)
scanner.set_editor_property('scan_duration',4.0)
scanner.set_editor_property('scan_interval',9.0)
scanner.set_editor_property('highlight_duration',8.0)
assert levels.save_current_level()
actors.set_selected_level_actors([scanner])
pos=dog.get_actor_location();view=pos+u.Vector(370,450,220)
editor.set_level_viewport_camera_info(view,u.MathLibrary.find_look_at_rotation(view,pos+u.Vector(0,0,90)))
(out/'installed.json').write_text(json.dumps({'scanner':scanner.get_path_name(),'robot':dog.get_path_name(),'radius_cm':850,'scan_duration':4,'scan_interval':9,'highlight_seconds':8,'materials':[scan.get_path_name(),highlight.get_path_name()]},indent=2))
print('GO2_SCAN_INSTALLED')


