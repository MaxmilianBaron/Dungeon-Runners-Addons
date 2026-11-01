class Node:
    def __init__(self, name, extends_path="", is_static=False):
        self.name = name
        self.extends_path = extends_path
        self.is_static = is_static
        self.properties = []
        self.children = []


def strip_comments(text):
    output = []
    index = 0
    in_string = False
    while index < len(text):
        current = text[index]
        if current == '"' and (index == 0 or text[index - 1] != "\\"):
            in_string = not in_string
            output.append(current)
            index += 1
            continue
        if in_string:
            output.append(current)
            index += 1
            continue
        if index + 1 < len(text) and current == "/" and text[index + 1] == "/":
            index += 2
            while index < len(text) and text[index] != "\n":
                index += 1
            continue
        if index + 1 < len(text) and current == "/" and text[index + 1] == "*":
            index += 2
            end = text.find("*/", index)
            if end < 0:
                raise ValueError("unterminated block comment")
            index = end + 2
            continue
        output.append(current)
        index += 1
    if in_string:
        raise ValueError("unterminated string")
    return "".join(output)


class Parser:
    def __init__(self, text):
        self.text = strip_comments(text)
        self.position = 0

    def skip_space(self):
        while self.position < len(self.text) and self.text[self.position].isspace():
            self.position += 1

    def word(self):
        self.skip_space()
        start = self.position
        allowed = "_./\\-*:"
        while self.position < len(self.text):
            value = self.text[self.position]
            if not (value.isalnum() or value in allowed):
                break
            self.position += 1
        if self.position == start:
            return None
        return self.text[start:self.position].strip('"')

    def at_word(self, value):
        self.skip_space()
        end = self.position + len(value)
        if self.text[self.position:end].lower() != value.lower():
            return False
        if end < len(self.text) and self.text[end].isalnum():
            return False
        return True

    def value(self):
        self.skip_space()
        if self.position >= len(self.text):
            return ""
        output = []
        if self.text[self.position] == '"':
            self.position += 1
            while self.position < len(self.text) and self.text[self.position] != '"':
                if self.text[self.position] == "\\" and self.position + 1 < len(self.text):
                    output.append(self.text[self.position + 1])
                    self.position += 2
                else:
                    output.append(self.text[self.position])
                    self.position += 1
            if self.position >= len(self.text):
                raise ValueError("unterminated quoted value")
            self.position += 1
        else:
            while self.position < len(self.text) and self.text[self.position] not in ";\n\r}":
                output.append(self.text[self.position])
                self.position += 1
        if self.position < len(self.text) and self.text[self.position] == ";":
            self.position += 1
        return "".join(output).strip()

    def skip_statement(self):
        while self.position < len(self.text) and self.text[self.position] not in ";{}\n":
            self.position += 1
        if self.position < len(self.text) and self.text[self.position] == ";":
            self.position += 1
        elif self.position < len(self.text) and self.text[self.position] == "{":
            depth = 0
            while self.position < len(self.text):
                current = self.text[self.position]
                self.position += 1
                if current == "{":
                    depth += 1
                elif current == "}":
                    depth -= 1
                    if depth == 0:
                        break
        elif self.position < len(self.text) and self.text[self.position] == "\n":
            self.position += 1

    def node_header(self):
        first = self.word()
        if first is None:
            return None
        is_static = first.lower() == "static"
        name = self.word() if is_static else first
        if not name:
            return None
        self.skip_space()
        extends_path = ""
        if self.at_word("extends"):
            self.position += len("extends")
            extends_path = self.word() or ""
        return Node(name, extends_path, is_static)

    def parse(self):
        root = self.node_header()
        if root is None:
            raise ValueError("missing root node")
        self.skip_space()
        if self.position >= len(self.text) or self.text[self.position] != "{":
            raise ValueError("missing root block")
        self.position += 1
        self.block(root)
        self.skip_space()
        trailing = self.text[self.position:].strip()
        if trailing and set(trailing) != {"}"}:
            raise ValueError(f"trailing content at {self.position}")
        return root

    def block(self, parent):
        while self.position < len(self.text):
            loop_position = self.position
            self.skip_space()
            if self.position >= len(self.text):
                raise ValueError("unterminated block")
            if self.text[self.position] == "}":
                self.position += 1
                return
            saved = self.position
            header = self.node_header()
            if header is None:
                self.skip_statement()
                continue
            self.skip_space()
            if self.position < len(self.text) and self.text[self.position] == "=":
                self.position += 1
                parent.properties.append((header.name, self.value()))
                continue
            if self.position < len(self.text) and self.text[self.position] == "{":
                self.position += 1
                self.block(header)
                parent.children.append(header)
                continue
            self.position = saved
            self.skip_statement()
            if self.position <= loop_position:
                raise ValueError(f"parser made no progress at {self.position}")
        raise ValueError("unterminated block")


def normalize_path(value):
    normalized = (value or "").strip().replace("\\", ".").replace("/", ".")
    if normalized.lower().endswith(".gc"):
        normalized = normalized[:-3]
    while ".." in normalized:
        normalized = normalized.replace("..", ".")
    return normalized.strip(".")
