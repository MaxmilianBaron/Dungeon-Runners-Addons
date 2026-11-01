import hashlib
import json
import sys
from pathlib import Path

from catalog import load_catalog, load_zones


def generate(client_directory, output_directory):
    client = Path(client_directory)
    output = Path(output_directory)
    result = load_catalog(client)
    result["zones"], zone_entries = load_zones(client)
    result["entries"].extend(zone_entries)
    combined = hashlib.sha256()
    with (client / "game.pkg").open("rb") as package:
        for entry in result["entries"]:
            package.seek(entry["package_offset"])
            combined.update(package.read(entry["stored_size"]))
    result["combinedStoredSha256"] = combined.hexdigest()
    lines = ["#pragma once", "#include <cstdint>", "struct CatalogLabel { const char* path; const char* label; };", "static const CatalogLabel SkillLabels[] = {"]
    for key, value in sorted(result["labels"].items()):
        lines.append("    {" + json.dumps(key, ensure_ascii=True) + ", " + json.dumps(value, ensure_ascii=True) + "},")
    lines += ["};", "struct CatalogCritical { const char* path; const char* element; int32_t multiplier; };", "static const CatalogCritical SkillCriticals[] = {"]
    for key, value in sorted(result["critical"].items()):
        components = {
            "skills.generic.firering": {"ProjectileEffect": "hit", "FireModifierEffect": "DoT"},
            "skills.generic.icetargetedburst": {"ProjectileEffect": "hit", "IceModifierEffect": "DoT"},
            "skills.generic.iceshot": {"ProjectileEffect.DamageEffect": "hit", "ProjectileEffect.*4": "splash"},
            "skills.generic.firecurseshot": {"FireDamageEffect": "hit", "FireShotObject": "aura"},
            "skills.generic.poisontrail": {"ProjectileEffect": "hit", "DoTModifierEffect": "DoT", "SelfDoTModifierEffect": "self"},
            "skills.generic.firetrail": {"ProjectileEffect": "hit", "DoTModifierEffect": "DoT", "SelfDoTModifierEffect": "self"},
        }
        effects = set()
        for effect in value["effects"]:
            branch = effect["path"][len(key)+1:]
            component = next((label for prefix, label in components.get(key, {}).items() if branch == prefix or branch.startswith(prefix + ".")), "")
            element = effect["element"] + (" " + component if component else "")
            effects.add((element, effect["fixed"]))
        effects = sorted(effects)
        for element, fixed in effects:
            lines.append("    {" + json.dumps(key) + ", " + json.dumps(element) + ", " + str(fixed) + "},")
    lines += ["};", "struct CatalogZone { const char* key; const char* family; const char* title; };", "static const CatalogZone DungeonZones[] = {"]
    for key, zone in sorted(result["zones"].items()):
        lines.append("    {" + ", ".join(json.dumps(value, ensure_ascii=True) for value in (key, zone["family"], zone["title"])) + "},")
    lines += ["};", "struct CatalogRange { uint64_t offset; uint32_t length; };", "static const CatalogRange SkillRanges[] = {"]
    for entry in result["entries"]:
        lines.append(f'    {{{entry["package_offset"]}ULL, {entry["stored_size"]}U}},')
    lines += ["};", f'static const char* CatalogPkiSha1 = "{result["pkiSha1"]}";', f'static const char* CatalogPayloadSha256 = "{result["combinedStoredSha256"]}";']
    output.mkdir(parents=True, exist_ok=True)
    (output / "native_catalog.generated.h").write_text("\n".join(lines) + "\n", encoding="ascii")
    (output / "native-catalog-provenance.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"Generated {len(result['labels'])} labels from {len(result['entries'])} verified PKI entries")


if __name__ == "__main__":
    generate(sys.argv[1], sys.argv[2])
