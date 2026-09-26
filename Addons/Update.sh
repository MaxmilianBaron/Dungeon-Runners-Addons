#!/bin/sh
set -eu
if ! command -v python3 >/dev/null 2>&1; then
    printf '%s\n' 'Python 3.8 or newer is required. Install python3 using your distribution package manager.' >&2
    exit 1
fi
DR_ADDONS_LAUNCHER=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")
export DR_ADDONS_LAUNCHER
exec python3 - "$@" <<'PYTHON'
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import zlib

REPOSITORY = 'MaxmilianBaron/Dungeon-Runners-Addons'
HASH = re.compile(r'[0-9a-f]{64}')
FILES = {
    'd3d9.dll', 'Addons/Runtime/Addons.dll', 'Addons/Runtime/ui-resources.json',
    'Addons/DamageMeter/addon.ini', 'Addons/HideGoldLabels/addon.ini',
    'Addons/Update.sh',
}
LICENSE_PATH = 'Addons/Licenses/LICENSE.txt'
LICENSE_TEXT = '''MIT License

Copyright (c) 2026 MaxmilianBaron

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
'''
LIMIT = 16 * 1024 * 1024


class InstallError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise InstallError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_hash(path, algorithm='sha256'):
    hasher = hashlib.new(algorithm)
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            hasher.update(block)
    return hasher.hexdigest()


def child(root, relative):
    require(isinstance(relative, str) and relative and '\\' not in relative and ':' not in relative,
            'Invalid installation path.')
    parts = relative.split('/')
    require(not PurePosixPath(relative).is_absolute() and all(p not in ('', '.', '..') for p in parts),
            'Invalid installation path.')
    current = root
    for part in parts:
        if current.is_dir():
            matches = [p.name for p in current.iterdir() if p.name.casefold() == part.casefold()]
            require(not matches or matches == [part], 'Conflicting file name capitalization.')
        current = current / part
        require(not current.is_symlink(), 'Linked addon files or folders are not supported.')
        if current.exists():
            require(current.is_dir() or current.is_file(), 'Unsupported installation target.')
    return current


def read_json(path, limit=65536):
    require(path.is_file() and path.stat().st_size <= limit, 'Missing or oversized metadata.')
    return json.loads(path.read_text(encoding='utf-8-sig'))


def validate_manifest(manifest, version=None):
    require(isinstance(manifest, dict) and manifest.get('schema') == 1 and manifest.get('platform') == 'linux',
            'Unsupported Linux package.')
    require(isinstance(manifest.get('version'), str) and
            re.fullmatch(r'[0-9][A-Za-z0-9._-]{0,63}', manifest['version']) is not None,
            'Invalid package version.')
    require(version is None or manifest['version'] == version, 'Release version mismatch.')
    for key in ('clients', 'loaders'):
        values = manifest.get(key, [])
        require(isinstance(values, list) and 1 <= len(values) <= 64 and
                all(isinstance(v, str) and HASH.fullmatch(v) for v in values), 'Invalid compatibility list.')
    chains = manifest.get('chainLoaders', [])
    require(isinstance(chains, list) and len(chains) <= 64 and
            all(isinstance(v, str) and HASH.fullmatch(v) for v in chains), 'Invalid graphics compatibility list.')
    files = manifest.get('files', [])
    require(isinstance(files, list) and len(FILES) <= len(files) <= 64, 'Invalid file list.')
    paths, assets = set(), set()
    for item in files:
        require(isinstance(item, dict), 'Invalid package file.')
        path, asset, checksum = item.get('path', ''), item.get('asset', ''), item.get('sha256', '')
        require(all(isinstance(v, str) for v in (path, asset, checksum)), 'Invalid package file.')
        allowed = path in FILES or re.fullmatch(r'Addons/[A-Za-z0-9_-]+/(addon\.ini|Addon\.dll)', path)
        require(allowed and HASH.fullmatch(checksum) and re.fullmatch(r'[A-Za-z0-9_.-]{1,96}', asset),
                'Invalid package file.')
        require(path.casefold() not in paths and asset.casefold() not in assets, 'Duplicate package file.')
        paths.add(path.casefold())
        assets.add(asset.casefold())
    require({p.casefold() for p in FILES} <= paths, 'Incomplete Linux package.')
    return files


def validate_client(game, manifest, updating=False):
    exe = child(game, 'DungeonRunners.exe')
    require(exe.is_file() and file_hash(exe) in manifest['clients'],
            'Select a supported folder containing DungeonRunners.exe.')
    loader = child(game, 'd3d9.dll')
    require(not updating or loader.is_file(), 'The updater requires an existing addon installation. Use the installer first.')
    require(not loader.exists() or (loader.is_file() and file_hash(loader) in manifest['loaders'] + manifest.get('chainLoaders', [])),
            'This d3d9.dll is not supported for coexistence. It was left untouched.')


def local_cache(game, metadata):
    require(file_hash(child(game, 'game.pki'), 'sha1') == metadata.get('pkiSha1'),
            'This game data version is not supported.')
    entries = metadata.get('entries', [])
    require(isinstance(entries, list) and len(entries) == 7, 'Invalid resource list.')
    resources = {}
    with child(game, 'game.pkg').open('rb') as stream:
        size = os.fstat(stream.fileno()).st_size
        for entry in entries:
            name = entry['name']
            offset, stored_size, decoded_size = (entry[k] for k in ('package_offset', 'stored_size', 'decoded_size'))
            require(name not in resources and all(type(v) is int for v in (offset, stored_size, decoded_size)) and
                    0 < stored_size <= 2097152 and 0 < decoded_size <= 2097152 and
                    0 <= offset <= size - stored_size, 'Invalid UI resource range.')
            stream.seek(offset)
            raw = stream.read(stored_size)
            require(digest(raw) == entry['storedSha256'], 'Game resource checksum mismatch.')
            if entry['flags'] & 1:
                inflater = zlib.decompressobj()
                raw = inflater.decompress(raw, decoded_size + 1)
                require(inflater.eof and not inflater.unused_data and not inflater.unconsumed_tail,
                        'Invalid compressed UI resource.')
            require(len(raw) == decoded_size and digest(raw) == entry['decodedSha256'],
                    'Decoded UI resource checksum mismatch.')
            resources[name] = raw
    components = []
    for name, side in (('InGameUI4', 1024), ('NewUI', 512), ('Font_Outline', 512)):
        raw = resources[name]
        require(len(raw) >= 128 + side * side and raw[:4] == b'DDS ' and raw[84:88] == b'DXT3' and
                struct.unpack_from('<II', raw, 12) == (side, side), 'Unsupported texture layout.')
        components.append(raw[128:128 + side * side])
    metrics = resources['fonts\\Font_Outline_Metrics']
    require(len(metrics) == 512, 'Unsupported font metrics.')
    components.append(bytes((v + 2) & 255 for v in struct.unpack('<256H', metrics)))
    components.append(resources['sylfaen'])
    result = b'DRUI0001' + struct.pack('<5I', *(len(c) for c in components)) + b''.join(components)
    require(len(result) == metadata.get('size') and digest(result) == metadata.get('sha256'),
            'Local UI cache verification failed.')
    return result



def execute(command, timeout=90):
    return subprocess.run(list(map(str, command)), capture_output=True,
                          text=True, errors='replace', timeout=timeout)


def terminal_input(prompt):
    try:
        with open('/dev/tty', 'r+', encoding='utf-8') as tty:
            tty.write(prompt + ' ')
            tty.flush()
            answer = tty.readline().strip()
    except OSError:
        raise InstallError('Run this script in a terminal, or install zenity or kdialog for folder selection.')
    if not answer:
        raise KeyboardInterrupt
    return answer


def dialog_backend():
    if os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'):
        return shutil.which('zenity') or shutil.which('kdialog')
    return None


def choose_path(prompt, folder=True):
    tool = dialog_backend()
    if tool:
        if Path(tool).name == 'zenity':
            command = [tool, '--file-selection', '--title=' + prompt]
            if folder:
                command.append('--directory')
        else:
            command = [tool, '--title', prompt,
                       '--getexistingdirectory' if folder else '--getopenfilename', str(Path.home())]
        result = execute(command, timeout=600)
        if result.returncode == 1:
            raise KeyboardInterrupt
        require(result.returncode == 0, 'Could not open the file selection dialog.')
        value = result.stdout.strip()
    else:
        value = terminal_input(prompt)
    require(value and not any(ord(c) < 32 for c in value), 'Invalid path.')
    return Path(value).expanduser().resolve()


def select_path(prompt, candidates, folder=True):
    candidates = sorted(set(p.resolve() for p in candidates), key=str)
    if candidates:
        labels = [str(p) for p in candidates] + ['Choose another location']
        tool = dialog_backend()
        if tool:
            if Path(tool).name == 'zenity':
                command = [tool, '--list', '--title=' + prompt, '--width=800', '--height=360',
                           '--column=ID', '--column=Location', '--hide-column=1', '--print-column=1']
            else:
                command = [tool, '--title', 'Dungeon Runners Addons', '--menu', prompt]
            for number, label in enumerate(labels, 1):
                command.extend((str(number), label))
            result = execute(command, timeout=600)
            if result.returncode == 1:
                raise KeyboardInterrupt
            require(result.returncode == 0, 'Could not open the selection dialog.')
            choice = result.stdout.strip()
        else:
            print(prompt)
            for number, label in enumerate(labels, 1):
                print(str(number) + ': ' + label)
            choice = terminal_input('Number (empty to cancel):')
        require(choice.isdigit() and 1 <= int(choice) <= len(labels), 'Invalid selection.')
        if int(choice) <= len(candidates):
            return candidates[int(choice) - 1]
    return choose_path(prompt, folder)


def game_stopped():
    result = execute(['ps', '-eo', 'args='], timeout=10)
    require(result.returncode == 0, 'Could not check running applications.')
    require(not re.search(r'dungeonrunners(?:118)?\.exe', result.stdout, re.I),
            'Close Dungeon Runners before installing or updating addons.')


def steam_roots():
    home = Path.home()
    paths = [home/'.local/share/Steam', home/'.steam/steam', home/'.steam/root',
             home/'.var/app/com.valvesoftware.Steam/.local/share/Steam']
    if os.environ.get('STEAM_COMPAT_CLIENT_INSTALL_PATH'):
        paths.insert(0, Path(os.environ['STEAM_COMPAT_CLIENT_INSTALL_PATH']))
    return list(dict.fromkeys(p.resolve() for p in paths if (p/'steamapps').is_dir()))


def steam_libraries():
    paths = steam_roots()
    for root in list(paths):
        config = root/'steamapps/libraryfolders.vdf'
        if config.is_file() and config.stat().st_size <= 1048576:
            for value in re.findall(r'"path"\s+"((?:\\.|[^"\\])*)"', config.read_text(errors='replace')):
                value = value.replace(r'\\', '\\').replace(r'\"', '"')
                candidate = Path(value)
                if candidate.is_absolute() and (candidate/'steamapps').is_dir():
                    paths.append(candidate.resolve())
    return list(dict.fromkeys(paths))


def existing_prefix(path):
    return path.is_dir() and all((path/name).is_file() for name in ('user.reg', 'system.reg')) and (path/'drive_c').is_dir()


def launcher_locations():
    home = Path.home()
    data = Path(os.environ.get('XDG_DATA_HOME', str(home/'.local/share')))
    config = Path(os.environ.get('XDG_CONFIG_HOME', str(home/'.config')))
    return [(data/'lutris', config/'lutris', None),
            (data/'bottles', config/'bottles', None),
            (config/'heroic', config/'heroic', None)] + [
        (home/'.var/app'/app/'data'/name, home/'.var/app'/app/'config'/name, app)
        for name, app in [('lutris', 'org.lutris.Lutris'), ('bottles', 'com.usebottles.bottles'),
                          ('heroic', 'com.heroicgameslauncher.hgl')]]


def configured_paths():
    paths = []
    def extract(value):
        if isinstance(value, dict):
            for key, item in value.items():
                if key in ('winePrefix', 'prefix', 'install_path', 'installPath', 'exe') and isinstance(item, str):
                    candidate = Path(item).expanduser()
                    if candidate.is_absolute():
                        paths.append(candidate)
                elif isinstance(item, (dict, list)):
                    extract(item)
        elif isinstance(value, list):
            for item in value:
                extract(item)
    for data, config, app in launcher_locations():
        for file in list(config.glob('GamesConfig/*.json'))[:500]:
            try:
                extract(read_json(file, 1048576))
            except (OSError, ValueError, InstallError):
                continue
        for file in list(config.glob('games/*.yml'))[:500]:
            if file.stat().st_size > 1048576:
                continue
            for value in re.findall(r'^\s+(?:prefix|exe|working_dir):\s+(.+)$', file.read_text(errors='replace'), re.M):
                candidate = Path(value.strip().strip('\"\'')).expanduser()
                if candidate.is_absolute():
                    paths.append(candidate)
    return paths


def prefix_candidates():
    home = Path.home()
    paths = [home/'.wine']
    for key in ('WINEPREFIX', 'STEAM_COMPAT_DATA_PATH'):
        if os.environ.get(key):
            path = Path(os.environ[key]).expanduser()
            paths.append(path/'pfx' if key == 'STEAM_COMPAT_DATA_PATH' else path)
    for root in steam_libraries():
        paths.extend((root/'steamapps/compatdata').glob('*/pfx'))
    paths.extend(configured_paths())
    roots = [home/'Games', home/'.cxoffice', home/'.PlayOnLinux/wineprefix']
    roots += [data/'bottles' for data, config, app in launcher_locations() if data.name == 'bottles']
    visited = 0
    for root in roots:
        for directory, children, files in os.walk(root, followlinks=False):
            visited += 1
            current = Path(directory)
            if existing_prefix(current):
                paths.append(current)
                children.clear()
            elif visited > 10000 or len(current.relative_to(root).parts) >= 4:
                children.clear()
            else:
                children[:] = [d for d in children if d.lower() not in ('drive_c', 'dosdevices', 'runners')
                               and not (current/d).is_symlink()]
    return list(dict.fromkeys(p.resolve() for p in paths if existing_prefix(p)))


def find_games(prefixes):
    roots = [p/'drive_c' for p in prefixes]
    roots += [p/'steamapps/common' for p in steam_libraries()]
    roots += [Path.home()/'Games']
    for path in configured_paths():
        if path.name == 'DungeonRunners.exe' and path.is_file():
            roots.append(path.parent)
        elif (path/'DungeonRunners.exe').is_file():
            roots.append(path)
    found = []
    visited = 0
    for root in roots:
        for path, directories, files in os.walk(root, followlinks=False):
            visited += 1
            current = Path(path)
            if visited > 10000:
                return found
            if 'DungeonRunners.exe' in files:
                found.append(current.resolve())
                directories.clear()
            elif len(current.relative_to(root).parts) >= 8:
                directories.clear()
            else:
                directories[:] = [d for d in directories if d.lower() not in
                                  ('windows', 'users', 'addons', '.git') and not (current/d).is_symlink()]
    return list(dict.fromkeys(found))


def completed_backup(game, name):
    require(re.fullmatch(r'[0-9]{8}-[0-9]{6}-[a-f0-9]{8}', name), 'Unknown backup name.')
    backup = child(game, 'Addons/Backups/' + name)
    require(backup.is_dir(), 'Missing backup folder.')
    metadata = read_json(child(backup, 'installation.json'))
    require(isinstance(metadata, dict) and isinstance(metadata.get('version'), str) and
            re.fullmatch(r'[0-9][A-Za-z0-9._-]{0,63}', metadata['version']), 'Unknown backup version.')
    entries = metadata.get('files')
    require(isinstance(entries, list) and len(entries) <= 67, 'Unknown backup file list.')
    allowed = {LICENSE_PATH, 'd3d9.dll', 'd3d9.previous.dll', 'Addons/Update.sh', 'Addons/Update.command',
               'Addons/Update.cmd', 'Addons/Runtime/Addons.dll', 'Addons/Runtime/ui.bin',
               'Addons/Runtime/ui-resources.json', 'Addons/Runtime/Update.ps1', 'Addons/Runtime/macOS.py',
               'Addons/DamageMeter/DamageMeter.dll', 'Addons/DamageMeter/Dear-ImGui-LICENSE.txt',
               'Addons/DamageMeter/MinHook-LICENSE.txt'}
    expected = {'installation.json'}
    directories = {''}
    for relative in entries:
        require(isinstance(relative, str) and (relative in allowed or
                re.fullmatch(r'Addons/[A-Za-z0-9_-]+/addon\.ini', relative)), 'Unknown backup file.')
        require(relative not in expected, 'Duplicate backup file.')
        expected.add(relative)
        parts = relative.split('/')
        directories.update('/'.join(parts[:i]) for i in range(1, len(parts)))
    files, folders, queue = [], [], ['']
    while queue:
        relative = queue.pop()
        folder = child(backup, relative) if relative else backup
        folders.append(folder)
        for item in folder.iterdir():
            path = item.relative_to(backup).as_posix()
            require(len(files) + len(folders) + len(queue) < 256, 'Oversized backup folder.')
            checked = child(backup, path)
            if checked.is_dir():
                require(path in directories, 'Unknown backup folder.')
                queue.append(path)
            else:
                require(path in expected and checked.is_file() and checked.stat().st_size <= LIMIT,
                        'Unknown backup content.')
                files.append(checked)
    return backup, files, folders


def prune_backups(game):
    try:
        require(not game.is_symlink(), 'Linked game folder.')
        root = child(game, 'Addons/Backups')
        if not root.is_dir():
            return
        names = []
        for entry in root.iterdir():
            names.append(entry.name)
            if len(names) > 1024:
                return
        candidates = []
        for name in sorted(names, reverse=True):
            try:
                candidates.append(completed_backup(game, name))
            except (OSError, ValueError, TypeError, RecursionError, InstallError):
                continue
        kept, failed = 0, False
        for backup, files, _ in candidates:
            if len(files) > 1:
                kept += 1
                if kept <= 3:
                    continue
            try:
                backup, files, folders = completed_backup(game, backup.name)
                for path in sorted(files, key=lambda p: p.name == 'installation.json'):
                    child(game, path.relative_to(game).as_posix()).unlink()
                for path in reversed(folders):
                    child(game, path.relative_to(game).as_posix()).rmdir()
            except (OSError, ValueError, TypeError, RecursionError, InstallError):
                failed = True
        if failed:
            print('Some older addon backups could not be removed.', file=sys.stderr)
    except (OSError, ValueError, TypeError, RecursionError, InstallError):
        return


def install(game, package, manifest, check_running=game_stopped):
    files = validate_manifest(manifest)
    validate_client(game, manifest)
    check_running()
    for item in files:
        source, target = child(package, item['path']), child(game, item['path'])
        require(source.is_file() and source.stat().st_size <= LIMIT and file_hash(source) == item['sha256'],
                'Damaged installation file: ' + item['path'])
        require(not target.exists() or target.is_file(), 'An installation target is a directory.')
    metadata = read_json(child(package, 'Addons/Runtime/ui-resources.json'))
    cache = local_cache(game, metadata)
    local_files = {'Addons/Runtime/ui.bin': cache}
    license_path = child(game, LICENSE_PATH)
    require(not license_path.exists() or license_path.is_file(), 'The addon license target is a directory.')
    if not license_path.exists():
        local_files[LICENSE_PATH] = LICENSE_TEXT.encode('utf-8')
    loader, preserved = child(game, 'd3d9.dll'), child(game, 'd3d9.previous.dll')
    loader_hash = file_hash(loader) if loader.is_file() else None
    if loader_hash in manifest.get('chainLoaders', []):
        require(loader.stat().st_size <= LIMIT, 'The existing d3d9.dll is too large.')
        local_files['d3d9.previous.dll'] = loader.read_bytes()
        require(digest(local_files['d3d9.previous.dll']) == loader_hash, 'The existing d3d9.dll changed during installation.')
    if preserved.exists():
        require(preserved.is_file() and file_hash(preserved) in manifest.get('chainLoaders', []) and
                ('d3d9.previous.dll' not in local_files or file_hash(preserved) == loader_hash),
                'A different d3d9.previous.dll already exists. Both libraries were left untouched.')
    identity = time.strftime('%Y%m%d-%H%M%S', time.gmtime()) + '-' + uuid.uuid4().hex[:8]
    backup = child(game, 'Addons/Backups/' + identity)
    pending = child(game, 'Addons/Runtime/.install-' + identity)
    for relative in local_files:
        target = child(game, relative)
        require(not target.exists() or target.is_file(), 'A local addon data target is a directory.')
    ordered = [f for f in files if f['path'] != 'd3d9.dll'] + [
        {'path': path, 'sha256': digest(data)} for path, data in local_files.items()] + [f for f in files if f['path'] == 'd3d9.dll']
    if all(child(game, f['path']).is_file() and
            file_hash(child(game, f['path'])) == f['sha256'] for f in ordered):
        child(game, 'Addons/Update.sh').chmod(0o755)
        prune_backups(game)
        return 0
    backup.mkdir(parents=True)
    pending.mkdir(parents=True)
    changed = []
    try:
        for item in ordered:
            relative = item['path']
            target = child(game, relative)
            if relative == LICENSE_PATH and target.exists():
                require(target.is_file(), 'The addon license target is a directory.')
                continue
            if target.is_file() and file_hash(target) == item['sha256']:
                if relative.endswith('.sh'):
                    target.chmod(0o755)
                continue
            staged = child(pending, relative)
            saved = child(backup, relative)
            staged.parent.mkdir(parents=True, exist_ok=True)
            target.parent.mkdir(parents=True, exist_ok=True)
            if relative in local_files:
                staged.write_bytes(local_files[relative])
            else:
                shutil.copyfile(child(package, relative), staged)
            require(file_hash(staged) == item['sha256'], 'An installation file changed while being staged.')
            staged.chmod(0o755 if relative.endswith('.sh') else 0o644)
            exists = target.exists()
            if relative == LICENSE_PATH and exists:
                require(child(game, relative).is_file(), 'The addon license target is a directory.')
                continue
            if exists:
                saved.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(target, saved)
            check_running()
            if relative == 'd3d9.dll' and loader_hash:
                require(file_hash(loader) == loader_hash, 'The existing d3d9.dll changed during installation.')
            if relative == LICENSE_PATH:
                try:
                    with target.open('xb') as output:
                        changed.append((relative, False))
                        output.write(local_files[relative])
                except FileExistsError:
                    require(child(game, relative).is_file(), 'The addon license target is a directory.')
                continue
            os.replace(staged, target)
            changed.append((relative, exists))
        check_running()
        for item in ordered:
            if item['path'] == LICENSE_PATH and (LICENSE_PATH, False) not in changed:
                require(child(game, LICENSE_PATH).is_file(), 'The addon license is missing.')
                continue
            require(file_hash(child(game, item['path'])) == item['sha256'], 'Installed file verification failed.')
        (backup / 'installation.json').write_text(json.dumps({
            'version': manifest['version'], 'files': [p for p, _ in changed],
        }, indent=2) + '\n', encoding='utf-8')
    except BaseException as original:
        failures = []
        for relative, existed in reversed(changed):
            try:
                target = child(game, relative)
                if existed:
                    shutil.copy2(child(backup, relative), target)
                else:
                    target.unlink()
            except (OSError, InstallError):
                failures.append(relative)
        if failures:
            raise InstallError('Installation stopped. Some files could not be restored; backups are in Addons/Backups.') from original
        raise
    finally:
        shutil.rmtree(pending)
    prune_backups(game)
    return len(changed)


class SecureRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        parsed = urllib.parse.urlparse(new_url)
        require(parsed.scheme == 'https' and parsed.port in (None, 443) and not parsed.username and parsed.hostname in
                ('github.com', 'release-assets.githubusercontent.com', 'objects.githubusercontent.com'),
                'Unexpected download redirect.')
        return super().redirect_request(request, response, code, message, headers, new_url)


def request_bytes(url, limit):
    parsed = urllib.parse.urlparse(url)
    require(parsed.scheme == 'https' and not parsed.username and parsed.port in (None, 443) and
            parsed.hostname in ('api.github.com', 'github.com'), 'Invalid download address.')
    request = urllib.request.Request(url, headers={'User-Agent': 'Dungeon-Runners-Addons',
                                                  'Accept': 'application/vnd.github+json'})
    with urllib.request.build_opener(SecureRedirect()).open(request, timeout=90) as response:
        result = response.read(limit + 1)
    require(len(result) <= limit, 'Download exceeds the allowed size.')
    return result


def release_metadata():
    release = json.loads(request_bytes('https://api.github.com/repos/' + REPOSITORY + '/releases/latest', 1048576))
    require(isinstance(release, dict) and not release.get('draft') and not release.get('prerelease') and
            re.fullmatch(r'[vV][0-9][A-Za-z0-9._-]{0,63}', release.get('tag_name', '')) and
            isinstance(release.get('assets'), list) and len(release['assets']) <= 80, 'Invalid release metadata.')
    return release


def asset_bytes(release, name, checksum=None):
    require(re.fullmatch(r'[A-Za-z0-9_.-]{1,96}', name) is not None, 'Invalid release file name.')
    matches = [a for a in release['assets'] if a.get('name') == name]
    require(len(matches) == 1, 'Missing or duplicate release file: ' + name)
    asset = matches[0]
    url = 'https://github.com/' + REPOSITORY + '/releases/download/' + release['tag_name'] + '/' + name
    digest_value = asset.get('digest', '')
    limit = 65536 if name == 'linux-package.json' else LIMIT
    require(asset.get('browser_download_url') == url and type(asset.get('size')) is int and
            0 < asset['size'] <= limit and re.fullmatch(r'sha256:[0-9a-f]{64}', digest_value), 'Invalid release file metadata.')
    expected = digest_value[7:]
    require(checksum is None or checksum == expected, 'Release file checksum mismatch.')
    result = request_bytes(url, limit)
    require(len(result) == asset['size'] and digest(result) == expected, 'Downloaded file verification failed.')
    return result


def update(game, release=None, receive=asset_bytes, check_running=game_stopped, manifest=None):
    check_running()
    release = release or release_metadata()
    manifest = manifest or json.loads(receive(release, 'linux-package.json'))
    files = validate_manifest(manifest, release['tag_name'][1:])
    validate_client(game, manifest, updating=True)
    downloads = 0
    with tempfile.TemporaryDirectory(prefix='Dungeon-Runners-Addons-') as temporary:
        package = Path(temporary).resolve()
        for item in files:
            installed = child(game, item['path'])
            target = child(package, item['path'])
            target.parent.mkdir(parents=True, exist_ok=True)
            if installed.is_file() and file_hash(installed) == item['sha256']:
                shutil.copyfile(installed, target)
            else:
                target.write_bytes(receive(release, item['asset'], item['sha256']))
                downloads += 1
        install(game, package, manifest, check_running)
    return manifest['version'], downloads



def main():
    parser = argparse.ArgumentParser(description='Install or update Dungeon Runners Addons in the game folder')
    parser.add_argument('mode', choices=('install', 'update'), nargs='?', default='update')
    parser.add_argument('--client', help='Folder containing DungeonRunners.exe')
    parser.add_argument('--package', help='Extracted installer folder')
    arguments = parser.parse_args()
    try:
        require(sys.platform == 'linux', 'This installer is for Linux. Use the download matching your system.')
        require(sys.version_info >= (3, 8), 'Python 3.8 or newer is required.')
        require(os.geteuid() != 0, 'Run the installer as your normal user, without sudo.')
        game_stopped()
        location = Path(os.environ['DR_ADDONS_LAUNCHER']).resolve().parent
        candidate = location.parent if location.name == 'Addons' else location
        if arguments.client:
            game = Path(arguments.client).expanduser().resolve()
        elif arguments.mode == 'update' and (candidate/'DungeonRunners.exe').is_file():
            game = candidate
        else:
            game = select_path('Select the installed game folder containing DungeonRunners.exe.', find_games(prefix_candidates()))
        if arguments.mode == 'install':
            package = Path(arguments.package).resolve() if arguments.package else candidate
            manifest = read_json(child(package, 'linux-package.json'))
            release = None
        else:
            release = release_metadata()
            manifest = json.loads(asset_bytes(release, 'linux-package.json'))
        validate_manifest(manifest, release['tag_name'][1:] if release else None)
        validate_client(game, manifest, updating=arguments.mode == 'update')
        if arguments.mode == 'install':
            install(game, package, manifest)
            print('Installed V' + manifest['version'] + '. Start the game as usual, then open ESC > Addons.')
        else:
            version, count = update(game, release, manifest=manifest)
            print('V' + version + ': ' + str(count) + ' changed files downloaded. Settings and history were preserved.')
        return 0
    except KeyboardInterrupt:
        return 130
    except (InstallError, OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError, zlib.error) as error:
        message = str(error) if isinstance(error, InstallError) else 'Installation stopped. Check the selected game folder, file permissions and package.'
        if isinstance(error, urllib.error.URLError):
            message = 'Download failed. Check your network connection and system CA certificates.'
        print(message, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
PYTHON
