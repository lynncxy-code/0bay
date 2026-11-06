"""Run in Unreal's Python console to receive telemetry without pressing Play."""
import unreal
if '_go2_live_editor_handle' in globals():
    unreal.unregister_slate_post_tick_callback(_go2_live_editor_handle)
def _go2_live_editor_tick(dt):
    # During Play, native Actor.Tick owns polling; do not poll editor duplicates too.
    if unreal.EditorLevelLibrary.get_game_world() is not None:return
    subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cls=unreal.Go2TelemetryActor
    for actor in subsystem.get_all_level_actors():
        if isinstance(actor,cls):actor.poll_telemetry(dt)
_go2_live_editor_handle=unreal.register_slate_post_tick_callback(_go2_live_editor_tick)
unreal.log('Go2 editor telemetry polling started. Only local UDP is read. Open L_Go2_Live.')
