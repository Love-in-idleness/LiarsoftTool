"""Run beside the test DLLs and launcher, on Windows or via a Wine command prefix."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--wine")
    args = parser.parse_args()
    directory = args.directory.resolve()
    prefix = [args.wine] if args.wine else []

    def windows_path(path):
        return "Z:" + str(path).replace("/", "\\") if args.wine else str(path)

    with tempfile.TemporaryDirectory(prefix="rscript loader 中文 ") as temporary:
        root = Path(temporary)
        installed = root / "installed Русский path"
        installed.mkdir()
        for name in ("RScriptLauncher.exe", "RScriptHost.dll", "loader_game.exe",
                     "loader_plugin.dll", "loader_failure.dll", "loader_bad_abi.dll"):
            shutil.copy2(directory / name, installed / name)
        marker = root / "game entered.txt"
        shutdown = root / "shutdown.txt"
        environment = dict(os.environ, RSCRIPT_TEST_RESULT=windows_path(marker),
                           RSCRIPT_TEST_SHUTDOWN=windows_path(shutdown))
        environment.pop("RSCRIPT_TEST_PLUGIN", None)
        environment.pop("RSCRIPT_TEST_KINSOKU", None)

        def run(plugins, success, kinsoku=None, expected=None):
            marker.unlink(missing_ok=True)
            command = prefix + [str(installed / "RScriptLauncher.exe"), "--game",
                windows_path(installed / "loader_game.exe"), "--resources", windows_path(root), "--wait"]
            for plugin in plugins:
                command += ["--plugin", windows_path(installed / plugin)]
            if kinsoku is not None:
                command += ["--kinsoku", windows_path(kinsoku)]
            current_environment = dict(environment)
            if expected is not None:
                current_environment["RSCRIPT_TEST_KINSOKU"] = expected
            process = subprocess.run(command, env=current_environment, capture_output=True, timeout=90)
            assert (process.returncode == 0) == success, (command, process.returncode, process.stderr)
            assert marker.exists() == success, (command, "game entry point reached after startup failure")
            return marker.read_text() if success else ""

        assert "host=0" in run([], True)
        assert "plugin=initialized" in run(["loader_plugin.dll"], True)
        assert "host=1" in marker.read_text()
        rules = json.dumps({"version": 1, "no_line_start": "。）」»", "no_line_end": "（「«"}, ensure_ascii=False)
        custom_rules = root / "禁则 Русский rules.json"
        custom_rules.write_text(rules, encoding="utf-8")
        assert "plugin=initialized" in run(["loader_plugin.dll"], True, custom_rules, rules)
        # Enabling the built-in filename loads the configuration beside the launcher.
        shutil.copy2(installed / "loader_plugin.dll", installed / "RScriptUnicode.dll")
        run(["RScriptUnicode.dll"], False)
        (installed / "kinsoku.json").write_text(rules, encoding="utf-8")
        assert "plugin=initialized" in run(["RScriptUnicode.dll"], True, expected=rules)
        (installed / "kinsoku.json").unlink()
        assert "plugin=initialized" in run(["loader_plugin.dll"], True)
        run([], False, root / "missing rules.json")
        run(["loader_plugin.dll"], False, root / "missing rules.json")
        for malformed in (b"{}", b'{"version":1,"no_line_start":"\xff","no_line_end":""}',
                          b'{"version":1,"no_line_start":"x","no_line_end":"x"}', b" " * 65537):
            custom_rules.write_bytes(malformed)
            run(["loader_plugin.dll"], False, custom_rules)
        custom_rules.write_text(rules, encoding="utf-8")
        run(["loader_failure.dll"], False)
        run(["loader_plugin.dll", "loader_failure.dll"], False)
        assert shutdown.read_text() == "shutdown"
        run(["loader_bad_abi.dll"], False)
        run(["missing.dll"], False)
        run(["loader_plugin.dll", "loader_plugin.dll"], False)
        run(["RScriptHost.dll"], False)
        (installed / "RScriptHost.dll").unlink()
        assert "host=0" in run([], True)
        run(["loader_plugin.dll"], False)
    print("loader: direct launch, early DLL initialization, kinsoku configuration and startup failure checks passed")


if __name__ == "__main__":
    main()
