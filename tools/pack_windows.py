#!/usr/bin/env python3
"""Compress a Windows runtime bundle into independent CLI and GUI executables."""
import os
from pathlib import Path
import re
import subprocess
import sys
from xml.sax.saxutils import escape


def windows_path(path):
    if os.name == "nt":
        return str(path.resolve())
    return subprocess.check_output(["winepath", "-w", str(path.resolve())], text=True).strip()


def cli_files(bundle):
    libraries = {p.name.lower(): p for p in (bundle / "bin").glob("*.dll")}
    pending = [bundle / "bin/liarsofttool.exe"]
    found = set()
    while pending:
        source = pending.pop()
        imports = subprocess.check_output(
            [os.environ.get("OBJDUMP", "objdump"), "-p", str(source)], text=True)
        for name in re.findall(r"DLL Name:\s*(\S+)", imports):
            dependency = libraries.get(name.lower())
            if dependency and dependency not in found:
                found.add(dependency)
                pending.append(dependency)
    return found


def project(bundle, executable, output):
    files = {}
    root = windows_path(bundle)
    dependencies = cli_files(bundle) if executable == "liarsofttool.exe" else None
    for source in sorted(bundle.rglob("*")):
        if not source.is_file():
            continue
        relative = source.relative_to(bundle)
        if relative.parts[0] == "bin":
            if source.suffix.lower() != ".dll" or (dependencies is not None and source not in dependencies):
                continue
            relative = Path(source.name)
        elif dependencies is not None and relative.parts[0] not in ("docs", "licenses") and \
                relative.parts[:2] != ("share", "licenses") and len(relative.parts) != 1:
            continue
        elif relative.parts[:2] == ("share", "locale"):
            if relative.parts[2] not in ("en", "ru", "zh_CN", "ja"):
                continue
            if source.name == "liarsofttool.mo":
                # The app checks adjacent catalogs; GTK keeps its share/locale layout.
                relative = Path("locale", *relative.parts[2:])
        files[relative] = source

    tree = {}
    for relative, source in files.items():
        parent = tree
        for part in relative.parts[:-1]:
            parent = parent.setdefault(part, {})
        parent[relative.name] = source

    def item(name, value):
        directory = isinstance(value, dict)
        content = "".join(item(n, v) for n, v in sorted(value.items())) if directory else ""
        return ("<File><Type>" + ("3" if directory else "2") + "</Type><Name>" + escape(name) +
                "</Name><Action>" + ("0" if directory else "3") + "</Action><OverwriteDateTime>false</OverwriteDateTime>"
                "<OverwriteAttributes>false</OverwriteAttributes>" +
                ("<Files>" + content + "</Files>" if directory else
                 "<File>" + escape(root + "\\" + str(value.relative_to(bundle)).replace("/", "\\")) + "</File><ActiveX>false</ActiveX>"
                 "<ActiveXInstall>false</ActiveXInstall><PassCommandLine>false</PassCommandLine>") + "</File>")

    return ('<?xml encoding="utf-16"?><><InputFile>' + escape(windows_path(bundle / "bin" / executable)) +
            "</InputFile><OutputFile>" + escape(windows_path(output)) + "</OutputFile><Files><Enabled>true</Enabled>"
            "<DeleteExtractedOnExit>true</DeleteExtractedOnExit><CompressFiles>true</CompressFiles><Files>" +
            item("%DEFAULT FOLDER%", tree) + "</Files></Files><Registries><Enabled>false</Enabled></Registries>"
            "<Packaging><Enabled>false</Enabled></Packaging><Options><ShareVirtualSystem>true</ShareVirtualSystem>"
            "<MapExecutableWithTemporaryFile>true</MapExecutableWithTemporaryFile>"
            "<AllowRunningOfVirtualExeFiles>true</AllowRunningOfVirtualExeFiles></Options></>")


def main(argv):
    if len(argv) != 4:
        raise SystemExit("Usage: pack_windows.py BUNDLE_DIR NEW_OUTPUT_DIR ENIGMAVBCONSOLE_EXE")
    bundle, destination, packer = (Path(p).resolve() for p in argv[1:])
    if not packer.is_file():
        raise FileNotFoundError(packer)
    if destination == bundle or destination.is_relative_to(bundle) or bundle.is_relative_to(destination):
        raise ValueError("Bundle and output directories must not overlap")
    for executable in ("liarsofttool.exe", "liarsofttool-gui.exe"):
        if not (bundle / "bin" / executable).is_file():
            raise FileNotFoundError(executable)
    destination.mkdir()
    runner = [] if os.name == "nt" else ["wine"]
    for executable in ("liarsofttool.exe", "liarsofttool-gui.exe"):
        output = destination / executable
        configuration = output.with_suffix(".evb")
        configuration.write_text(project(bundle, executable, output), encoding="utf-16")
        with output.with_suffix(".log").open("w") as log:
            subprocess.run([*runner, str(packer), windows_path(configuration)],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        if not output.is_file() or output.stat().st_size == 0:
            raise RuntimeError("Packer did not produce " + str(output))
        print(output, output.stat().st_size, "bytes")


if __name__ == "__main__":
    main(sys.argv)
