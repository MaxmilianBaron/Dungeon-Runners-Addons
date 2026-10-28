import math
from functools import lru_cache

from definitions import Parser, normalize_path


def critical_catalog(documents):
    roots = {normalize_path(path).lower(): Parser(text).parse() for path, text in documents}

    def merge(node, inherited=None, depth=0):
        if depth > 64:
            raise ValueError("Critical definition depth exceeded")
        base = resolve(normalize_path(node.extends_path).lower()) if node.extends_path else inherited
        properties = dict(base[1]) if base else {}
        properties.update({key.lower(): value for key, value in node.properties})
        children = list(base[2]) if base else []
        for child in node.children:
            index = next((i for i, (name, _) in enumerate(children) if name.lower() == child.name.lower()), None) if child.name != "*" else None
            previous = children[index][1] if index is not None else None
            value = (child.name, merge(child, previous, depth + 1))
            if index is None:
                children.append(value)
            else:
                children[index] = value
        kind = base[0] if base else normalize_path(node.extends_path).lower()
        return kind, properties, children

    resolving = set()

    @lru_cache(maxsize=None)
    def resolve(path):
        if not path:
            return None
        if path in resolving:
            raise ValueError("Cyclic critical definition: " + path)
        resolving.add(path)
        try:
            if path in roots:
                return merge(roots[path])
            parent, dot, child = path.rpartition(".")
            if dot:
                value = resolve(parent)
                if value:
                    return next((node for name, node in value[2] if name.lower() == child), None)
                return None
            return path, {}, []
        finally:
            resolving.remove(path)

    result = {}
    for path in sorted(roots):
        if not path.startswith("skills.generic.") or ".base." in path:
            continue
        root = resolve(path)
        if not root or root[0] not in {"activeskill", "passiveskill"}:
            continue
        description = next((node for name, node in root[2] if name.lower() == "description"), None)
        if not description or not description[1].get("label"):
            continue
        rows = []

        def visit(node, location):
            if node[0] == "spelldamageeffect":
                multiplier = float(node[1].get("criticalchance", "0"))
                if not math.isfinite(multiplier) or not 0 <= multiplier <= 100:
                    raise ValueError("Invalid critical multiplier: " + location)
                rows.append({"path": location, "multiplier": multiplier, "fixed": math.ceil(multiplier * 256), "element": node[1].get("damagetype", "CRUSHING").title()})
            for index, (name, child) in enumerate(node[2]):
                visit(child, location + "." + (name if name != "*" else "*" + str(index)))

        visit(root, path)
        if rows:
            result[path] = {"label": description[1]["label"], "effects": rows}
    return result
