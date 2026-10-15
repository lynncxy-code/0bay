"""Find planter cabinets (PDF_24) and potted plants (Pothos) in every office map, with the streaming sublevels of
Office_Demo, to see which copy the running scene shows (2026-10-16). Headless; prints VTPL lines."""
import unreal as u

MAPS = ['/Game/OfficeShell/Maps/Office_Demo', '/Game/OfficeShell/Maps/L_GrandNeoBay_Building',
        '/Game/OfficeShell/Maps/L_Office_Light', '/Game/OfficeShell/Maps/cxy']
for m in MAPS:
    world = u.EditorLoadingAndSavingUtils.load_map(m)
    if not world:
        u.log('VTPL %s: cannot load' % m)
        continue
    try:
        subs = [s.get_editor_property('world_asset').get_path_name() for s in world.get_streaming_levels()]
    except Exception as e:
        subs = ['?%s' % e]
    actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
    hits = 0
    for a in actors:
        meshes = [c.get_editor_property('static_mesh') for c in a.get_components_by_class(u.StaticMeshComponent)]
        names = ','.join(sorted({x.get_name() for x in meshes if x}))
        if 'Pothos' in names or 'PDF_24' in names:
            o, e = a.get_actor_bounds(False)
            hits += 1
            if hits <= 40:
                u.log('VTPL %s | %s | %s | x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f | level %s' % (
                    m.split('/')[-1], a.get_actor_label(), names[:40], o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y,
                    o.z - e.z, o.z + e.z, a.get_level().get_outer().get_name()))
    u.log('VTPL %s: %d actors, %d planter/plant, streaming sublevels: %s' % (m, len(actors), hits, subs))
