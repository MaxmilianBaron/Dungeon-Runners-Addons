from __future__ import annotations

import hashlib
import re
import zlib
from pathlib import Path

from package_index import EXPECTED_PKI_SHA1, parse_index
from definitions import Parser, normalize_path


def collect_labels(documents, combat_only=False):
    roots = {}
    kinds = {}
    names = {}
    for path, text in documents:
        root = Parser(text).parse()
        label = ""
        for child in root.children:
            if child.name.lower() == "description":
                label = dict(child.properties).get("Label", "")
                break
        roots[normalize_path(path).lower()] = (label, normalize_path(root.extends_path).lower())
        kinds[normalize_path(path).lower()] = normalize_path(root.extends_path).lower()
        names[normalize_path(path).lower()] = normalize_path(path).rsplit(".", 1)[-1]
    result = {}
    for path in roots:
        if combat_only:
            current = path
            seen = set()
            eligible = path.startswith("skills.") or path.startswith("items.procs.") or ".skills." in path
            while not eligible and current in kinds and current not in seen:
                seen.add(current)
                current = kinds[current]
                eligible = current in {"activeskill", "passiveskill", "procmodifier"}
            if not eligible:
                continue
        current = path
        seen = set()
        while current in roots and current not in seen and len(seen) < 32:
            seen.add(current)
            label, current = roots[current]
            if label:
                result[path] = label
                break
        if combat_only and path not in result:
            result[path] = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", names[path]).replace("_", " ")
    return result


def load_catalog(client_directory):
    directory = Path(client_directory)
    index = directory / "game.pki"
    digest = hashlib.sha1(index.read_bytes()).hexdigest()
    if digest != EXPECTED_PKI_SHA1:
        raise ValueError("Unsupported game.pki; skill names require the verified data version")
    _, _, entries = parse_index(index)
    documents = []
    evidence = []
    with (directory / "game.pkg").open("rb") as package:
        for entry in entries:
            if entry["type_code"] != 13:
                continue
            package.seek(entry["package_offset"])
            stored = package.read(entry["stored_size"])
            decoded = zlib.decompress(stored) if entry["flags"] & 1 else stored
            if len(stored) != entry["stored_size"] or len(decoded) != entry["decoded_size"]:
                raise ValueError("Truncated skill definition: " + entry["name"])
            documents.append((entry["name"], decoded.decode("cp1252")))
            evidence.append({key: entry[key] for key in ("entry_id", "type_code", "name", "package_offset", "stored_size", "decoded_size", "flags")})
            evidence[-1].update(storedSha256=hashlib.sha256(stored).hexdigest(), decodedSha256=hashlib.sha256(decoded).hexdigest())
    from critical_catalog import critical_catalog
    return {"labels": collect_labels(documents, combat_only=True), "critical": critical_catalog(documents), "pkiSha1": digest, "entries": evidence}


def dungeon_family(name):
    name = name.lower()
    match = re.match(r"(?:dungeon|d)(\d{2})_", name)
    if match:
        return "dungeon" + match[1]
    match = re.match(r"((?:elite|epic)\d{2})_", name)
    if match:
        return match[1]
    return re.sub(r"_level\d+$", "", name)


def load_zones(client_directory):
    directory = Path(client_directory)
    index = directory / "game.pki"
    if hashlib.sha1(index.read_bytes()).hexdigest() != EXPECTED_PKI_SHA1:
        raise ValueError("Unsupported zone catalog")
    _, _, entries = parse_index(index)
    zones, evidence = {}, []
    with (directory / "game.pkg").open("rb") as package:
        for entry in entries:
            if entry["type_code"] != 16:
                continue
            package.seek(entry["package_offset"])
            stored = package.read(entry["stored_size"])
            decoded = zlib.decompress(stored) if entry["flags"] & 1 else stored
            if len(stored) != entry["stored_size"] or len(decoded) != entry["decoded_size"]:
                raise ValueError("Truncated zone definition")
            root = Parser(decoded.decode("cp1252")).parse()
            properties = dict(root.properties)
            name = entry["name"].lower()
            private = str(properties.get("Private", "false")).lower() == "true"
            dungeon = private and str(properties.get("PVPType", "0")) == "0"
            zones[name] = {"family": dungeon_family(name) if dungeon else "", "label": properties.get("Label", entry["name"])}
            evidence.append({key: entry[key] for key in ("entry_id", "type_code", "name", "package_offset", "stored_size", "decoded_size", "flags")})
            evidence[-1].update(storedSha256=hashlib.sha256(stored).hexdigest(), decodedSha256=hashlib.sha256(decoded).hexdigest())
    for name, zone in zones.items():
        family = zone["family"]
        root = next((zones[key] for key in (family + "_level01", family + "_intro") if key in zones), None)
        zone["title"] = root["label"] if root else zone["label"]
    return zones, evidence
