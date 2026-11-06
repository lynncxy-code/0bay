# Source-only update: deferred scene and asset upload

This main-branch update contains source code, plugin descriptors, tools, configuration, and documentation. Updated Unreal maps, Unreal assets, Blender sources, and other binary resources are deferred.

The complete update remains in the local `pending/full-update-20261106` branch at commit `000a4810815a25ad9a1df560e40fbc78acad2a18` in `D:/0bay/OfficeShell` (also preserved by `merge/art-assets-20261101`). It has not been published to main.

`Config/DefaultEngine.ini` retains main's existing `Office_Demo` startup/default map until the new art-merged map is uploaded. New asset-dependent features and scene installation tools require the deferred assets before runtime validation.

Validation: the complete local project compiled successfully as OfficeHostEditor Win64 Development on Unreal Engine 5.6. The four VideoTwin monitor gateway unit tests also pass in this source-only checkout. The source-only checkout has not been validated as a complete runnable scene.

When publishing the remaining assets, merge the updated main into the complete local branch, reconcile this note, verify the complete project, and push only after the required LFS objects have uploaded successfully.
