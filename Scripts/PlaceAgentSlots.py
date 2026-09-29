"""Restore lobby letters and TV to pre-agent layout. Do not move agents.

Editor Output Log:
    py F:/Office_VR/Scripts/PlaceAgentSlots.py
"""

import unreal

GOLD_MESH = "CompanyLogo_GoldCurved"
SAMPLE_MESH = "CompanyLogo_Sample"
SPAWNED_LABEL = "中航金网"
GOLD_LOC = unreal.Vector(3620.0, -1608.0, 4520.0)
SAMPLE_LOC = unreal.Vector(5711.0, -590.0, 4510.0)

# Positions captured before any logo/TV edits (08:48 dump / 08:50 pre-move).
TV_RESTORE = (
    ("StaticMeshActor_13", "newline_lyrapro_interactive_display", unreal.Vector(3380.0, -1410.0, 4320.0)),
    ("StaticMeshActor_57", "newline_lyrapro_interactive_display3", unreal.Vector(2670.0, -1830.0, 4320.0)),
    ("StaticMeshActor_882", "newline_lyrapro_interactive_display5", unreal.Vector(4410.0, -1980.0, 4320.0)),
    ("StaticMeshActor_883", "newline_lyrapro_interactive_display6", unreal.Vector(4670.0, -1980.0, 4320.0)),
)


def _level_actors():
    return list(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())


def _mesh_name(comp):
    mesh = comp.static_mesh if comp else None
    return mesh.get_name() if mesh else ""


def _dist(a, b):
    dx = a.x - b.x
    dy = a.y - b.y
    dz = a.z - b.z
    return (dx * dx + dy * dy + dz * dz) ** 0.5


def _set_yaw(actor, yaw):
    rot = actor.get_actor_rotation()
    rot.pitch = 0.0
    rot.yaw = yaw
    rot.roll = 0.0
    actor.set_actor_rotation(rot, False)


def _destroy_overlay():
    removed = 0
    for actor in list(_level_actors()):
        label = ""
        try:
            label = actor.get_actor_label()
        except Exception:
            pass
        if label == SPAWNED_LABEL:
            unreal.log("OfficeVR: destroy overlay {}".format(actor.get_name()))
            actor.destroy_actor()
            removed += 1
            continue
        try:
            comps = actor.get_components_by_class(unreal.StaticMeshComponent)
        except Exception:
            continue
        if not any(SAMPLE_MESH in _mesh_name(comp) for comp in comps):
            continue
        loc = actor.get_actor_location()
        if _dist(loc, SAMPLE_LOC) < 80.0:
            continue
        unreal.log("OfficeVR: destroy extra Sample {} at {}".format(actor.get_name(), loc))
        actor.destroy_actor()
        removed += 1
    unreal.log("OfficeVR: overlay removed={}".format(removed))


def _restore_gold():
    for actor in _level_actors():
        try:
            comps = actor.get_components_by_class(unreal.StaticMeshComponent)
        except Exception:
            continue
        if not any(GOLD_MESH in _mesh_name(comp) for comp in comps):
            continue
        try:
            for comp in comps:
                comp.set_visibility(True)
                comp.set_hidden_in_game(False)
            actor.set_actor_hidden_in_game(False)
        except Exception:
            pass
        actor.set_actor_location(GOLD_LOC, False, False)
        _set_yaw(actor, -15.0)
        unreal.log("OfficeVR: gold letters -> {} rot={}".format(
            actor.get_actor_location(), actor.get_actor_rotation()))
        return
    unreal.log_warning("OfficeVR: GoldCurved not found")


def _restore_sample():
    for actor in _level_actors():
        try:
            comps = actor.get_components_by_class(unreal.StaticMeshComponent)
        except Exception:
            continue
        if not any(SAMPLE_MESH in _mesh_name(comp) for comp in comps):
            continue
        actor.set_actor_location(SAMPLE_LOC, False, False)
        unreal.log("OfficeVR: Sample kept at {}".format(actor.get_actor_location()))
        return


def _restore_tvs():
    actors = _level_actors()
    restored = 0
    for actor_name, label, location in TV_RESTORE:
        match = None
        for actor in actors:
            if actor.get_name() == actor_name:
                match = actor
                break
            try:
                if actor.get_actor_label() == label:
                    match = actor
                    break
            except Exception:
                pass
        if not match:
            unreal.log_warning("OfficeVR: TV {} missing".format(label))
            continue
        match.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
        match.set_actor_location(location, False, False)
        restored += 1
        unreal.log("OfficeVR: TV '{}' -> {}".format(label, match.get_actor_location()))
    unreal.log("OfficeVR: TVs restored={}".format(restored))


def _log_agents_only(world):
    slot_cls = unreal.load_class(None, "/Script/OfficeHost.OfficeAgentSlot")
    if not slot_cls:
        return
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, slot_cls):
        unreal.log("OfficeVR: agent untouched '{}' at {}".format(
            actor.get_actor_label(), actor.get_actor_location()))


def main():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    _destroy_overlay()
    _restore_gold()
    _restore_sample()
    _restore_tvs()
    _log_agents_only(world)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log("OfficeVR: letters+TV restored to pre-agent layout; agents not moved")


if __name__ == "__main__":
    main()
