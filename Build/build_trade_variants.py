import hashlib
import json
import re
from decimal import Decimal, ROUND_CEILING
from functools import lru_cache

from definitions import normalize_path

QUALITY_ORDER = ('QUEST', 'NORMAL', 'SUPERIOR', 'MAGICAL', 'RARE', 'UNIQUE', 'COLLECTION', 'MYTHIC')


def quality_name(value):
    value = value.upper()
    if value == 'RELIC':
        return 'COLLECTION'
    if value.isdecimal() and int(value) < len(QUALITY_ORDER):
        return QUALITY_ORDER[int(value)]
    return value if value in QUALITY_ORDER else ''


def build_variants(nodes, roots, item_keys):
    effects = {}
    block_attributes = {}
    node_paths = {node: path for path, node in nodes.items()}

    def key(path):
        return normalize_path(path).lower()

    @lru_cache(None)
    def ancestors(node):
        chain, seen = [], set()
        while node is not None and node not in seen and len(chain) < 32:
            chain.append(node)
            seen.add(node)
            node = nodes.get(key(node.extends_path))
        return tuple(reversed(chain))

    @lru_cache(None)
    def properties(node, description=False):
        result = {}
        for parent in ancestors(node):
            if description:
                parent = next((c for c in parent.children if c.name.lower() in ('description', 'descriptione')), None)
            if parent is not None:
                result.update((name.lower(), value) for name, value in parent.properties)
        return result

    def attribute_label(attr):
        if attr in ('SPEEDMOD', 'SPEED_MOD'):
            return 'Movement Speed %'
        if attr in ('SIZEMOD', 'SIZE_MOD'):
            return 'Character Size %'
        if attr.endswith('_MOD'):
            return attr[:-4].replace('_', ' ').title() + ' %'
        return attr.removesuffix('_BONUS').replace('_', ' ').title().replace('Hit Point', 'Health')

    @lru_cache(None)
    def modifier(path):
        node = nodes.get(path)
        if node is None:
            return None
        chain = ancestors(node)
        if key(node.extends_path) == 'itemmodifier' and not node.properties and not node.children:
            effects[path] = ''
            return (), '', '', ''
        attrs = {}
        attributes = []
        complete = any(key(parent.extends_path) == 'itemattributemodifier' for parent in chain)
        fields_by_child = {}
        for parent in chain:
            desc = next((c for c in parent.children if c.name.lower() in ('description', 'descriptione')), None)
            if desc:
                for attr in desc.children:
                    child = attr.name.lower()
                    base, inherited = fields_by_child.get(child, ('', {}))
                    fields_by_child[child] = (key(attr.extends_path), properties(attr)) if attr.extends_path else (
                        base, {**inherited, **properties(attr)})
        for base, fields in fields_by_child.values():
            name = fields.get('attribute')
            if name:
                attrs.setdefault(name, attribute_label(name))
                attributes.append((base, tuple(sorted(fields.items()))))
            else:
                complete = False
        props = properties(node, True)
        if not attrs and not complete:
            return None
        effect = None
        if complete:
            effect = hashlib.sha256(json.dumps(sorted(attributes), ensure_ascii=True).encode()).hexdigest() if attributes else ''
            effects[path] = effect
            block = [dict(fields) for _, fields in attributes if dict(fields)['attribute'] == 'BLOCK']
            if block:
                block_attributes[effect] = block
        label = ' + '.join(attrs.values())
        if len(attributes) > 1:
            parts = [dict(fields) for _, fields in attributes]
            shares = [fields.get('value') for fields in parts]
            if all(shares) and len(set(shares)) > 1:
                label = ' + '.join(
                    f"{attribute_label(fields['attribute'])} ({(Decimal(fields['value']) * 100).normalize():f}%)"
                    for fields in parts)
        return tuple(attrs), label, quality_name(props.get('quality', '')), effect

    def interval(props):
        try:
            if float(props.get('chance', '1')) <= 0:
                return 1, 0
            return max(1, int(props.get('minlevel', '1'))), min(100, int(props.get('maxlevel', '100') or 100))
        except ValueError:
            return 1, 0

    @lru_cache(None)
    def table(path, chain=()):
        node = nodes.get(path)
        if node is None or path in chain or len(chain) >= 16:
            return None
        chain = (*chain, path)
        children = next((parent.children for parent in reversed(ancestors(node)) if parent.children), ())
        options = []
        complete = True
        for child in children or (node,):
            props = properties(child)
            low, high = interval(props)
            if low > high:
                continue
            link = key(props.get('linkedgenerator', ''))
            if link:
                linked = table(link, chain)
                if linked is None:
                    complete = False
                    continue
                complete &= linked[1]
                for attrs, label, quality, first, last, effect in linked[0]:
                    start, end = max(low, first), min(high, last)
                    if start <= end:
                        options.append((attrs, label, quality, start, end, effect))
                continue
            mod_path = key(props.get('itemmodifier', ''))
            value = modifier(mod_path)
            if value is None:
                return None
            attrs, label, quality, effect = value
            complete &= effect is not None
            options.append((attrs, label, quality, low, high, effect))
        return (tuple(dict.fromkeys(options)), complete) if options else None

    @lru_cache(None)
    def fixed_item(path):
        fixed, declared_quality = [], quality_name(properties(nodes[path], True).get('quality', ''))
        fixed_nodes = {}
        for parent in ancestors(nodes[path]):
            for child in parent.children:
                if re.fullmatch(r'mod\d+', child.name.lower()):
                    fixed_nodes[child.name.lower()] = node_paths.get(child, key(child.extends_path))
                elif child in node_paths and modifier(node_paths[child]):
                    fixed_nodes[node_paths[child]] = node_paths[child]
        mod_qualities = []
        fixed_effects = []
        complete = True
        for path in fixed_nodes.values():
            value = modifier(path)
            if value:
                if value[1]:
                    fixed.append(value[1])
                if value[2]:
                    mod_qualities.append(value[2])
                if value[3]:
                    fixed_effects.append(value[3])
                complete &= value[3] is not None
            else:
                complete = False
        quality = declared_quality
        if declared_quality in ('', 'NORMAL') and mod_qualities:
            quality = max(mod_qualities, key=QUALITY_ORDER.index)
        return ' · '.join(dict.fromkeys(fixed)), quality, declared_quality, tuple(sorted(fixed_effects)), complete

    recipes, groups, fixed_sets = set(), {}, {(): 0}
    primary = {'ENDURANCE', 'STRENGTH', 'AGILITY', 'INTELLECT', 'INTELLIGENCE', 'DEXTERITY'}
    qualities = set(QUALITY_ORDER)

    def timeline(node):
        for parent in reversed(ancestors(node)):
            phase = re.fullmatch(r'itemtimeline\.([a-z0-9]+)phase(\d+)', key(parent.extends_path))
            if phase:
                return phase[1]
        return ''

    stack = list(roots)
    while stack:
        node = stack.pop()
        stack.extend(node.children)
        declared = dict((name.lower(), value) for name, value in node.properties)
        if not any(name.startswith('itemmodgenerator') or name == 'item' for name in declared):
            continue
        props = properties(node)
        item = key(props.get('item', ''))
        if item not in item_keys:
            continue
        low, high = interval(props)
        if low > high:
            continue
        kind = timeline(node)
        fixed, rarity, declared_quality, fixed_effects, complete = fixed_item(item)
        locked_effects = list(fixed_effects)
        generated_qualities = [rarity] if rarity else []
        fixed_labels = [fixed] if fixed else []
        slot_keys = sorted(name for name in props if re.fullmatch(r'itemmodgenerator\d+', name) and props[name])
        slots = []
        for name in slot_keys:
            data = table(key(props[name]))
            if data is None:
                complete = False
                continue
            options, supported = data
            complete &= supported
            options = tuple(option for option in options if option[3] <= high and option[4] >= low)
            if not options:
                continue
            attrs = {attr for option in options for attr in option[0]}
            option_qualities = {option[2] for option in options}
            if len(option_qualities) == 1:
                quality = next(iter(option_qualities))
                if quality:
                    generated_qualities.append(quality)
            if not attrs and len(option_qualities) == 1 and next(iter(option_qualities)) in qualities:
                continue
            if not attrs or any(not option[1] for option in options):
                complete = False
                continue
            group = 'Primary Stats' if {attr.removesuffix('_BONUS') for attr in attrs} <= primary else 'Resistances' if all('RESIST' in attr for attr in attrs) else 'Bonuses'
            if len(options) == 1 and options[0][3] <= low and options[0][4] >= high:
                fixed_labels.append(options[0][1])
                if options[0][5]:
                    locked_effects.append(options[0][5])
                continue
            names = {}
            for opt in options:
                names.setdefault(opt[1], set()).add(opt[5])
            options = tuple(dict.fromkeys((opt[1], opt[3], opt[4], opt[5] if len(names[opt[1]]) == 1 else None)
                                         for opt in options))
            slots.append((group, groups.setdefault((group, options), len(groups)+1)))
        if declared_quality in ('', 'NORMAL'):
            if kind.upper() in qualities:
                generated_qualities.append(kind.upper())
            if generated_qualities:
                rarity = max(generated_qualities, key=QUALITY_ORDER.index)
        order = {'Primary Stats': 0, 'Bonuses': 1, 'Resistances': 2}
        ids = tuple(slot[1] for slot in sorted(slots, key=lambda slot: order[slot[0]]))
        if len(ids) <= 6:
            fixed_index = fixed_sets.setdefault(tuple(sorted(locked_effects)), len(fixed_sets))
            recipes.add((item, rarity.title() if rarity in qualities else '', ' · '.join(dict.fromkeys(fixed_labels)),
                         low, high, ids, fixed_index, complete))
    covered = {recipe[0] for recipe in recipes}
    for item in item_keys - covered:
        fixed, rarity, _, fixed_effects, complete = fixed_item(item)
        fixed_index = fixed_sets.setdefault(fixed_effects, len(fixed_sets))
        recipes.add((item, rarity.title() if rarity in qualities else '', fixed, 1, 100, (), fixed_index, complete))
    for path in nodes:
        modifier(path)
    knobs = properties(nodes['globalknobs'])
    deltas = [int(knobs['itemleveldelta' + name]) for name in ('unique', 'mythic')]
    direct_aliases = {}
    for item in item_keys:
        node = nodes[item]
        parent = key(node.extends_path)
        if parent in item_keys and not node.properties and all(
            child.name.lower() in ('description', 'descriptione') and not child.extends_path and
            not child.properties and not child.children for child in node.children
        ):
            direct_aliases[item] = parent
    aliases = {}
    for item, target in direct_aliases.items():
        seen = {item}
        while target in direct_aliases and target not in seen:
            seen.add(target)
            target = direct_aliases[target]
        if target not in seen:
            aliases[item] = target

    def fixed_raw(text):
        return int((Decimal(text) * 256).to_integral_value(rounding=ROUND_CEILING))

    per_level = fixed_raw(knobs['itemdefenseratingperlevel'])
    armor_ratings = {}
    for item in item_keys:
        if item in aliases or not any(key(parent.extends_path) == 'armor' for parent in ancestors(nodes[item])):
            continue
        multiplier = fixed_raw(properties(nodes[item], True).get('defenserating', '0'))
        armor_ratings[item] = tuple(
            (1 + (per_level * (level + delta) * multiplier >> 16)) & 65535
            for delta in deltas for level in range(100, 103))

    block_pool = 'pooltables.blockmodpool'
    block_points = [(fixed_raw(properties(point)['level']), fixed_raw(properties(point)['value']))
                    for point in nodes[block_pool].children]
    mapping = next(child for child in nodes['pooltablemapping.shield'].children if key(child.extends_path) == block_pool)
    shield_weight = fixed_raw(properties(mapping)['value'])

    def shield_block(fields, level):
        raw = fixed_raw(fields['value'])
        if fields.get('overridetable', '').lower() == 'true':
            increment = fixed_raw(fields.get('valueincrement', '0'))
            return (raw+(level-1)*increment) >> 8
        current = level*256
        for (left, first), (right, last) in zip(block_points, block_points[1:]):
            if left <= current <= right:
                fraction = ((current-left) << 8) // (right-left)
                value = first+((last-first)*fraction >> 8)
                rounded = (value >> 8)+(value & 255 >= 127)
                return max(1, ((rounded*raw*shield_weight) >> 16))
        raise ValueError(f'No authored shield block curve for level {level}')

    blocks = [(effect, tuple(sum(shield_block(fields, level+delta) for fields in attributes)
                             for delta in deltas for level in range(100, 103)))
              for effect, attributes in sorted(block_attributes.items())]
    return sorted(recipes), list(groups), list(fixed_sets), sorted(effects.items()), deltas, sorted(aliases.items()), sorted(armor_ratings.items()), blocks


def write_variants(recipes, groups, fixed_sets, effects, deltas, aliases, armor_ratings, blocks, output):
    quote = lambda text: json.dumps(text, ensure_ascii=True)
    lines = ['#pragma once', '#include <array>',
             'struct TradeRollOption { const char* label; unsigned low, high; const char* effect; };',
             'struct TradeRollGroup { const char* label; unsigned first, count; };',
             'struct TradeRollRecipe { const char* item; const char* rarity; const char* fixed; unsigned low, high; std::array<unsigned,6> slots; unsigned fixedSet; bool exact; };',
             'struct TradeModifierEffect { const char* definition; const char* effect; };',
             'struct TradeFixedEffects { unsigned first, count; };',
             'struct TradeItemAlias { const char* definition; const char* canonical; };',
             'struct TradeArmorRating { const char* definition; std::array<unsigned,6> ratings; };',
             'struct TradeBlockEffect { const char* effect; std::array<int,6> values; };',
             'inline constexpr TradeRollOption TradeRollOptions[] = {']
    offsets = []
    index = 0
    for label, options in groups:
        offsets.append((label, index, len(options)))
        for text, low, high, effect in options:
            encoded = 'nullptr' if effect is None else quote(effect)
            lines.append(f'    {{{quote(text)},{low},{high},{encoded}}},')
        index += len(options)
    lines += ['};', 'inline constexpr TradeRollGroup TradeRollGroups[] = {', '    {"",0,0},']
    for label, start, count in offsets:
        lines.append(f'    {{{quote(label)},{start},{count}}},')
    lines += ['};', 'inline constexpr TradeRollRecipe TradeRollRecipes[] = {']
    for key, rarity, fixed, low, high, slots, fixed_index, exact in recipes:
        group_ids = ','.join(str(number) for number in slots)
        lines.append(f'    {{{quote(key)},{quote(rarity)},{quote(fixed)},{low},{high},{{{group_ids}}},{fixed_index},{str(exact).lower()}}},')
    lines += ['};', 'inline constexpr const char* TradeFixedEffectIds[] = {', '    "",']
    first = 1
    ranges = []
    for fixed in fixed_sets:
        ranges.append((first, len(fixed)))
        lines.extend(f'    {quote(effect)},' for effect in fixed)
        first += len(fixed)
    lines += ['};', 'inline constexpr TradeFixedEffects TradeFixedEffectSets[] = {']
    lines.extend(f'    {{{first},{count}}},' for first, count in ranges)
    lines += ['};', 'inline constexpr TradeModifierEffect TradeModifierEffects[] = {']
    lines.extend(f'    {{{quote(path)},{quote(effect)}}},' for path, effect in effects)
    lines += ['};', 'inline constexpr TradeItemAlias TradeItemAliases[] = {']
    lines.extend(f'    {{{quote(path)},{quote(canonical)}}},' for path, canonical in aliases)
    lines += ['};', 'inline constexpr TradeArmorRating TradeArmorRatings[] = {']
    lines.extend(f'    {{{quote(path)},{{{",".join(map(str, ratings))}}}}},' for path, ratings in armor_ratings)
    lines += ['};', 'inline constexpr TradeBlockEffect TradeBlockEffects[] = {']
    lines.extend(f'    {{{quote(effect)},{{{",".join(map(str, values))}}}}},' for effect, values in blocks)
    lines += ['};', f'inline constexpr int TradeUniqueLevelDelta={deltas[0]}, TradeMythicLevelDelta={deltas[1]};']
    (output / 'trade_variants.generated.h').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    evidence = dict(recipes=recipes, groups=groups)
    (output / 'trade-variants.json').write_text(json.dumps(evidence), encoding='utf-8')
    print(f'Indexed {len(recipes)} authored Trade Auction variants')
