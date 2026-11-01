import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import ontotwin_delivery as d


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / "project" / "Demo.uproject"
        self.plugin = self.project.parent / "Plugins" / "OntoTwinSync"
        self.make_plugin(self.plugin)
        self.project.write_text(json.dumps({"EngineAssociation": "5.6"}), encoding="utf-8")
        self.context = {"project": str(self.project), "plugin": str(self.plugin), "loaded_hash": d.source_hash(self.plugin)}

    @staticmethod
    def make_plugin(root):
        (root / "Source").mkdir(parents=True)
        (root / "Source" / "test.cpp").write_text("original", encoding="utf-8")
        (root / "OntoTwinSync.uplugin").write_text('{"VersionName":"4.4.0"}', encoding="utf-8")

    def test_loaded_identity_not_descriptor(self):
        self.assertIn("一致", d.version_report(self.context)["runtime_status"])
        self.assertNotEqual("未知", d.version_report(self.context)["current"]["last_updated"])
        (self.plugin / "Source" / "test.cpp").write_text("changed", encoding="utf-8")
        self.assertIn("需编译", d.version_report(self.context)["runtime_status"])

    def test_binary_does_not_change_source_identity(self):
        before = d.source_hash(self.plugin)
        (self.plugin / "Binaries").mkdir()
        (self.plugin / "Binaries" / "test.dll").write_bytes(b"binary")
        self.assertEqual(before, d.source_hash(self.plugin))

    def test_export_standalone_manifest_and_original_unchanged(self):
        dest = self.root / "delivery"
        before = d.source_hash(self.plugin)
        manifest = d.export_project(self.context, dest, "", "", "", source_only=True)
        self.assertFalse((dest / "EXPORT_INCOMPLETE.txt").exists())
        self.assertEqual(before, d.source_hash(dest / "Plugins" / "OntoTwinSync"))
        self.assertEqual(before, d.source_hash(self.plugin))
        self.assertTrue(manifest["requires_rebuild"])
        for record in manifest["files"]:
            self.assertEqual(record["sha256"], d.digest(dest / record["path"]))

    def test_refuse_existing_or_nested_output(self):
        for dest in (self.project.parent, self.project.parent / "export", self.root):
            with self.assertRaises(ValueError):
                d.export_project(self.context, dest, "", "", "", source_only=True)

    def test_dataset_failure_does_not_create_delivery(self):
        dest = self.root / "delivery"
        with patch.object(d, "fetch_package", side_effect=ValueError("offline")):
            with self.assertRaisesRegex(ValueError, "offline"):
                d.export_project(self.context, dest, "localhost", "ds", "ue")
        self.assertFalse(dest.exists())

    def test_dataset_binding_mismatch(self):
        with patch.object(d, "datasets", return_value=[{"id": "ds", "bound_ue_project_id": "other"}]):
            with self.assertRaisesRegex(ValueError, "未绑定"):
                d.fetch_package("http://localhost", "ds", "ue")

    def test_full_export_includes_package(self):
        data = b"fixture-package"
        dest = self.root / "delivery"
        with patch.object(d, "fetch_package", return_value=data):
            result = d.export_project(self.context, dest, "", "ds", "ue")
        self.assertEqual(data, (dest / "OntoTwinDelivery/project.otdataset").read_bytes())
        self.assertEqual(result["dataset_sha256"], d.digest(dest / "OntoTwinDelivery/project.otdataset"))

    def test_fetch_full_package_verifies_attachments_and_dataset(self):
        payload, asset = b'{"id":"ds"}', b"floor-plan"
        manifest = {"delivery_mode": "project_assets", "source": {"dataset_id": "ds"},
                    "payload": {"sha256": d.hashlib.sha256(payload).hexdigest()},
                    "attachments": [{"file": "attachments/floor.png", "sha256": d.hashlib.sha256(asset).hexdigest()}]}
        def package(broken=False):
            stream = io.BytesIO()
            with zipfile.ZipFile(stream, "w") as z:
                z.writestr("manifest.json", json.dumps(manifest))
                z.writestr("project.json", payload)
                z.writestr("attachments/floor.png", b"bad" if broken else asset)
            return stream.getvalue()
        with patch.object(d, "datasets", return_value=[{"id": "ds", "bound_ue_project_id": "ue"}]):
            with patch.object(d, "api_bytes", return_value=package()):
                self.assertEqual(package(), d.fetch_package("http://localhost", "ds", "ue"))
            with patch.object(d, "api_bytes", return_value=package(True)):
                with self.assertRaisesRegex(ValueError, "附件"):
                    d.fetch_package("http://localhost", "ds", "ue")
            manifest["delivery_mode"] = "data_only"
            with patch.object(d, "api_bytes", return_value=package()):
                with self.assertRaisesRegex(ValueError, "完整交付包"):
                    d.fetch_package("http://localhost", "ds", "ue")

    def test_resource_revision_tracks_content_separately(self):
        before = d.plugin_info(self.plugin)
        (self.plugin / "Content").mkdir()
        (self.plugin / "Content/Asset.uasset").write_bytes(b"asset")
        after = d.plugin_info(self.plugin)
        self.assertEqual(before["source_hash"], after["source_hash"])
        self.assertNotEqual(before["resource_hash"], after["resource_hash"])

    def test_rebuild_blocks_open_editor(self):
        with patch.object(d, "editors_running", return_value=True):
            with self.assertRaisesRegex(ValueError, "关闭所有"):
                d.rebuild(self.context)

    def test_late_source_change_keeps_incomplete_marker(self):
        def progress(message):
            if message.startswith("复核源文件"):
                (self.plugin / "Source/test.cpp").write_text("changed", encoding="utf-8")
        dest = self.root / "delivery"
        with self.assertRaisesRegex(ValueError, "源文件变化"):
            d.export_project(self.context, dest, "", "", "", source_only=True, progress=progress)
        self.assertTrue((dest / "EXPORT_INCOMPLETE.txt").exists())
        self.assertFalse((dest / "OntoTwinDelivery/manifest.json").exists())

    def test_secret_stops_export(self):
        (self.plugin / ".env").write_text("secret", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "凭据"):
            d.export_project(self.context, self.root / "delivery", "", "", "", source_only=True)

    def test_external_plugin_directory_is_materialized(self):
        external = self.root / "external_plugins"
        self.make_plugin(external / "ExternalPlugin")
        (external / "ExternalPlugin/OntoTwinSync.uplugin").rename(external / "ExternalPlugin/ExternalPlugin.uplugin")
        self.project.write_text(json.dumps({
            "EngineAssociation": "5.6",
            "AdditionalPluginDirectories": [str(external)],
        }), encoding="utf-8")
        _, _, entries = d.project_files(self.project)
        self.assertIn("Plugins/ExternalPlugin/Source/test.cpp", {relative.as_posix() for _, relative in entries})
        dest = self.root / "delivery"
        d.export_project(self.context, dest, "", "", "", source_only=True)
        self.assertTrue((dest / "Plugins/ExternalPlugin/Source/test.cpp").exists())
        delivered_desc = json.loads((dest / self.project.name).read_text(encoding="utf-8"))
        self.assertNotIn("AdditionalPluginDirectories", delivered_desc)
        self.assertIn("AdditionalPluginDirectories", d.read_json(self.project))

    def test_current_plugin_can_live_in_external_root(self):
        external = self.root / "mother"
        external.mkdir()
        moved = external / "OntoTwinSync"
        self.plugin.rename(moved)
        self.context["plugin"] = str(moved)
        self.project.write_text(json.dumps({"AdditionalPluginDirectories": ["../mother"]}), encoding="utf-8")
        dest = self.root / "delivery"
        result = d.export_project(self.context, dest, "", "", "", source_only=True)
        self.assertTrue((dest / "Plugins/OntoTwinSync/OntoTwinSync.uplugin").exists())
        for record in result["files"]:
            self.assertEqual(record["sha256"], d.digest(dest / record["path"]))

    def test_missing_external_root_blocks_before_export(self):
        self.project.write_text('{"AdditionalPluginDirectories":["missing"]}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "不存在"):
            d.project_files(self.project)

    def test_duplicate_external_plugin_blocks(self):
        external = self.root / "mother"
        self.make_plugin(external / "OntoTwinSync")
        self.project.write_text(json.dumps({"AdditionalPluginDirectories": [str(external)]}), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "重复"):
            d.project_files(self.project)

    def test_update_closed_editor_backup_outside_plugins(self):
        mother = self.root / "mother"
        self.make_plugin(mother)
        (mother / "Source/test.cpp").write_text("new", encoding="utf-8")
        with patch.object(d, "editors_running", return_value=False):
            backup = d.update_copy(self.plugin, mother)
        self.assertEqual("original", (backup / "Source/test.cpp").read_text())
        self.assertEqual("new", (self.plugin / "Source/test.cpp").read_text())
        self.assertNotIn(backup, self.plugin.parent.iterdir())

    def test_update_blocks_running_editor(self):
        mother = self.root / "mother"
        self.make_plugin(mother)
        with patch.object(d, "editors_running", return_value=True):
            with self.assertRaisesRegex(ValueError, "关闭所有"):
                d.update_copy(self.plugin, mother)
        self.assertEqual("original", (self.plugin / "Source/test.cpp").read_text())

    def junction(self, link, target):
        if os.name != "nt":
            link.symlink_to(target, target_is_directory=True)
        else:
            # Both paths are fixed private tempfile children, never user input.
            result = subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], capture_output=True)
            self.assertEqual(0, result.returncode, result.stdout)

    def test_junction_is_materialized_and_cycles_rejected(self):
        external = self.root / "external"
        external.mkdir()
        (external / "asset.uasset").write_bytes(b"fixture")
        content = self.project.parent / "Content"
        self.junction(content, external)
        dest = self.root / "delivery"
        d.export_project(self.context, dest, "", "", "", source_only=True)
        self.assertEqual((dest / "Content").absolute(), (dest / "Content").resolve())
        self.assertEqual(b"fixture", (dest / "Content/asset.uasset").read_bytes())
        with self.assertRaises(ValueError):
            d.export_project(self.context, external / "export", "", "", "", source_only=True)
        self.junction(external / "cycle", external)
        with self.assertRaisesRegex(ValueError, "循环"):
            d.inventory(content)


if __name__ == "__main__":
    unittest.main()
