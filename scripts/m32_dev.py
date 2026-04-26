#!/usr/bin/env python3
"""
m32_dev.py — single-tool dev workflow for the m32duino Arduino core.

Replaces the old build-release.sh (bash) + dev_serve.py duo. Pure Python, no
external runtime deps.

Sub-commands:
    up        (default) build Rust binary + assemble tarballs + serve via HTTP
              + update arduino-cli.yaml + refresh index. The result: install
              the M32 board via Arduino IDE → Boards Manager.
    package   skip the serve step; only build + assemble release/ artefacts.
    serve     skip build/package; only HTTP + arduino-cli refresh (artefacts
              must already exist in release/).
    stop      stop the HTTP server started by `up` / `serve`.
    status    print current state (server pid, port, paths).
    ci        trigger the GitHub Actions release workflow remotely via the
              `gh` CLI — no tag push needed. Useful to validate the YAML on
              real macOS/Linux/Windows runners before cutting a real release.

Local dev produces a single-host package (only the host you're building on
will be present in package_m32_index.json's tools[].systems[]); CI produces
all four hosts.

Environment knobs:
    PORT             HTTP port for the local index server (default 8765)
    STM32DUINO_REF   git tag/SHA of STM32duino to fork from (default 2.7.1)
    CLI_CONFIG       arduino-cli.yaml path (default ~/.arduinoIDE/arduino-cli.yaml)
    ARDUINO_CLI      arduino-cli binary (default Arduino IDE 2.x bundled CLI)
    RUST_BUILDS_DIR  CI uses this to inject pre-built binaries; locally unset.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

REPO_ROOT = Path(__file__).resolve().parent.parent
RELEASE_DIR = REPO_ROOT / "release"
WORK_DIR_BASE = REPO_ROOT / ".m32_dev_work"
PID_FILE = RELEASE_DIR / ".http-server.pid"
LOG_FILE = RELEASE_DIR / ".http-server.log"

VERSION_FILE = REPO_ROOT / "VERSION"
TEMPLATE_JSON = REPO_ROOT / "package_m32_index.template.json"
PLATFORM_PATCH = REPO_ROOT / "templates" / "platform.patch"

PORT = int(os.environ.get("PORT", "8765"))
STM32DUINO_REF = os.environ.get("STM32DUINO_REF", "2.7.1")
CLI_CONFIG = Path(os.environ.get("CLI_CONFIG", str(Path.home() / ".arduinoIDE/arduino-cli.yaml")))
ARDUINO_CLI = Path(os.environ.get(
    "ARDUINO_CLI",
    "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli",
))

# Mapping: rustc target triple → Arduino host triple (used in tarball filenames + JSON)
RUST_TARGET_TO_ARDUINO_HOST = {
    "aarch64-apple-darwin":     "arm64-apple-darwin",
    "x86_64-apple-darwin":      "x86_64-apple-darwin",
    "x86_64-unknown-linux-gnu": "x86_64-pc-linux-gnu",
    "x86_64-pc-windows-msvc":   "x86_64-mingw32",
    "x86_64-pc-windows-gnu":    "x86_64-mingw32",
}

# ---------------------------------------------------------------- small helpers

def info(msg: str) -> None:  print(msg, flush=True)
def warn(msg: str) -> None:  print(f"!  {msg}", file=sys.stderr, flush=True)
def fatal(msg: str, code: int = 1):
    print(f"error: {msg}", file=sys.stderr, flush=True)
    sys.exit(code)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def file_size(path: Path) -> int:
    return path.stat().st_size


def read_version() -> str:
    return VERSION_FILE.read_text().strip()


# ---------------------------------------------------------------- HTTP server lifecycle

def _running_pid() -> Optional[int]:
    if not PID_FILE.exists():
        return None
    try:
        pid = int(PID_FILE.read_text().strip())
    except ValueError:
        return None
    try:
        os.kill(pid, 0)
        return pid
    except OSError:
        return None


def _port_in_use(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.bind(("127.0.0.1", port))
        except OSError:
            return True
    return False


def start_http_server() -> int:
    """Idempotent. Returns the pid of the running (or newly started) server."""
    pid = _running_pid()
    if pid:
        info(f"-> server already running (pid {pid}) on http://localhost:{PORT}")
        return pid
    if _port_in_use(PORT):
        fatal(f"port {PORT} is in use by another process; try PORT=<other> {sys.argv[0]}")
    if not (RELEASE_DIR / "package_m32_index.json").exists():
        fatal(f"{RELEASE_DIR}/package_m32_index.json missing — run `{sys.argv[0]} package` first")

    log = open(LOG_FILE, "ab")
    proc = subprocess.Popen(
        [sys.executable, "-m", "http.server", str(PORT)],
        cwd=RELEASE_DIR, stdout=log, stderr=log, stdin=subprocess.DEVNULL,
        start_new_session=True,
    )
    PID_FILE.write_text(str(proc.pid))
    for _ in range(25):
        if _port_in_use(PORT):
            break
        time.sleep(0.1)
    else:
        fatal(f"server failed to bind port {PORT}; see {LOG_FILE}")
    info(f"-> server started (pid {proc.pid}) on http://localhost:{PORT}")
    return proc.pid


def stop_http_server() -> None:
    pid = _running_pid()
    if not pid:
        info("-> server not running")
        PID_FILE.unlink(missing_ok=True)
        return
    try:
        os.kill(pid, 15)
    except OSError:
        pass
    PID_FILE.unlink(missing_ok=True)
    info(f"-> server stopped (pid {pid})")


# ---------------------------------------------------------------- arduino-cli config

def update_arduino_cli_config(index_url: str) -> None:
    """Insert / update m32 entry in board_manager.additional_urls of arduino-cli.yaml."""
    if not CLI_CONFIG.exists():
        warn(f"{CLI_CONFIG} not found — add the URL manually in Arduino IDE Preferences")
        return
    text = CLI_CONFIG.read_text()
    lines = text.splitlines()
    out, i, touched = [], 0, False
    while i < len(lines):
        line = lines[i]
        m = re.match(r"(\s*)additional_urls:\s*$", line)
        if not m:
            out.append(line); i += 1; continue
        out.append(line)
        indent = m.group(1) + "    "
        i += 1
        urls = []
        while i < len(lines) and re.match(rf"{re.escape(indent)}-\s", lines[i]):
            urls.append(lines[i].strip()[2:].strip()); i += 1
        urls = [u for u in urls if not u.endswith("package_m32_index.json")]
        urls.insert(0, index_url)
        out.extend(f"{indent}- {u}" for u in urls)
        touched = True
    if not touched:
        fatal("could not locate board_manager.additional_urls block in arduino-cli.yaml")
    suffix = "\n" if text.endswith("\n") else ""
    CLI_CONFIG.write_text("\n".join(out) + suffix)
    info(f"-> arduino-cli.yaml additional_urls now points to {index_url}")


def refresh_arduino_cli_index() -> None:
    if not ARDUINO_CLI.is_file() or not os.access(ARDUINO_CLI, os.X_OK):
        warn(f"arduino-cli not found at {ARDUINO_CLI} — restart Arduino IDE manually")
        return
    subprocess.run(
        [str(ARDUINO_CLI), "--config-file", str(CLI_CONFIG), "core", "update-index"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
    )
    cached = Path.home() / "Library/Arduino15/package_m32_index.json"
    if cached.exists():
        info(f"-> index fetched into {cached}")
    else:
        warn(f"index NOT fetched — sanity-check: curl -I http://localhost:{PORT}/package_m32_index.json")


# ---------------------------------------------------------------- Rust build

@dataclass
class RustBinary:
    arduino_host: str
    path: Path
    is_windows: bool = False

    @property
    def filename(self) -> str:
        return "m32_flash.exe" if self.is_windows else "m32_flash"


def cargo_build_native() -> RustBinary:
    """Build the Rust binary for the current host. Returns its info."""
    info("-> cargo build --release (native host)")
    crate_dir = REPO_ROOT / "tools" / "m32_flasher"
    subprocess.run(["cargo", "build", "--release"], cwd=crate_dir, check=True)
    bin_path = crate_dir / "target" / "release" / "m32_flash"
    if not bin_path.is_file():
        fatal(f"cargo built but binary not found at {bin_path}")

    triple = subprocess.check_output(
        ["rustc", "-vV"], text=True
    ).splitlines()
    host = next((l.split(":", 1)[1].strip() for l in triple if l.startswith("host:")), None)
    if not host:
        fatal("cannot determine rustc host triple")
    arduino_host = RUST_TARGET_TO_ARDUINO_HOST.get(host)
    if not arduino_host:
        fatal(f"unsupported rustc host: {host}")
    return RustBinary(arduino_host=arduino_host, path=bin_path, is_windows="windows" in host)


def collect_rust_binaries_from_dir(builds_dir: Path) -> list[RustBinary]:
    """Used in CI: scans RUST_BUILDS_DIR/<host>/m32_flash[.exe] for each host."""
    out: list[RustBinary] = []
    for arduino_host in ("arm64-apple-darwin", "x86_64-apple-darwin",
                         "x86_64-pc-linux-gnu", "x86_64-mingw32"):
        for fname in ("m32_flash", "m32_flash.exe"):
            cand = builds_dir / arduino_host / fname
            if cand.is_file():
                out.append(RustBinary(
                    arduino_host=arduino_host, path=cand,
                    is_windows=(fname == "m32_flash.exe"),
                ))
                break
    return out


# ---------------------------------------------------------------- platform tarball

def fetch_stm32duino(work_dir: Path) -> Path:
    """Fetch STM32duino into work_dir/stm32duino, return that path."""
    info(f"-> fetching STM32duino {STM32DUINO_REF}")
    url = f"https://github.com/stm32duino/Arduino_Core_STM32/archive/refs/tags/{STM32DUINO_REF}.tar.gz"
    archive = work_dir / "stm32duino.tar.gz"
    target = work_dir / "stm32duino"
    target.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url) as r, archive.open("wb") as f:
        shutil.copyfileobj(r, f)
    with tarfile.open(archive, "r:gz") as tf:
        members = tf.getmembers()
        # strip the top-level directory (Arduino_Core_STM32-<ref>/)
        prefix = members[0].name.split("/", 1)[0] + "/"
        for m in members:
            if m.name == prefix.rstrip("/"):
                continue
            if not m.name.startswith(prefix):
                continue
            m.name = m.name[len(prefix):]
            tf.extract(m, target, filter="data")
    return target


def patch_platform_txt(stm_platform_txt: Path, version: str) -> str:
    """Apply REPLACE_HEADER + REPLACE_UPLOAD_TOOL sections of platform.patch onto
    STM32duino's platform.txt; return the patched contents."""
    src = stm_platform_txt.read_text()
    patch = PLATFORM_PATCH.read_text()

    def section(name: str) -> str:
        m = re.search(rf"^{name}\n(.*?)(?=^(?:REPLACE_[A-Z_]+|\Z)\n?)",
                      patch, re.M | re.S)
        return ((m.group(1) if m else "").rstrip() + "\n")

    hdr = (section("REPLACE_HEADER")
           .replace("{{VERSION}}", version)
           .replace("{{STM32DUINO_VERSION}}", STM32DUINO_REF))

    out = re.sub(
        r"^name=.*?\n(?:(?:version|compiler_flags|compiler_warning_flags|compiler_ldflags)=.*?\n)*",
        hdr + "\n", src, count=1, flags=re.M,
    )
    upload = section("REPLACE_UPLOAD_TOOL")
    out += "\n\n# ======================= m32-arduino additions =======================\n"
    out += upload
    return out


def assemble_platform(work_dir: Path, version: str) -> Path:
    """Build the m32-stm32-<version>/ tree. Returns the path to that dir."""
    stm_dir = fetch_stm32duino(work_dir)
    plat_dir = work_dir / f"m32-stm32-{version}"
    plat_dir.mkdir(parents=True, exist_ok=True)
    info(f"-> assembling platform tree at {plat_dir.name}")

    # Core
    shutil.copytree(stm_dir / "cores", plat_dir / "cores", dirs_exist_ok=True)

    # System subset for STM32F1xx
    sys_drivers = plat_dir / "system" / "Drivers"
    sys_drivers.mkdir(parents=True, exist_ok=True)
    shutil.copytree(stm_dir / "system" / "Drivers" / "CMSIS",
                    sys_drivers / "CMSIS", dirs_exist_ok=True)
    shutil.copytree(stm_dir / "system" / "Drivers" / "STM32F1xx_HAL_Driver",
                    sys_drivers / "STM32F1xx_HAL_Driver", dirs_exist_ok=True)
    shutil.copytree(stm_dir / "system" / "STM32F1xx",
                    plat_dir / "system" / "STM32F1xx", dirs_exist_ok=True)
    shutil.copytree(stm_dir / "system" / "extras",
                    plat_dir / "system" / "extras", dirs_exist_ok=True)
    shutil.copytree(stm_dir / "system" / "Middlewares",
                    plat_dir / "system" / "Middlewares", dirs_exist_ok=True)
    shutil.copy2(stm_dir / "system" / "ldscript.ld",
                 plat_dir / "system" / "ldscript.ld")
    cmsis_packs = stm_dir / "system" / "CMSIS_PACKS_DIR"
    if cmsis_packs.exists():
        shutil.copy2(cmsis_packs, plat_dir / "system" / "CMSIS_PACKS_DIR")
    svd = stm_dir / "system" / "svd" / "STM32F1xx.svd"
    if svd.exists():
        (plat_dir / "system" / "svd").mkdir(exist_ok=True)
        shutil.copy2(svd, plat_dir / "system" / "svd" / "STM32F1xx.svd")

    # Variants
    shutil.copytree(REPO_ROOT / "platform" / "variants" / "NI_M32",
                    plat_dir / "variants" / "NI_M32", dirs_exist_ok=True)

    # Bundled libraries from STM32duino (SPI, Wire, SrcWrapper, …)
    shutil.copytree(stm_dir / "libraries", plat_dir / "libraries", dirs_exist_ok=True)

    # Examples — wrapped in a fake library (Arduino IDE 2.x reliably surfaces
    # File → Examples → M32_Examples, but ignores platform-level examples/).
    ex_lib = plat_dir / "libraries" / "M32_Examples"
    ex_lib.mkdir(parents=True, exist_ok=True)
    shutil.copytree(REPO_ROOT / "examples", ex_lib / "examples", dirs_exist_ok=True)
    (ex_lib / "library.properties").write_text(
        f"name=M32_Examples\n"
        f"version={version}\n"
        f"author=Federico Paglioni (@fedepaj)\n"
        f"maintainer=Federico Paglioni (@fedepaj)\n"
        f"sentence=Example sketches for the NI Komplete Kontrol M32 board.\n"
        f"paragraph=Bundled with the m32duino core.\n"
        f"category=Other\n"
        f"url=https://github.com/fedepaj/m32duino\n"
        f"architectures=stm32\n"
        f"includes=Arduino.h\n"
    )

    # Our boards.txt (replaces STM32duino's). programmers.txt is shipped (even
    # if empty) so the IDE enables Tools → Burn Bootloader.
    shutil.copy2(REPO_ROOT / "platform" / "boards.txt", plat_dir / "boards.txt")
    progs = REPO_ROOT / "platform" / "programmers.txt"
    if progs.exists():
        shutil.copy2(progs, plat_dir / "programmers.txt")
    else:
        (plat_dir / "programmers.txt").write_text("")

    # platform.txt: based on STM32duino's, with our patches applied
    patched = patch_platform_txt(stm_dir / "platform.txt", version)
    (plat_dir / "platform.txt").write_text(patched)
    info(f"   patched platform.txt: {len(patched)} B")

    return plat_dir


def package_platform_tarball(plat_dir: Path, version: str) -> dict:
    """Tar plat_dir into release/m32-stm32-<v>.tar.bz2; return meta dict."""
    name = plat_dir.name  # m32-stm32-<v>
    tar_path = RELEASE_DIR / f"{name}.tar.bz2"
    info(f"-> tar -cjf {tar_path.name}")
    with tarfile.open(tar_path, "w:bz2") as tf:
        tf.add(plat_dir, arcname=name)
    meta = {"file": tar_path.name, "size": file_size(tar_path), "sha256": sha256(tar_path)}
    info(f"   {meta['size']} B  sha {meta['sha256'][:16]}…")
    return meta


# ---------------------------------------------------------------- tool tarballs

def package_tool_tarball(rust_bin: RustBinary, work_dir: Path, version: str) -> dict:
    """Build release/m32-dfu-<v>-<host>.tar.bz2 for one host. Returns meta dict."""
    name = f"m32-dfu-{version}"
    stage = work_dir / f"tool-{rust_bin.arduino_host}" / name
    stage.mkdir(parents=True, exist_ok=True)

    # Just the Rust binary + factory firmware. No more dfu-util / Python script.
    dest_bin = stage / rust_bin.filename
    shutil.copy2(rust_bin.path, dest_bin)
    dest_bin.chmod(0o755)
    (stage / "firmware").mkdir(exist_ok=True)
    shutil.copy2(REPO_ROOT / "tools" / "m32_flasher" / "firmware" / "m32_ni_factory.bin",
                 stage / "firmware" / "m32_ni_factory.bin")

    tar_path = RELEASE_DIR / f"m32-dfu-{version}-{rust_bin.arduino_host}.tar.bz2"
    info(f"-> tar -cjf {tar_path.name}")
    with tarfile.open(tar_path, "w:bz2") as tf:
        tf.add(stage, arcname=name)
    meta = {
        "file": tar_path.name,
        "size": file_size(tar_path),
        "sha256": sha256(tar_path),
        "arduino_host": rust_bin.arduino_host,
    }
    info(f"   {meta['size']} B  sha {meta['sha256'][:16]}…  ({rust_bin.arduino_host})")
    return meta


# ---------------------------------------------------------------- index JSON

def render_index_json(version: str, asset_url_prefix: str,
                      platform_meta: dict, tool_metas: list[dict]) -> str:
    """Load template, fill version+platform, filter tools[].systems[] to the hosts
    actually built, return JSON string."""
    idx = json.loads(TEMPLATE_JSON.read_text())
    plat = idx["packages"][0]["platforms"][0]
    plat["version"]         = version
    plat["url"]             = asset_url_prefix + platform_meta["file"]
    plat["archiveFileName"] = platform_meta["file"]
    plat["checksum"]        = "SHA-256:" + platform_meta["sha256"]
    plat["size"]            = str(platform_meta["size"])

    # Tool-version dependency must match the version we're shipping
    for dep in plat.get("toolsDependencies", []):
        if dep.get("packager") == "m32" and dep.get("name") == "m32-dfu":
            dep["version"] = version

    tool = idx["packages"][0]["tools"][0]
    tool["version"] = version
    by_host = {m["arduino_host"]: m for m in tool_metas}
    new_systems = []
    for sys_entry in tool["systems"]:
        host = sys_entry["host"]
        # i686-mingw32 piggy-backs on the x86_64 Windows binary (STM32duino does the same)
        target_host = "x86_64-mingw32" if host == "i686-mingw32" else host
        meta = by_host.get(target_host)
        if not meta:
            continue
        new_systems.append({
            "host": host,
            "url": asset_url_prefix + meta["file"],
            "archiveFileName": meta["file"],
            "checksum": "SHA-256:" + meta["sha256"],
            "size": str(meta["size"]),
        })
    tool["systems"] = new_systems

    return json.dumps(idx, indent=2) + "\n"


# ---------------------------------------------------------------- top-level commands

def cmd_package(version: str, asset_url_prefix: str) -> None:
    RELEASE_DIR.mkdir(exist_ok=True)
    if WORK_DIR_BASE.exists():
        shutil.rmtree(WORK_DIR_BASE)
    WORK_DIR_BASE.mkdir()

    info("=" * 60)
    info(f"  Building m32duino  v{version}")
    info(f"  STM32duino fork base: {STM32DUINO_REF}")
    info(f"  Asset URL prefix:     {asset_url_prefix}")
    info("=" * 60)

    # 1. Rust binaries — CI injects RUST_BUILDS_DIR; locally we cargo-build native
    builds_dir = os.environ.get("RUST_BUILDS_DIR", "").strip()
    if builds_dir:
        info(f"-> using pre-built Rust binaries from {builds_dir}")
        rust_bins = collect_rust_binaries_from_dir(Path(builds_dir))
        if not rust_bins:
            fatal(f"RUST_BUILDS_DIR set but no binaries found under {builds_dir}/<host>/m32_flash[.exe]")
    else:
        rust_bins = [cargo_build_native()]

    info(f"   hosts to package: {', '.join(b.arduino_host for b in rust_bins)}")

    # 2. Platform tarball
    plat_dir = assemble_platform(WORK_DIR_BASE, version)
    plat_meta = package_platform_tarball(plat_dir, version)

    # 3. One tool tarball per host
    tool_metas = [package_tool_tarball(b, WORK_DIR_BASE, version) for b in rust_bins]

    # 4. Index JSON
    json_path = RELEASE_DIR / "package_m32_index.json"
    json_path.write_text(render_index_json(version, asset_url_prefix, plat_meta, tool_metas))
    info(f"-> rendered {json_path.name}  (systems: {len(tool_metas)})")

    # 5. Cleanup
    shutil.rmtree(WORK_DIR_BASE)


def cmd_serve() -> None:
    start_http_server()
    index_url = f"http://localhost:{PORT}/package_m32_index.json"
    update_arduino_cli_config(index_url)
    refresh_arduino_cli_index()
    info("")
    info("Done. In Arduino IDE: Tools → Board → Boards Manager → search 'M32' → Install")
    info("(if the package is already installed, Remove first to pick up the new tarball).")
    info(f"To stop the server: {sys.argv[0]} stop")


def cmd_up() -> None:
    version = read_version()
    cmd_package(version, asset_url_prefix=f"http://localhost:{PORT}/")
    cmd_serve()


def cmd_status() -> None:
    pid = _running_pid()
    info(f"server:     {'running (pid ' + str(pid) + ')' if pid else 'not running'} on http://localhost:{PORT}")
    info(f"release:    {RELEASE_DIR}")
    info(f"index URL:  http://localhost:{PORT}/package_m32_index.json")
    info(f"cli config: {CLI_CONFIG}")
    if LOG_FILE.exists():
        info(f"server log: {LOG_FILE}")


def cmd_ci(args) -> None:
    if shutil.which("gh") is None:
        fatal("`gh` CLI not installed (brew install gh)")
    workflow_version = args.version or "0.0.0-cidev"
    info(f"-> gh workflow run release.yml -f version={workflow_version}")
    subprocess.run(
        ["gh", "workflow", "run", "release.yml",
         "-f", f"version={workflow_version}"],
        cwd=REPO_ROOT, check=True,
    )
    info("Triggered. View progress with:")
    info("  gh run list --workflow=release.yml --limit 3")
    info("  gh run watch")


# ---------------------------------------------------------------- CLI

def main() -> None:
    ap = argparse.ArgumentParser(
        prog="m32_dev", description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("up", help="build + package + serve (default)")
    pkg = sub.add_parser("package", help="only build + package, no serve")
    pkg.add_argument("--asset-url-prefix",
                     help="URL prefix for tarballs in the generated JSON. "
                          "Default: http://localhost:$PORT/")
    pkg.add_argument("--version",
                     help="Version string. Default: contents of the VERSION file.")
    sub.add_parser("serve", help="only serve (artefacts must already be in release/)")
    sub.add_parser("stop", help="stop the HTTP server")
    sub.add_parser("status", help="show current state")
    ci = sub.add_parser("ci", help="trigger the GitHub Actions release workflow remotely (gh)")
    ci.add_argument("--version", help="workflow input.version (default 0.0.0-cidev)")

    args = ap.parse_args()
    cmd = args.cmd or "up"

    if cmd == "up":       cmd_up()
    elif cmd == "package":
        version = args.version or read_version()
        prefix = args.asset_url_prefix or f"http://localhost:{PORT}/"
        cmd_package(version, asset_url_prefix=prefix)
    elif cmd == "serve":  cmd_serve()
    elif cmd == "stop":   stop_http_server()
    elif cmd == "status": cmd_status()
    elif cmd == "ci":     cmd_ci(args)
    else:
        ap.print_help()
        sys.exit(2)


if __name__ == "__main__":
    main()
