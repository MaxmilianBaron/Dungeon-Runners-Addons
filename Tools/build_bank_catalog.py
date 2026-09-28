import argparse
import hashlib
import json
import re
import zlib
from collections import Counter
from functools import lru_cache
from pathlib import Path

from definitions import Parser, normalize_path
from package_index import EXPECTED_PKI_SHA1, parse_index


def build_catalog(client):
    index = client / 'game.pki'
    if hashlib.sha1(index.read_bytes()).hexdigest() != EXPECTED_PKI_SHA1:
        raise ValueError('Unsupported bank item catalog')
    _, _, entries = parse_index(index)
    nodes, provenance = {}, []

    def register(node, path):
        nodes[path] = node
        for child in node.children:
            if child.name.lower() not in ('description', 'descriptione'):
                register(child, path + '.' + child.name.lower())

    with (client / 'game.pkg').open('rb') as package:
        for entry in entries:
            if entry['type_code'] != 13:
                continue
            package.seek(entry['package_offset'])
            stored = package.read(entry['stored_size'])
            raw = zlib.decompress(stored) if entry['flags'] & 1 else stored
            if len(stored) != entry['stored_size'] or len(raw) != entry['decoded_size']:
                raise ValueError('Truncated definition: ' + entry['name'])
            root = Parser(raw.decode('cp1252')).parse()
            register(root, normalize_path(entry['name']).lower())
            record = {k: entry[k] for k in ('entry_id', 'type_code', 'name', 'package_offset', 'stored_size', 'decoded_size', 'flags')}
            record.update(storedSha256=hashlib.sha256(stored).hexdigest(), decodedSha256=hashlib.sha256(raw).hexdigest())
            provenance.append(record)

    @lru_cache(None)
    def resolve(path, depth=0):
        if depth >= 32:
            raise ValueError('Cyclic item inheritance: ' + path)
        node = nodes.get(path)
        if node is None:
            return {}, (path,)
        props, chain = resolve(normalize_path(node.extends_path).lower(), depth + 1) if node.extends_path else ({}, ())
        props = dict(props)
        desc = next((c for c in node.children if c.name.lower() in ('description', 'descriptione')), None)
        if desc:
            props.update({k.lower(): v for k, v in desc.properties})
        return props, (path,) + chain

    materials = {'scale': 0, 'plate': 0, 'crystal': 0, 'leather': 1, 'chain': 1, 'splint': 1, 'cloth': 2, 'padded': 2, 'ghost': 2,
                 'xheavy': 0, 'xxheavy': 0, 'xxxheavy': 0, 'light': 1, 'medium': 1, 'heavy': 1, 'xlight': 2, 'xxlight': 2, 'xxxlight': 2}
    rows = []
    for path in sorted(nodes):
        props, chain = resolve(path)
        icon = props.get('inventoryicon', '')
        if not icon or not any(c in ('item', 'activeitem', 'weapon', 'meleeweapon', 'rangedweapon', 'armor') for c in chain):
            continue
        try:
            slot, width, height = (int(props.get(k, '0')) for k in ('slottype', 'inventorywidth', 'inventoryheight'))
        except ValueError:
            continue
        if not (0 < width <= 32 and 0 < height <= 32):
            continue
        category, role, family, kind = 6, 3, '', ''
        weapon = props.get('weaponcategory', '').lower()
        if 'basescroll' in chain:
            category, kind = 0, '0-scroll'
        elif 'basepotion' in chain:
            category, kind = 0, '1-potion'
        elif 'basequestitem' in chain or props.get('quality', '').upper() == 'QUEST':
            category, kind = 0, '2-quest'
        elif slot in (3, 4):
            category = 1
        elif slot == 1:
            category = 2
        elif weapon:
            category, kind = (4 if weapon.startswith('2h') else 3), weapon
        elif slot == 11:
            category, kind = 3, 'shield'
        elif slot in (2, 5, 6, 7, 8):
            category = 5
            for ancestor in chain:
                match = re.fullmatch(r'base(?:armor|helm|gloves|boots|shoulders)classes\.(\w+)', ancestor)
                if match and match[1] in materials:
                    role = materials[match[1]]
                    break
            if not family:
                visual = props.get('visual', '').lower()
                if visual.startswith('object_visual:'):
                    visual = visual[len('object_visual:'):]
                match = re.fullmatch(r'(ghost|cloth|rubber)(mythic|unique)?(?:armor|helm|shoulders?|gloves?|boots?)(\d+)(.*)', visual)
                if match:
                    family = ''.join((match[1], match[2] or '', '_', match[3], match[4]))
                    if family == 'ghost_3':
                        family += '_death' if any(c.endswith('.prebuiltmythic001') for c in chain) else '_horror'
            for ancestor in chain:
                match = re.search(r'\.((?:cloth|ghost|padded|leather|chain|splint|scale|plate|crystal)(?:mythic|unique)?)(?:armor|helm|shoulders?|gloves?|boots?)(\d+\w*)$', ancestor)
                if match:
                    family = match[1] + '_' + match[2]
                    if role == 3:
                        role = next((v for k, v in materials.items() if match[1].startswith(k)), 3)
                    break
        rows.append(dict(path=path, icon=icon.lower(), slot=slot, width=width, height=height, category=category, role=role, family=family, kind=kind))
    return rows, dict(pkiSha256=hashlib.sha256(index.read_bytes()).hexdigest(), entries=provenance)


def write_catalog(rows, output):
    quote = lambda value: json.dumps(value, ensure_ascii=True)
    lines = ['#pragma once', '#include <algorithm>', '#include <cstring>', '#include <iterator>',
             'struct BankCatalogEntry { const char* key; const char* icon; const char* family; const char* kind; unsigned slot, width, height, category, role; };',
             'inline constexpr BankCatalogEntry BankItemCatalog[] = {']
    for row in rows:
        values = [quote(row[k]) for k in ('path', 'icon', 'family', 'kind')] + [str(row[k]) for k in ('slot', 'width', 'height', 'category', 'role')]
        lines.append('    {' + ','.join(values) + '},')
    lines += ['};', 'inline const BankCatalogEntry* FindBankDefinition(const char* key) {',
              '    const auto end = std::end(BankItemCatalog);',
              '    const auto found = std::lower_bound(std::begin(BankItemCatalog),end,key,[](const auto& row,const char* value) { return std::strcmp(row.key,value)<0; });',
              '    return found!=end && !std::strcmp(found->key,key) ? found : nullptr;', '}']
    (output / 'bank_catalog.generated.h').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('client', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows, provenance = build_catalog(args.client)
    if len(rows) < 500:
        raise ValueError('Incomplete bank item catalog')
    write_catalog(rows, args.output)
    (args.output / 'bank-catalog.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    (args.output / 'bank-catalog-provenance.json').write_text(json.dumps(provenance, indent=2), encoding='utf-8')
    print(json.dumps(dict(items=len(rows), categories=dict(Counter(r['category'] for r in rows)), armorClasses=dict(Counter(r['role'] for r in rows if r['category'] == 5)))))


if __name__ == '__main__':
    main()
