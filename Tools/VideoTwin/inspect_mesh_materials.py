"""Print bounds (and materials) of the furniture meshes used near the planters (headless, VTMAT lines)."""
import unreal as u

for path in ['/Game/Art/Furniture/F11/SM_F11', '/Game/Art/Furniture/F14/SM_F14', '/Game/Art/Furniture/SM_Pothos_Potted']:
    mesh = u.load_asset(path)
    if not mesh:
        u.log('VTMAT %s missing' % path)
        continue
    b = mesh.get_bounds()
    o, e = b.origin, b.box_extent
    u.log('VTMAT %s bounds x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f' % (path, o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y, o.z - e.z, o.z + e.z))
