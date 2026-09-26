#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec /usr/bin/osascript -l JavaScript - "$base" "$@" <<'JAVASCRIPT'
ObjC.import('Foundation');

var fm = $.NSFileManager.defaultManager;
var repository = 'MaxmilianBaron/Dungeon-Runners-Addons';
var requiredFiles = ['d3d9.dll', 'Addons/Runtime/Addons.dll', 'Addons/Runtime/ui-resources.json',
    'Addons/DamageMeter/addon.ini', 'Addons/HideGoldLabels/addon.ini',
    'Addons/Runtime/macOS.py', 'Addons/Update.command'];
var licensePath = 'Addons/Licenses/LICENSE.txt';
var licenseText = "MIT License\n\nCopyright (c) 2026 MaxmilianBaron\n\nPermission is hereby granted, free of charge, to any person obtaining a copy\nof this software and associated documentation files (the \"Software\"), to deal\nin the Software without restriction, including without limitation the rights\nto use, copy, modify, merge, publish, distribute, sublicense, and/or sell\ncopies of the Software, and to permit persons to whom the Software is\nfurnished to do so, subject to the following conditions:\n\nThe above copyright notice and this permission notice shall be included in all\ncopies or substantial portions of the Software.\n\nTHE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\nIMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\nFITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\nAUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\nLIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\nOUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\nSOFTWARE.\n";
var limit = 16 * 1024 * 1024;

function requireValue(condition, message) {
    if (!condition) {
        var error = new Error(message);
        error.installation = true;
        throw error;
    }
}

function join(root, relative) { return root.replace(/\/$/, '') + '/' + relative; }
function parent(path) { return ObjC.unwrap($(path).stringByDeletingLastPathComponent); }
function resolve(path) { return ObjC.unwrap($(path).stringByExpandingTildeInPath.stringByStandardizingPath.stringByResolvingSymlinksInPath); }
function info(path) { return ObjC.deepUnwrap(fm.attributesOfItemAtPathError($(path), Ref())) || null; }
function regular(path) { var stat = info(path); return stat && stat.NSFileType === 'NSFileTypeRegular'; }
function directory(path) { var stat = info(path); return stat && stat.NSFileType === 'NSFileTypeDirectory'; }
function list(path) {
    var items = ObjC.deepUnwrap(fm.contentsOfDirectoryAtPathError($(path), Ref()));
    requireValue(Array.isArray(items), 'Cannot read the selected folder.');
    return items;
}

function mkdir(path) {
    requireValue(fm.createDirectoryAtPathWithIntermediateDirectoriesAttributesError($(path), true, $.NSDictionary.dictionary, Ref()),
        'Cannot create the addon folder. Check its permissions.');
}

function safePath(root, relative) {
    requireValue(typeof relative === 'string' && relative && !/[\\:]/.test(relative), 'Invalid installation path.');
    var parts = relative.split('/');
    requireValue(parts.every(function(p) { return p && p !== '.' && p !== '..'; }), 'Invalid installation path.');
    var current = root;
    parts.forEach(function(part) {
        if (directory(current)) {
            var matches = list(current).filter(function(name) { return name.toLowerCase() === part.toLowerCase(); });
            requireValue(!matches.length || (matches.length === 1 && matches[0] === part), 'Conflicting file name capitalization.');
        }
        current = join(current, part);
        var stat = info(current);
        requireValue(!stat || stat.NSFileType === 'NSFileTypeRegular' || stat.NSFileType === 'NSFileTypeDirectory',
            'Linked addon files or folders are not supported.');
    });
    return current;
}

function read(path, maximum) {
    var stat = info(path);
    requireValue(stat && stat.NSFileType === 'NSFileTypeRegular' && stat.NSFileSize <= maximum, 'Missing or oversized file.');
    var data = $.NSData.dataWithContentsOfFile($(path));
    requireValue(Number(data.length) === stat.NSFileSize, 'Could not read a required file.');
    return data;
}

function textData(value) { return $(value).dataUsingEncoding($.NSUTF8StringEncoding); }
function dataText(value) { return ObjC.unwrap($.NSString.alloc.initWithDataEncoding(value, $.NSUTF8StringEncoding)); }
function json(path, maximum) { return JSON.parse(dataText(read(path, maximum || 65536)).replace(/^\uFEFF/, '')); }
function write(path, data) {
    mkdir(parent(path));
    requireValue(data.writeToFileAtomically($(path), true), 'Could not save an addon file.');
}
function chmod(path, executable) {
    requireValue(fm.setAttributesOfItemAtPathError($({NSFilePosixPermissions: executable ? 493 : 420}), $(path), Ref()),
        'Could not set addon file permissions.');
}
function remove(path) {
    requireValue(fm.removeItemAtPathError($(path), Ref()), 'Could not remove a temporary addon file.');
}
function temporary() {
    var path = join(resolve(ObjC.unwrap($.NSTemporaryDirectory())), 'Dungeon-Runners-Addons-' + ObjC.unwrap($.NSUUID.UUID.UUIDString));
    requireValue(!info(path), 'Temporary folder already exists.');
    mkdir(path);
    requireValue(fm.setAttributesOfItemAtPathError($({NSFilePosixPermissions: 448}), $(path), Ref()), 'Could not secure the temporary folder.');
    return path;
}

function execute(executable, args, input) {
    var working = temporary();
    var task = $.NSTask.alloc.init;
    var stdout, stderr;
    try {
        var outPath = join(working, 'stdout'), errPath = join(working, 'stderr');
        write(outPath, $.NSData.data);
        write(errPath, $.NSData.data);
        stdout = $.NSFileHandle.fileHandleForWritingAtPath($(outPath));
        stderr = $.NSFileHandle.fileHandleForWritingAtPath($(errPath));
        task.launchPath = $(executable);
        task.arguments = $(args);
        task.standardOutput = stdout;
        task.standardError = stderr;
        var pipe = $.NSPipe.pipe;
        task.standardInput = pipe;
        requireValue(task.launchAndReturnError(Ref()), 'Could not start a required system tool.');
        if (input) pipe.fileHandleForWriting.writeData(input);
        pipe.fileHandleForWriting.closeFile;
        var deadline = Date.now() + 95000;
        while (task.running && Date.now() < deadline) $.NSThread.sleepForTimeInterval(0.05);
        if (task.running) {
            task.terminate;
            requireValue(false, 'A system tool did not finish in time.');
        }
        task.waitUntilExit;
        stdout.closeFile;
        stderr.closeFile;
        return {status: Number(task.terminationStatus), output: dataText(read(outPath, 2097152)), error: dataText(read(errPath, 2097152))};
    } finally {
        if (task.running) task.terminate;
        if (stdout) stdout.closeFile;
        if (stderr) stderr.closeFile;
        remove(working);
    }
}

function hash(data, algorithm) {
    var result = execute('/usr/bin/shasum', ['-a', algorithm || '256'], data);
    requireValue(result.status === 0 && /^[a-f0-9]+\s/.test(result.output), 'Could not verify a file checksum.');
    return result.output.split(/\s/)[0];
}
function fileHash(path, algorithm) {
    var result = execute('/usr/bin/shasum', ['-a', algorithm || '256', path]);
    requireValue(result.status === 0 && /^[a-f0-9]+\s/.test(result.output), 'Could not verify a file checksum.');
    return result.output.split(/\s/)[0];
}

function manifestFiles(manifest, version) {
    requireValue(manifest && manifest.schema === 1 && manifest.platform === 'macos' && typeof manifest.version === 'string' &&
        /^[0-9][A-Za-z0-9._-]{0,63}$/.test(manifest.version) && (!version || manifest.version === version), 'Unsupported macOS package.');
    ['clients', 'loaders'].forEach(function(key) {
        var values = manifest[key];
        requireValue(Array.isArray(values) && values.length && values.length <= 64 && values.every(function(v) {
            return typeof v === 'string' && /^[a-f0-9]{64}$/.test(v);
        }), 'Invalid compatibility list.');
    });
    var chains = manifest.chainLoaders === undefined ? [] : manifest.chainLoaders;
    requireValue(Array.isArray(chains) && chains.length <= 64 && chains.every(function(v) {
        return typeof v === 'string' && /^[a-f0-9]{64}$/.test(v);
    }), 'Invalid graphics compatibility list.');
    var files = manifest.files, paths = [], assets = [];
    requireValue(Array.isArray(files) && files.length >= requiredFiles.length && files.length <= 64, 'Invalid file list.');
    files.forEach(function(file) {
        requireValue(file && typeof file.path === 'string' && (requiredFiles.indexOf(file.path) >= 0 ||
            /^Addons\/[A-Za-z0-9_-]+\/(addon\.ini|Addon\.dll)$/.test(file.path)) &&
            typeof file.sha256 === 'string' && /^[a-f0-9]{64}$/.test(file.sha256) &&
            typeof file.asset === 'string' && /^[A-Za-z0-9_.-]{1,96}$/.test(file.asset), 'Invalid package file.');
        requireValue(paths.indexOf(file.path.toLowerCase()) < 0 && assets.indexOf(file.asset.toLowerCase()) < 0, 'Duplicate package file.');
        paths.push(file.path.toLowerCase());
        assets.push(file.asset.toLowerCase());
    });
    requireValue(requiredFiles.every(function(path) { return paths.indexOf(path.toLowerCase()) >= 0; }), 'Incomplete macOS package.');
    return files;
}

function validateClient(game, manifest, updating) {
    var exe = safePath(game, 'DungeonRunners.exe'), loader = safePath(game, 'd3d9.dll');
    requireValue(regular(exe) && manifest.clients.indexOf(fileHash(exe)) >= 0, 'Select a supported folder containing DungeonRunners.exe.');
    requireValue(!updating || regular(loader), 'The updater requires an existing addon installation. Use the installer first.');
    requireValue(!info(loader) || (regular(loader) && manifest.loaders.concat(manifest.chainLoaders || []).indexOf(fileHash(loader)) >= 0),
        'This d3d9.dll is not supported for coexistence. It was left untouched.');
}

function bytes(data) {
    var encoded = ObjC.unwrap(data.base64EncodedStringWithOptions(0)), alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    var result = [], bits = 0, value = 0;
    for (var i = 0; i < encoded.length && encoded[i] !== '='; i++) {
        value = (value << 6) | alphabet.indexOf(encoded[i]);
        bits += 6;
        if (bits >= 8) { bits -= 8; result.push((value >>> bits) & 255); }
    }
    return result;
}
function byteData(values) {
    var alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/', result = '';
    for (var i = 0; i < values.length; i += 3) {
        var value = (values[i] << 16) | ((values[i + 1] || 0) << 8) | (values[i + 2] || 0);
        result += alphabet[(value >>> 18) & 63] + alphabet[(value >>> 12) & 63] +
            (i + 1 < values.length ? alphabet[(value >>> 6) & 63] : '=') + (i + 2 < values.length ? alphabet[value & 63] : '=');
    }
    return $.NSData.alloc.initWithBase64EncodedStringOptions($(result), 0);
}
function slice(data, offset, length) {
    requireValue(offset >= 0 && length >= 0 && offset + length <= Number(data.length), 'Invalid resource range.');
    return data.subdataWithRange($.NSMakeRange(offset, length));
}
function integer32(values, offset) { return (values[offset] | (values[offset + 1] << 8) | (values[offset + 2] << 16) | (values[offset + 3] << 24)) >>> 0; }

function localCache(game, metadata) {
    requireValue(fileHash(safePath(game, 'game.pki'), '1') === metadata.pkiSha1, 'This game data version is not supported.');
    requireValue(Array.isArray(metadata.entries) && metadata.entries.length === 7, 'Invalid resource list.');
    var packagePath = safePath(game, 'game.pkg'), packageInfo = info(packagePath), resources = Object.create(null);
    requireValue(packageInfo && regular(packagePath), 'Missing game resources.');
    var stream = $.NSFileHandle.fileHandleForReadingAtPath($(packagePath));
    try {
        metadata.entries.forEach(function(entry) {
            var offset = entry.package_offset, size = entry.stored_size, decodedSize = entry.decoded_size;
            requireValue(typeof entry.name === 'string' && !resources[entry.name] &&
                [offset, size, decodedSize].every(function(n) { return Number.isSafeInteger(n); }) &&
                size > 0 && size <= 2097152 && decodedSize > 0 && decodedSize <= 2097152 &&
                offset >= 0 && offset <= packageInfo.NSFileSize - size, 'Invalid UI resource range.');
            stream.seekToFileOffset(offset);
            var raw = stream.readDataOfLength(size);
            requireValue(Number(raw.length) === size && hash(raw) === entry.storedSha256, 'Game resource checksum mismatch.');
            if (entry.flags & 1) {
                var header = bytes(slice(raw, 0, 2));
                requireValue(size > 6 && (header[0] & 15) === 8 && !(header[1] & 32) &&
                    ((header[0] << 8) | header[1]) % 31 === 0, 'Invalid compressed UI resource.');
                raw = slice(raw, 2, size - 6).decompressedDataUsingAlgorithmError($.NSDataCompressionAlgorithmZlib, Ref());
            }
            requireValue(Number(raw.length) === decodedSize && hash(raw) === entry.decodedSha256, 'Decoded UI resource checksum mismatch.');
            resources[entry.name] = raw;
        });
    } finally { stream.closeFile; }
    var components = [];
    [['InGameUI4', 1024], ['NewUI', 512], ['Font_Outline', 512]].forEach(function(texture) {
        var raw = resources[texture[0]], side = texture[1];
        requireValue(raw && Number(raw.length) >= 128 + side * side, 'Missing UI texture.');
        var header = bytes(slice(raw, 0, 128));
        requireValue(dataText(slice(raw, 0, 4)) === 'DDS ' && dataText(slice(raw, 84, 4)) === 'DXT3' &&
            integer32(header, 12) === side && integer32(header, 16) === side, 'Unsupported texture layout.');
        components.push(slice(raw, 128, side * side));
    });
    var metrics = resources['fonts\\Font_Outline_Metrics'];
    requireValue(metrics && Number(metrics.length) === 512 && resources.sylfaen, 'Unsupported font resources.');
    var original = bytes(metrics), converted = [];
    for (var i = 0; i < 512; i += 2) converted.push(((original[i] | (original[i + 1] << 8)) + 2) & 255);
    components.push(byteData(converted), resources.sylfaen);
    var sizes = [];
    components.forEach(function(data) {
        var length = Number(data.length);
        for (var i = 0; i < 4; i++) sizes.push((length >>> (i * 8)) & 255);
    });
    var result = $.NSMutableData.data;
    result.appendData(textData('DRUI0001'));
    result.appendData(byteData(sizes));
    components.forEach(function(data) { result.appendData(data); });
    requireValue(Number(result.length) === metadata.size && hash(result) === metadata.sha256, 'Local UI cache verification failed.');
    return result;
}

function gameStopped() {
    var processes = execute('/bin/ps', ['-axo', 'command=']);
    requireValue(processes.status === 0, 'Could not check running applications.');
    requireValue(!/dungeonrunners(?:118)?\.exe/i.test(processes.output), 'Close Dungeon Runners before installing or updating addons.');
}

function completedBackup(game, name) {
    requireValue(/^[0-9]{17}-[A-Fa-f0-9]{8}-(?:[A-Fa-f0-9]{4}-){3}[A-Fa-f0-9]{12}$/.test(name), 'Unknown backup name.');
    var backup = safePath(game, 'Addons/Backups/' + name);
    requireValue(directory(backup), 'Missing backup folder.');
    var metadata = json(safePath(backup, 'installation.json'));
    requireValue(metadata && typeof metadata.version === 'string' && /^[0-9][A-Za-z0-9._-]{0,63}$/.test(metadata.version) &&
        Array.isArray(metadata.files) && metadata.files.length <= 67, 'Unknown backup metadata.');
    var allowed = [licensePath, 'd3d9.dll', 'd3d9.previous.dll', 'Addons/Update.command', 'Addons/Update.sh', 'Addons/Update.cmd',
        'Addons/Runtime/Addons.dll', 'Addons/Runtime/ui.bin', 'Addons/Runtime/ui-resources.json',
        'Addons/Runtime/Update.ps1', 'Addons/Runtime/macOS.py', 'Addons/DamageMeter/DamageMeter.dll',
        'Addons/DamageMeter/Dear-ImGui-LICENSE.txt', 'Addons/DamageMeter/MinHook-LICENSE.txt'];
    var expected = ['installation.json'], directories = [''];
    metadata.files.forEach(function(relative) {
        requireValue(typeof relative === 'string' && (allowed.indexOf(relative) >= 0 ||
            /^Addons\/[A-Za-z0-9_-]+\/addon\.ini$/.test(relative)) && expected.indexOf(relative) < 0, 'Unknown backup file.');
        expected.push(relative);
        var parts = relative.split('/');
        for (var i = 1; i < parts.length; i++) directories.push(parts.slice(0, i).join('/'));
    });
    var files = [], folders = [], queue = [''];
    while (queue.length) {
        var relative = queue.pop(), folder = relative ? safePath(backup, relative) : backup;
        folders.push(folder);
        list(folder).forEach(function(name) {
            var path = relative ? relative + '/' + name : name;
            requireValue(files.length + folders.length + queue.length < 256, 'Oversized backup folder.');
            var checked = safePath(backup, path);
            if (directory(checked)) {
                requireValue(directories.indexOf(path) >= 0, 'Unknown backup folder.');
                queue.push(path);
            } else {
                requireValue(expected.indexOf(path) >= 0 && regular(checked) && info(checked).NSFileSize <= limit, 'Unknown backup content.');
                files.push(checked);
            }
        });
    }
    return {path: backup, name: name, files: files, folders: folders};
}

function pruneBackups(game) {
    try {
        requireValue(directory(game), 'Linked game folder.');
        var root = safePath(game, 'Addons/Backups');
        if (!directory(root)) return;
        var names = list(root);
        if (names.length > 1024) return;
        var candidates = [];
        names.sort().reverse().forEach(function(name) {
            try { candidates.push(completedBackup(game, name)); } catch (error) { }
        });
        var kept = 0, failed = false;
        candidates.forEach(function(candidate) {
            if (candidate.files.length > 1 && ++kept <= 3) return;
            try {
                candidate = completedBackup(game, candidate.name);
                candidate.files.sort(function(a, b) { return Number(a.endsWith('/installation.json')) - Number(b.endsWith('/installation.json')); });
                candidate.files.forEach(function(path) {
                    var checked = safePath(game, path.slice(game.length + 1));
                    requireValue(checked.indexOf(root + '/') === 0 && regular(checked), 'Backup is outside its folder.');
                    remove(checked);
                });
                candidate.folders.reverse().forEach(function(path) {
                    var checked = safePath(game, path.slice(game.length + 1));
                    requireValue(checked.indexOf(root + '/') === 0 && directory(checked) && !list(checked).length, 'Backup folder is not empty.');
                    remove(checked);
                });
            } catch (error) { failed = true; }
        });
        if (failed) console.log('Some older addon backups could not be removed.');
    } catch (error) { }
}

function install(game, packageRoot, manifest, checkRunning) {
    checkRunning = checkRunning || gameStopped;
    var files = manifestFiles(manifest);
    validateClient(game, manifest, false);
    checkRunning();
    files.forEach(function(file) {
        var target = safePath(game, file.path), source = safePath(packageRoot, file.path);
        requireValue(hash(read(source, limit)) === file.sha256, 'Damaged installation file: ' + file.path);
        requireValue(!info(target) || regular(target), 'An installation target is a directory.');
    });
    var cache = localCache(game, json(safePath(packageRoot, 'Addons/Runtime/ui-resources.json')));
    var cachePath = safePath(game, 'Addons/Runtime/ui.bin');
    requireValue(!info(cachePath) || regular(cachePath), 'The UI cache target is a directory.');
    var localFiles = {'Addons/Runtime/ui.bin': cache};
    var licenseTarget = safePath(game, licensePath);
    requireValue(!info(licenseTarget) || regular(licenseTarget), 'The addon license target is a directory.');
    if (!info(licenseTarget)) localFiles[licensePath] = textData(licenseText);
    var loader = safePath(game, 'd3d9.dll'), preserved = safePath(game, 'd3d9.previous.dll');
    var loaderHash = regular(loader) ? fileHash(loader) : null;
    if ((manifest.chainLoaders || []).indexOf(loaderHash) >= 0) {
        localFiles['d3d9.previous.dll'] = read(loader, limit);
        requireValue(hash(localFiles['d3d9.previous.dll']) === loaderHash, 'The existing d3d9.dll changed during installation.');
    }
    if (info(preserved)) {
        requireValue(regular(preserved) && (manifest.chainLoaders || []).indexOf(fileHash(preserved)) >= 0 &&
            (!localFiles['d3d9.previous.dll'] || fileHash(preserved) === loaderHash),
            'A different d3d9.previous.dll already exists. Both libraries were left untouched.');
    }
    var ordered = files.filter(function(f) { return f.path !== 'd3d9.dll'; }).concat(Object.keys(localFiles).map(function(path) {
        return {path: path, sha256: hash(localFiles[path])};
    })).concat(files.filter(function(f) { return f.path === 'd3d9.dll'; }));
    var pending = ordered.filter(function(file) { var path = safePath(game, file.path); return !regular(path) || fileHash(path) !== file.sha256; });
    if (!pending.length) { chmod(safePath(game, 'Addons/Update.command'), true); pruneBackups(game); return 0; }
    var backup = safePath(game, 'Addons/Backups/' + new Date().toISOString().replace(/[^0-9]/g, '') + '-' + ObjC.unwrap($.NSUUID.UUID.UUIDString));
    mkdir(backup);
    var changed = [];
    try {
        pending.forEach(function(file) {
            var target = safePath(game, file.path), saved = safePath(backup, file.path), existed = regular(target);
            if (file.path === licensePath && info(target)) {
                requireValue(regular(target), 'The addon license target is a directory.');
                return;
            }
            if (existed) { write(saved, read(target, limit)); chmod(saved, file.path.endsWith('.command')); }
            checkRunning();
            var data = localFiles[file.path] || read(safePath(packageRoot, file.path), limit);
            requireValue(hash(data) === file.sha256, 'An installation file changed while being staged.');
            if (file.path === 'd3d9.dll' && loaderHash) {
                requireValue(fileHash(loader) === loaderHash, 'The existing d3d9.dll changed during installation.');
            }
            if (file.path === licensePath) {
                var staged = safePath(backup, '.license-pending');
                write(staged, data);
                try {
                    mkdir(parent(target));
                    if (!fm.moveItemAtPathToPathError($(staged), $(target), Ref())) {
                        requireValue(regular(safePath(game, licensePath)), 'Could not save the addon license.');
                        return;
                    }
                } finally { if (info(staged)) remove(staged); }
                changed.push({path: file.path, existed: false});
            } else {
                changed.push({path: file.path, existed: existed});
                write(target, data);
            }
            chmod(target, file.path.endsWith('.command'));
        });
        checkRunning();
        ordered.forEach(function(file) {
            if (file.path === licensePath && !changed.some(function(item) { return item.path === licensePath; })) {
                requireValue(regular(safePath(game, licensePath)), 'The addon license is missing.');
                return;
            }
            requireValue(fileHash(safePath(game, file.path)) === file.sha256, 'Installed file verification failed.');
        });
        chmod(safePath(game, 'Addons/Update.command'), true);
        write(join(backup, 'installation.json'), textData(JSON.stringify({version: manifest.version,
            files: changed.map(function(file) { return file.path; })}, null, 2) + '\n'));
    } catch (error) {
        var failures = [];
        changed.reverse().forEach(function(file) {
            try {
                var target = safePath(game, file.path);
                if (file.existed) { write(target, read(safePath(backup, file.path), limit)); chmod(target, file.path.endsWith('.command')); }
                else if (info(target)) remove(target);
            } catch (restoreError) { failures.push(file.path); }
        });
        requireValue(!failures.length, 'Installation stopped. Some files could not be restored; backups are in Addons/Backups.');
        throw error;
    }
    pruneBackups(game);
    return changed.length;
}

function requestBytes(url, maximum) {
    var workspace = temporary();
    try {
        for (var redirects = 0; redirects <= 5; redirects++) {
            requireValue(/^https:\/\/(api\.github\.com|github\.com|release-assets\.githubusercontent\.com|objects\.githubusercontent\.com)\//.test(url) && !/[\r\n]/.test(url),
                'Unexpected download address.');
            var target = join(workspace, 'download'), headers = join(workspace, 'headers');
            var result = execute('/usr/bin/curl', ['--silent', '--show-error', '--proto', '=https', '--connect-timeout', '20',
                '--max-time', '90', '--max-filesize', String(maximum), '--user-agent', 'Dungeon-Runners-Addons',
                '--header', 'Accept: application/vnd.github+json', '--dump-header', headers, '--output', target, '--write-out', '%{http_code}', url]);
            requireValue(result.status === 0, 'Download failed. Check your internet connection and try again.');
            var status = Number(result.output);
            if ([301, 302, 303, 307, 308].indexOf(status) >= 0) {
                var location = /^location:\s*(.+?)\s*$/im.exec(dataText(read(headers, 65536)));
                requireValue(location, 'Missing download redirect.');
                url = location[1];
                continue;
            }
            requireValue(status === 200, status === 403 || status === 429 ? 'GitHub download limit reached. Try again later.' : 'The release file could not be downloaded.');
            return read(target, maximum);
        }
        requireValue(false, 'Too many download redirects.');
    } finally { remove(workspace); }
}

function releaseMetadata() {
    var release = JSON.parse(dataText(requestBytes('https://api.github.com/repos/' + repository + '/releases/latest', 1048576)));
    requireValue(release && !release.draft && !release.prerelease && /^[vV][0-9][A-Za-z0-9._-]{0,63}$/.test(release.tag_name) &&
        Array.isArray(release.assets) && release.assets.length <= 80, 'Invalid release metadata.');
    return release;
}
function assetBytes(release, name, checksum) {
    var matches = release.assets.filter(function(asset) { return asset.name === name; });
    requireValue(/^[A-Za-z0-9_.-]{1,96}$/.test(name) && matches.length === 1, 'Missing or duplicate release file.');
    var asset = matches[0], url = 'https://github.com/' + repository + '/releases/download/' + release.tag_name + '/' + name;
    var maximum = name === 'macOS-package.json' ? 65536 : limit;
    requireValue(asset.browser_download_url === url && Number.isSafeInteger(asset.size) && asset.size > 0 && asset.size <= maximum &&
        typeof asset.digest === 'string' && /^sha256:[0-9a-f]{64}$/.test(asset.digest), 'Invalid release file metadata.');
    var expected = asset.digest.slice(7);
    requireValue(!checksum || checksum === expected, 'Release file checksum mismatch.');
    var data = requestBytes(url, maximum);
    requireValue(Number(data.length) === asset.size && hash(data) === expected, 'Downloaded file verification failed.');
    return data;
}
function update(game, release, receive, checkRunning) {
    checkRunning = checkRunning || gameStopped;
    receive = receive || assetBytes;
    checkRunning();
    release = release || releaseMetadata();
    var manifest = JSON.parse(dataText(receive(release, 'macOS-package.json')));
    var files = manifestFiles(manifest, release.tag_name.slice(1));
    validateClient(game, manifest, true);
    var workspace = temporary(), downloads = 0;
    try {
        files.forEach(function(file) {
            var installed = safePath(game, file.path), data;
            if (regular(installed) && fileHash(installed) === file.sha256) data = read(installed, limit);
            else { data = receive(release, file.asset, file.sha256); downloads++; }
            write(safePath(workspace, file.path), data);
        });
        install(game, workspace, manifest, checkRunning);
    } finally { remove(workspace); }
    return {version: manifest.version, count: downloads};
}

function findGames() {
    var prefixes = [resolve('~/.wine')], bottles = resolve('~/Library/Application Support/CrossOver/Bottles');
    if (directory(bottles)) list(bottles).forEach(function(name) { prefixes.push(join(bottles, name)); });
    var queue = [], found = [], visited = 0;
    prefixes.forEach(function(prefix) {
        var drive = join(prefix, 'drive_c');
        if (regular(join(prefix, 'user.reg')) && regular(join(prefix, 'system.reg')) && directory(drive)) queue.push({path: drive, depth: 0});
    });
    while (queue.length && visited++ < 5000) {
        var item = queue.shift();
        if (regular(join(item.path, 'DungeonRunners.exe'))) { found.push(item.path); continue; }
        if (item.depth >= 6) continue;
        try {
            list(item.path).forEach(function(name) {
                var path = join(item.path, name);
                if (name[0] !== '.' && name.toLowerCase() !== 'windows' && directory(path)) queue.push({path: path, depth: item.depth + 1});
            });
        } catch (error) { if (!error.installation) throw error; }
    }
    return found;
}

function chooseGame(app) {
    var games = findGames();
    if (games.length) {
        var items = games.concat(['Choose another folder...']);
        var selected = app.chooseFromList(items, {withTitle: 'Dungeon Runners Addons',
            withPrompt: 'Select your existing Dungeon Runners installation.', defaultItems: [items[0]],
            okButtonName: 'Select', cancelButtonName: 'Cancel'});
        if (!selected) throw {number: -128};
        if (selected[0] !== items[items.length - 1]) return selected[0];
    }
    return resolve(app.chooseFolder({withPrompt: 'Select your installed Dungeon Runners folder (containing DungeonRunners.exe).'}).toString());
}

function run(argv) {
    var app = Application.currentApplication();
    app.includeStandardAdditions = true;
    try {
        var base = resolve(argv.shift()), mode = 'update', options = {};
        if (argv.length && /^(install|update)$/.test(argv[0])) mode = argv.shift();
        while (argv.length) {
            var key = argv.shift();
            requireValue(['--client', '--package'].indexOf(key) >= 0 && argv.length, 'Unsupported installer argument.');
            options[key] = argv.shift();
        }
        var system = $.NSProcessInfo.processInfo.operatingSystemVersion;
        requireValue(system.majorVersion > 10 || (system.majorVersion === 10 && system.minorVersion >= 15), 'This installer requires macOS 10.15 or newer.');
        gameStopped();
        var candidate = parent(base), game;
        if (options['--client']) game = resolve(options['--client']);
        else if (mode === 'update' && regular(join(candidate, 'DungeonRunners.exe'))) game = candidate;
        else game = chooseGame(app);
        var packageRoot, manifest;
        if (mode === 'install') {
            packageRoot = options['--package'] ? resolve(options['--package']) : candidate;
            manifest = json(safePath(packageRoot, 'macOS-package.json'));
            manifestFiles(manifest);
            validateClient(game, manifest, false);
        } else requireValue(regular(safePath(game, 'd3d9.dll')), 'The updater requires an existing addon installation. Use the installer first.');
        var message;
        if (mode === 'install') {
            install(game, packageRoot, manifest);
            message = 'Installed V' + manifest.version + '. Start the game as usual, then open ESC > Addons.';
        } else {
            var result = update(game);
            message = 'V' + result.version + ': ' + result.count + ' changed files downloaded. Settings and history were preserved.';
        }
        if (!options['--client']) app.displayDialog(message, {withTitle: 'Dungeon Runners Addons', buttons: ['OK'], defaultButton: 'OK'});
        return message;
    } catch (error) {
        if (error.number === -128) return;
        var message = error.installation ? error.message : 'Installation stopped. Check the package, file access and your connection.';
        if (!options || !options['--client']) app.displayAlert('Dungeon Runners Addons', {message: message, as: 'critical'});
        throw new Error(message);
    }
}
JAVASCRIPT
