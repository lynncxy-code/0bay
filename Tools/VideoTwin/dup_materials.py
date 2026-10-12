import unreal

# Post-process material instances that share a parent are blended into ONE pass (their parameters are lerped),
# so five projectors using the same M_VideoProjection collapse to a single camera. Each camera therefore
# needs its own parent material. The copies are identical; only the asset identity differs.
SRC = '/Game/VideoTwinMVP/M_VideoProjection'
made, kept = [], []
for cam in ['18', '19', '20', '21', '22', '23', '24', '25', '26', '27', '28', '29', '30']:
    dst = '%s_%s' % (SRC, cam)
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        kept.append(dst)
        continue
    asset = unreal.EditorAssetLibrary.duplicate_asset(SRC, dst)
    if asset is None:
        unreal.log_error('VTDUP failed to duplicate to ' + dst)
        continue
    unreal.EditorAssetLibrary.save_asset(dst, only_if_is_dirty=False)
    made.append(dst)
unreal.log('VTDUP made=%s kept=%s' % (made, kept))
