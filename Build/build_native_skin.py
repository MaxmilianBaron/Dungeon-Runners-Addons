import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

from catalog import EXPECTED_PKI_SHA1, parse_index

EXPECTED_RESOURCES = {
    "Button": "c3034c5692b964db5fbcc39567af308ce81ad433a1bc91a0a87e619026d98e97",
    "Frame": "d7dd1c8fb8178de3584e3311e4237b1520719e20b76f7edc10c2def0382d491f",
    "Font": "f035e63c54b85ea184b4fd2e38acd28ac1e3583084a3e407293b15190c0c3a70",
    "Metrics": "d5c875a1268af8bc09a702418d5ca8b5a5b8b14b4dfcf528c9ba8ae53a148194",
    "Menu": "f110359f162eef5113ccc41cbae51dd60ca964e382fc27d05673140c62cab247",
    "Options": "971d3a4448cad401c636d8a864677783fccc48d6a8fd4363934dd2db61f4c63c",
    "Sylfaen": "992a6f0d72549748478d38feb993c14cdf451e51e5d278fcceb0bdb115e72d38",
    "Well": "0ce6b1c4cc674f4120d729dd0e2f9b5c74dbd816377a29d4a0fcf1b9c427471e",
    "WellReady": "02b56ee6529d0a7d2514bbe6f6beae7c2ceb20ea3d6cf0a4aded0bd677f424b3",
}


def build(directory, output):
    directory, output = Path(directory), Path(output)
    index = directory / "game.pki"
    if hashlib.sha1(index.read_bytes()).hexdigest() != EXPECTED_PKI_SHA1:
        raise ValueError("Unsupported UI package index")
    _, _, entries = parse_index(index)
    wanted = {(2, "InGameUI4"): "Button", (2, "NewUI"): "Frame", (2, "Font_Outline"): "Font", (17, "fonts\\Font_Outline_Metrics"): "Metrics", (9, "InGameMenu"): "Menu"}
    wanted.update({(9, "Options"): "Options", (3, "sylfaen"): "Sylfaen"})
    wanted.update({(2, "mapicon_wishingwell"): "Well", (2, "Mystery_Wishing_Well_Icon"): "WellReady"})
    evidence, data = [], {}
    with (directory / "game.pkg").open("rb") as package:
        for entry in entries:
            name = wanted.get((entry["type_code"], entry["name"]))
            if not name:
                continue
            package.seek(entry["package_offset"])
            stored = package.read(entry["stored_size"])
            decoded = zlib.decompress(stored) if entry["flags"] & 1 else stored
            if len(stored) != entry["stored_size"] or len(decoded) != entry["decoded_size"]:
                raise ValueError("Incomplete UI resource")
            if name in data or hashlib.sha256(decoded).hexdigest() != EXPECTED_RESOURCES[name]:
                raise ValueError("Unsupported native UI resource: " + name)
            data[name] = decoded
            row = {key: entry[key] for key in ("entry_id", "type_code", "name", "package_offset", "stored_size", "decoded_size", "flags")}
            row.update(storedSha256=hashlib.sha256(stored).hexdigest(), decodedSha256=hashlib.sha256(decoded).hexdigest())
            evidence.append(row)
    if len(data) != len(wanted) or len(data["Metrics"]) != 512:
        raise ValueError("Missing native UI resource")
    components = []
    lines = ["#pragma once", "#include <cstdint>"]
    for name in ("Button", "Frame", "Font"):
        raw = data[name]
        height, width = struct.unpack_from("<II", raw, 12)
        if raw[:4] != b"DDS " or raw[84:88] != b"DXT3" or width != height or width not in (512, 1024):
            raise ValueError("Unexpected native texture format")
        pixels = raw[128:128 + width * height]
        if len(pixels) != width * height:
            raise ValueError("Truncated texture")
        components.append(pixels)
        lines.append(f"constexpr unsigned Skin{name}Size = {width};")
    metrics = struct.unpack("<256H", data["Metrics"])
    if max(metrics[32:127]) > 30:
        raise ValueError("Unexpected font metrics")
    components.append(bytes((m + 2) & 255 for m in metrics))
    if data["Sylfaen"][:4] != bytes((0,1,0,0)):
        raise ValueError("Unexpected value font")
    components.append(data["Sylfaen"])
    legacy = b"DRUI0001" + struct.pack("<5I", *(len(c) for c in components)) + b"".join(components)
    components.extend((data["Well"], data["WellReady"]))
    blob = b"DRUI0002" + struct.pack("<7I", *(len(c) for c in components)) + b"".join(components)
    digest = hashlib.sha256(blob).hexdigest()
    lines.extend((f"constexpr unsigned SkinFileSize = {len(blob)};", f'static const char* SkinFileSha256 = "{digest}";'))
    lines.extend((f"constexpr unsigned SkinLegacyFileSize = {len(legacy)};", f'static const char* SkinLegacyFileSha256 = "{hashlib.sha256(legacy).hexdigest()}";'))
    output.mkdir(parents=True, exist_ok=True)
    (output / "ui.bin").write_bytes(blob)
    (output / "native_skin.generated.h").write_text("\n".join(lines)+"\n",encoding="ascii")
    resources = dict(pkiSha1=EXPECTED_PKI_SHA1, sha256=digest, size=len(blob), entries=evidence)
    (output / "ui-resources.json").write_text(json.dumps(resources,indent=2)+"\n",encoding="utf-8")
    (output / "native-skin-provenance.json").write_text(json.dumps({"pkiSha1": EXPECTED_PKI_SHA1, "entries": evidence}, indent=2) + "\n")


if __name__ == "__main__":
    build(*sys.argv[1:])
