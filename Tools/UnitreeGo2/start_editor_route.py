"""Run from the Unreal Python console to preview offline route actors without entering Play."""
import unreal
try:
    unreal.unregister_slate_post_tick_callback(_go2_route_preview_handle)
except Exception:
    pass
_go2_route_actor_subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def _go2_route_preview_tick(dt):
    if unreal.EditorLevelLibrary.get_game_world() is not None:return
    for actor in _go2_route_actor_subsystem.get_all_level_actors():
        if isinstance(actor,unreal.Go2RouteActor):actor.advance_simulation(dt)
_go2_route_preview_handle=unreal.register_slate_post_tick_callback(_go2_route_preview_tick)
unreal.log('Offline route preview ready. Select BP_Go2Route and click Start Route. Reset Route restores the initial pose.')
