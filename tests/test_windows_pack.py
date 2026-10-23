import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location("pack_windows", Path(__file__).parents[1] / "tools/pack_windows.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class WindowsPackTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.bundle = Path(self.temp.name) / "bundle & 中文"
        self.files = ("bin/liarsofttool.exe", "bin/liarsofttool-gui.exe", "bin/first.dll",
                      "bin/second.dll", "bin/gtk.dll", "bin/update-mime-database.exe",
                      "share/locale/ru/LC_MESSAGES/liarsofttool.mo",
                      "share/locale/ru/LC_MESSAGES/gtk40.mo",
                      "share/locale/zh_CN/LC_MESSAGES/liarsofttool.mo",
                      "share/locale/fr/LC_MESSAGES/gtk40.mo",
                      "lib/gtk-4.0/media/gstreamer.dll",
                      "libexec/gstreamer-1.0/gst-plugin-scanner.exe", "COPYING", "docs/README.md")
        for name in self.files:
            target = self.bundle / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"fixture")

    def project(self, executable):
        with patch.object(pack, "windows_path", side_effect=lambda p: "Z:" + str(p).replace("/", "\\")), \
             patch.object(pack, "cli_files", return_value={self.bundle / "bin/first.dll"}):
            xml = pack.project(self.bundle, executable, Path(self.temp.name) / "out & 中文.exe")
        xml = xml.removeprefix('<?xml encoding="utf-16"?>')
        root = ET.fromstring(xml.replace("<>", "<Project>").replace("</>", "</Project>"))
        files = {}
        def collect(parent, path=Path()):
            for child in parent.findall("File"):
                name = child.findtext("Name")
                relative = path if name == "%DEFAULT FOLDER%" else path / name
                if child.findtext("Type") == "3":
                    self.assertEqual(child.findtext("Action"), "0")
                    collect(child.find("Files"), relative)
                else:
                    self.assertEqual(child.findtext("Action"), "3")
                    files[relative.as_posix()] = child.findtext("File")
        collect(root.find("Files/Files"))
        self.assertIn("& 中文", root.findtext("InputFile"))
        self.assertEqual(root.findtext("Files/CompressFiles"), "true")
        self.assertEqual(root.findtext("Options/ShareVirtualSystem"), "true")
        return files

    def test_gui_layout_and_cli_isolation(self):
        gui = self.project("liarsofttool-gui.exe")
        self.assertIn("gtk.dll", gui)
        self.assertIn("libexec/gstreamer-1.0/gst-plugin-scanner.exe", gui)
        self.assertIn("locale/ru/LC_MESSAGES/liarsofttool.mo", gui)
        self.assertIn("share/locale/ru/LC_MESSAGES/gtk40.mo", gui)
        self.assertNotIn("share/locale/fr/LC_MESSAGES/gtk40.mo", gui)
        self.assertFalse(any(name.startswith("bin/") for name in gui))
        self.assertFalse(any(name.endswith(".exe") and name.startswith("liarsofttool") for name in gui))
        cli = self.project("liarsofttool.exe")
        self.assertEqual(set(cli), {"first.dll", "COPYING", "docs/README.md"})

    def test_dependency_closure_handles_cycles_and_system_dlls(self):
        imports = {"liarsofttool.exe": "DLL Name: FIRST.dll\nDLL Name: KERNEL32.dll",
                   "first.dll": "DLL Name: second.dll", "second.dll": "DLL Name: first.dll"}
        with patch.object(subprocess, "check_output", side_effect=lambda command, **kw: imports[Path(command[-1]).name]):
            self.assertEqual(pack.cli_files(self.bundle),
                             {self.bundle / "bin/first.dll", self.bundle / "bin/second.dll"})

    def test_reject_overlap_and_existing_output(self):
        packer = self.bundle / "bin/liarsofttool.exe"
        with self.assertRaises(ValueError):
            pack.main(["pack", str(self.bundle), str(self.bundle / "out"), str(packer)])
        output = Path(self.temp.name) / "output"
        output.mkdir()
        marker = output / "keep"
        marker.write_bytes(b"unchanged")
        with self.assertRaises(FileExistsError):
            pack.main(["pack", str(self.bundle), str(output), str(packer)])
        self.assertEqual(marker.read_bytes(), b"unchanged")


if __name__ == "__main__":
    unittest.main()
