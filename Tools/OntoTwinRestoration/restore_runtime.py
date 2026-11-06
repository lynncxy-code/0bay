import unreal,json,os
s=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
e=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
a=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
archive='/Game/OfficeShell/StaticEditing/Archive/L_OntoTwin_ControlBackup_20261027'
runtime='/Game/OfficeShell/Maps/L_OntoTwin_Runtime'
main='/Game/OfficeShell/Maps/Office_Demo_ArtMerged'
assert s.load_level(archive)
actors=a.get_all_level_actors()
print('ARCHIVE_ACTORS',[(x.get_name(),x.get_class().get_name()) for x in actors])
ms=[x for x in actors if isinstance(x,unreal.TwinSceneManager)]
assert len(ms)==1
m=ms[0]
m.set_editor_property('use_level_geometry',True)
m.get_editor_property('interaction_manager').set_editor_property('runtime_route_level_alias','/Game/OfficeShell/Maps/Office_Demo')
m.set_editor_property('enable_runtime_editor',True)
m.set_editor_property('enable_realtime_web_socket',False)
c=m.get_editor_property('asset_catalog_sync')
c.set_editor_property('sync_on_begin_play',False)
c.set_editor_property('enable_on_demand_catalog',False)
assert unreal.EditorLoadingAndSavingUtils.save_map(e.get_editor_world(),runtime)
assert s.load_level(main)
w=e.get_editor_world()
assert unreal.EditorLevelUtils.add_level_to_world(w,runtime,unreal.LevelStreamingAlwaysLoaded)
assert unreal.EditorLoadingAndSavingUtils.save_map(w,main)
assert s.load_level(main)
actors=a.get_all_level_actors()
ms=[x for x in actors if isinstance(x,unreal.TwinSceneManager)]
assert len(ms)==1 and ms[0].get_editor_property('use_level_geometry')
assert not any(isinstance(x,unreal.TwinInstance) for x in actors)
print('RESTORE_VALIDATED',len(actors),'actors',ms[0].get_editor_property('ue_project_id'))

import unreal
m=unreal.load_asset('/Game/arts/A03/SM_BGS_zhengting')
b=m.get_editor_property('body_setup')
print('COLLISION_BEFORE',b.get_editor_property('collision_trace_flag'))
b.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
assert unreal.EditorAssetLibrary.save_loaded_asset(m,False)
print('COLLISION_SAVED')
