import json
import zlib

from definitions import Parser, normalize_path


def write_sally_sam_catalog(package_path, entries, output):
    families = {
        "world.base.npc.sally": "Sally",
        "world.base.npc.sam": "UniqueSam",
        "world.base.npc.sammythic": "MythicSam",
    }
    sally_kinds = {
        "merchantrandomig": "NormalSally",
        "questitemig.token": "KcSally",
        "mythicig": "MythicSally",
    }
    categories = ("1HWeapon", "2HWeapon", "Shield", "Body", "Shoulders", "Helm", "Gloves", "Boots", "Jewelry")

    def properties(node):
        yield from node.properties
        for child in node.children:
            yield from properties(child)

    records = {}
    with package_path.open("rb") as package:
        for entry in entries:
            key = normalize_path(entry["name"]).lower()
            if entry["type_code"] != 13 or ".npc." not in key:
                continue
            package.seek(entry["package_offset"])
            stored = package.read(entry["stored_size"])
            raw = zlib.decompress(stored) if entry["flags"] & 1 else stored
            if len(stored) != entry["stored_size"] or len(raw) != entry["decoded_size"]:
                raise ValueError("Truncated NPC definition: " + key)
            text = raw.decode("cp1252")
            if key not in families and not any(family in normalize_path(text).lower() for family in families):
                continue
            node = Parser(text).parse()
            kind = families.get(normalize_path(node.extends_path).lower(), families.get(key))
            if kind is None:
                continue
            fields = list(properties(node))
            if kind == "Sally":
                generators = [normalize_path(value).lower() for name, value in fields if name.lower() == "itemgenerator"]
                if generators:
                    kind = sally_kinds[generators[0]]
            offers = 0
            if kind in ("UniqueSam", "MythicSam"):
                for name, value in fields:
                    if name.lower() == "quest":
                        quest = normalize_path(value).rsplit(".", 1)[-1]
                        offers |= 1 << categories.index(quest[4:])
            records[key] = kind, offers

    if len(records) != 151:
        raise ValueError("Unexpected Sally and Sam catalog size")
    lines = ["#pragma once", "inline constexpr Definition Definitions[] = {"]
    for key, (kind, offers) in sorted(records.items()):
        lines.append(f"    {{{json.dumps(key)}, Kind::{kind}, {offers}}},")
    (output / "sally_sam_catalog.generated.h").write_text("\n".join(lines + ["};", ""]), encoding="utf-8")
