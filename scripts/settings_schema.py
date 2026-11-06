#!/usr/bin/env python3
"""Writes settings/schema.json: every setting of CoAVolFog.ini as the settings window shows it.

The launcher and the website read the schema instead of repeating the settings: labels, help, ranges, choices,
which setting turns another off, and the shipped default. It is made from the engine's own sources:

  engine/src/settings_panel.cpp  sections, labels, help, slider ranges and formats, choices, disabled rows
  engine/src/config.cpp          the ini key behind each field and the range the engine clamps it to
  engine/src/relief_classes.h    the relief texture types
  package/CoAVolFog.ini          the shipped values and, for the settings the window does not show, the help

Run it after changing any of those; --check fails when settings/schema.json is out of date (CI runs it).
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "engine" / "src"
OUT = ROOT / "settings" / "schema.json"

SCHEMA_VERSION = 1
MULTIPLIER_MAX = 4.0

# What a player sees first. The rest of a section is "advanced"; "expert" sections are hidden behind a switch.
ESSENTIALS = [
    "Quality", "Density", "Shafts", "LampLight", "LampFog", "NightDarkness", "Lantern", "Relief", "WaterReflect",
    "WetSurfaces", "FarClipMax", "Saturation",
]
EXPERT_SECTIONS = {"debug", "engine", "hotkeys", "zones"}

SECTION_IDS = {
    "Quality": "quality", "Density": "density", "Light": "light", "Colour": "colour",
    "Sun and moon shafts": "shafts", "Drift and mist": "drift", "Weather": "weather", "Open land": "openLand",
    "Dusk and night": "night", "Belt lantern": "lantern", "Ground relief": "relief", "Water": "water",
    "Wet and shiny surfaces": "wet", "World": "world", "Debug": "debug",
}

# Settings the window does not draw with a plain widget: given here, with their help from the ini's comments.
EXTRA_SECTIONS = [
    ("engine", "Engine"),
    ("hotkeys", "Keys"),
    ("zones", "Zones"),
]
EXTRAS = [
    ("Enable", "engine", "Lumenveil", "bool",
     {"help": "Off: the game is drawn as without Lumenveil, which stays installed. Needs a client restart"}),
    ("PlainGame", "engine", "Plain game", "bool",
     {"help": "On: the game is shown without Lumenveil, as the compare key shows it, at once and at every start. "
              "Off: Lumenveil is shown"}),
    ("EngineHooks", "engine", "Engine hooks", "bool",
     {"help": "The hooks into the client that give the lamps, water, relief, shadows and lifted view distance. "
              "Off leaves only the fog. Needs a client restart"}),
    ("Overlay", "engine", "Settings window", "bool",
     {"help": "The in-game settings window and the keys below. Turning it on again needs a client restart"}),
    ("OverlayKey", "hotkeys", "Settings window", "hotkey", {"help": "Shows and hides the in-game settings window"}),
    ("ToggleKey", "hotkeys", "Compare with the plain game", "hotkey",
     {"help": "Switches the whole mod off and on while you play, to compare with the plain game. Never saved"}),
    ("LanternKey", "hotkeys", "Belt lantern", "hotkey", {"help": "Switches the belt lantern on or off and saves that"}),
    ("ReliefKey", "hotkeys", "Ground relief", "hotkey", {"help": "Switches the ground relief on or off and saves that"}),
    ("SpotKey", "hotkeys", "Save a spot", "hotkey",
     {"help": "Writes where you stand, the time and the weather to CoAVolFog-spots.txt, to report a place that looks "
              "wrong"}),
    ("RegionKey", "hotkeys", "Zone looks window", "hotkey",
     {"help": "Shows and hides the window of the looks kept per zone (CoAVolFog-regions.ini)"}),
    ("DrawLogKey", "hotkeys", "Draw log", "hotkey", {"help": "Writes one frame's draw calls to the log, for debugging"}),
    ("ZoneDensity", "zones", "Density per zone", "zoneFactors", {}),
    ("DryZones", "zones", "Dry zones", "zoneList", {}),
    ("GreenZones", "zones", "Green zones", "zoneList", {}),
    ("DrawLog", "debug", "Draw log", "int", {"min": 0, "max": 2}),
    ("PerfLog", "debug", "Timing log", "bool", {}),
    ("DrawLogVertexConstants", "debug", "Draw log vertex constants", "text", {}),
    ("DrawLogPixelConstants", "debug", "Draw log pixel constants", "text", {}),
]
HIDDEN_KEYS = {"SettingsVersion"}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def constants():
    """Every constexpr number in the engine's sources, by name (and with its namespace for reliefclass)."""
    values = {}
    pattern = re.compile(r"constexpr\s+(?:float|int|unsigned)\s+(k\w+)\s*=\s*([-0-9.]+)f?\s*;")
    for path in list(SRC.glob("*.h")) + list(SRC.glob("*.cpp")):
        for name, value in pattern.findall(path.read_text(encoding="utf-8")):
            values[name] = float(value)
            if path.name == "relief_classes.h":
                values["reliefclass::" + name] = float(value)
    return values


def number(expr, consts):
    expr = expr.strip()
    sign = 1.0
    if expr.startswith("-"):
        sign, expr = -1.0, expr[1:].strip()
    if re.fullmatch(r"[0-9.]+f?", expr):
        return sign * float(expr.removesuffix("f"))
    if expr in consts:
        return sign * consts[expr]
    raise ValueError(f"unknown number: {expr}")


def split_args(text):
    args, depth, current, quote = [], 0, "", False
    i = 0
    while i < len(text):
        ch = text[i]
        if quote:
            current += ch
            if ch == "\\":
                current += text[i + 1]
                i += 1
            elif ch == '"':
                quote = False
        elif ch == '"':
            quote = True
            current += ch
        elif ch in "([{":
            depth += 1
            current += ch
        elif ch in ")]}":
            depth -= 1
            current += ch
        elif ch == "," and depth == 0:
            args.append(current.strip())
            current = ""
        else:
            current += ch
        i += 1
    if current.strip():
        args.append(current.strip())
    return args


def string_literal(arg):
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', arg)
    if not parts:
        raise ValueError(f"not a string: {arg}")
    return "".join(parts).replace('\\"', '"').replace("\\\\", "\\")


def call_at(text, start):
    """The argument text of the call whose '(' is at start."""
    depth, quote, i = 0, False, start
    while i < len(text):
        ch = text[i]
        if quote:
            if ch == "\\":
                i += 1
            elif ch == '"':
                quote = False
        elif ch == '"':
            quote = True
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i
        i += 1
    raise ValueError("unbalanced call")


def field_keys():
    """Config field name -> (ini key, engine min, engine max, kind) from config.cpp's tables."""
    text = strip_comments((SRC / "config.cpp").read_text(encoding="utf-8"))
    consts = constants()
    fields = {}
    for kind, table in (("int", "kIntSettings"), ("float", "kFloatSettings"), ("bool", "kBoolSettings")):
        body = re.search(table + r"\[\]\s*=\s*\{(.*?)\n\};", text, re.S).group(1)
        for entry in re.findall(r"\{([^{}]*&Config::\w+[^{}]*)\}", body):
            args = split_args(entry)
            field = args[1].split("::")[1]
            key_arg = args[0]
            if key_arg.startswith('"'):
                key = string_literal(key_arg)
            else:
                cls = re.search(r"reliefclass::k(\w+)\]", key_arg).group(1)
                key = "Relief" + cls
            lo = number(args[2], consts) if len(args) > 2 else None
            hi = number(args[3], consts) if len(args) > 3 else None
            fields[field] = (key, lo, hi, kind)
    global_only = re.search(r"kGlobalOnlySettings\[\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
    return fields, set(re.findall(r'"(\w+)"', global_only))


def choice_names(text):
    names = {}
    for name, body in re.findall(r"const char\* const (k\w+Names)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        names[name] = [string_literal(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*', body)]
    return names


def format_info(fmt):
    m = re.fullmatch(r"%\.(\d)f\s*(.*)", fmt)
    decimals, unit = int(m.group(1)), m.group(2).strip() or None
    return decimals, {"deg": "°"}.get(unit, unit)


def condition(expr, fields):
    """The setting that greys a row out, from ImGui::BeginDisabled(expr): the row is on while the condition holds."""
    expr = expr.strip()
    m = re.fullmatch(r"!c\.(\w+)", expr)
    if m:
        return {"key": fields[m.group(1)][0], "is": True}
    m = re.fullmatch(r"c\.(\w+)", expr)
    if m:
        return {"key": fields[m.group(1)][0], "is": False}
    m = re.fullmatch(r"c\.(\w+)\s*==\s*(\w+)", expr)
    if m:
        return {"key": fields[m.group(1)][0], "not": int(number(m.group(2), constants()))}
    raise ValueError(f"unknown condition: {expr}")


def panel(fields, consts):
    text = strip_comments((SRC / "settings_panel.cpp").read_text(encoding="utf-8"))
    names = choice_names(text)
    sections, settings = [], {}
    section = None
    disabled = []
    token = re.compile(
        r"const Section section\(\"([^\"]+)\"|ImGui::BeginDisabled\(|ImGui::EndDisabled\(\)|"
        r"\b(Slider|Multiplier|Toggle|Choice|Count)\(|\bbool (Draw\w+)\(")
    pos = 0
    function = None
    while True:
        m = token.search(text, pos)
        if not m:
            break
        pos = m.end()
        if m.group(3):
            function = m.group(3)
            disabled = []
            continue
        if m.group(1):
            title = m.group(1)
            section = {"id": SECTION_IDS[title], "title": title, "settings": []}
            sections.append(section)
            continue
        if m.group(0).startswith("ImGui::BeginDisabled"):
            body, end = call_at(text, m.end() - 1)
            disabled.append(condition(body, fields) if "c." in body else None)
            pos = end
            continue
        if m.group(0).startswith("ImGui::EndDisabled"):
            disabled.pop()
            continue
        widget = m.group(2)
        body, end = call_at(text, m.end() - 1)
        pos = end
        args = split_args(body)
        if not args or not args[0].startswith('"'):
            continue  # the helpers' own definitions and inner calls
        if function in ("DrawViewDistance", "DrawZoneDensity", "DrawZoneKind", "DrawStatus", "DrawSaveRow",
                        "ReliefShareSlider"):
            continue
        field = args[1].removeprefix("c.")
        if field not in fields:
            raise ValueError(f"{widget} {args[0]}: no ini key for c.{field}")
        key, lo, hi, kind = fields[field]
        label = string_literal(args[0]).split("##")[0]
        entry = {"section": section["id"], "label": label}
        if widget == "Multiplier":
            entry.update(type="float", min=0.0, max=MULTIPLIER_MAX, decimals=2, help=string_literal(args[2]))
        elif widget == "Slider":
            decimals, unit = format_info(string_literal(args[4]))
            entry.update(type="float", min=number(args[2], consts), max=number(args[3], consts), decimals=decimals,
                         help=string_literal(args[5]))
            if unit:
                entry["unit"] = unit
            if len(args) > 6 and "Logarithmic" in args[6]:
                entry["scale"] = "log"
        elif widget == "Count":
            entry.update(type="int", min=number(args[2], consts), max=number(args[3], consts),
                         help=string_literal(args[4]))
        elif widget == "Toggle":
            entry.update(type="bool", help=string_literal(args[2]))
        elif widget == "Choice":
            first = int(number(args[2], consts))
            entry.update(type="choice", help=string_literal(args[4]),
                         options=[{"value": first + i, "label": n} for i, n in enumerate(names[args[3]])])
        if kind != "bool" and entry["type"] == "bool":
            entry["type"] = "bool"  # an int 0/1 shown as a checkbox
        if lo is not None and entry["type"] in ("float", "int"):
            entry["engineMin"], entry["engineMax"] = lo, hi
        if any(disabled):
            entry["enabledWhen"] = [dict(c) for c in disabled if c]
        if key in settings:
            raise ValueError(f"{key} drawn twice")
        settings[key] = entry
        section["settings"].append(key)
    return sections, settings


def relief_types(settings, sections):
    text = (SRC / "relief_classes.h").read_text(encoding="utf-8")
    groups = {"Ground": "Ground", "Buildings": "Buildings", "Nature": "Nature", "Living": "Creatures and characters",
              "Other": "Other"}
    relief = next(s for s in sections if s["id"] == "relief")
    pattern = re.compile(r'\{"(Relief\w+)",\s*"([^"]+)",\s*([0-9.]+)f,\s*Group::(\w+),\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\}')
    for key, name, share, group, help_text in pattern.findall(text):
        settings[key] = {"section": "relief", "group": groups[group], "label": name[0].upper() + name[1:],
                         "help": string_literal(help_text), "type": "float", "min": 0.0, "max": 3.0, "step": 0.05,
                         "decimals": 2, "engineMin": 0.0, "engineMax": 3.0,
                         "enabledWhen": [{"key": "Relief", "is": True}]}
        relief["settings"].append(key)


def ini_values_and_comments():
    """The shipped value of each key, and the comment block above it (shared by the keys right under it)."""
    values, comments = {}, {}
    block, owner = [], None
    for raw in (ROOT / "package" / "CoAVolFog.ini").read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith(";"):
            if owner is not None:
                block, owner = [], None
            block.append(line.lstrip("; ").rstrip())
        elif "=" in line and not line.startswith("["):
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
            comments[key.strip()] = " ".join(block).strip()
            owner = key
        else:
            block, owner = [], None
    return values, comments


def typed(value, entry):
    kind = entry["type"]
    if kind == "bool":
        return value.strip() not in ("0", "", "false")
    if kind in ("int", "choice"):
        return int(float(value))
    if kind in ("float", "viewDistance"):
        return float(value)
    return value


def build():
    consts = constants()
    fields, global_only = field_keys()
    sections, settings = panel(fields, consts)
    relief_types(settings, sections)
    values, comments = ini_values_and_comments()

    settings["FarClipMax"] = {
        "section": "world", "label": "View distance", "type": "viewDistance", "min": 184.0,
        "max": consts["kEngineFarClipMax"], "decimals": 0, "unit": "yd", "off": 0.0,
        "help": "The client caps the continents at 791 yd; the engine allows 1583. Costs about four times the loaded "
                "terrain. 0 keeps the client's cap. Turning it on when it was off at start-up needs a client restart",
    }
    next(s for s in sections if s["id"] == "world")["settings"].append("FarClipMax")

    for section_id, title in EXTRA_SECTIONS:
        sections.append({"id": section_id, "title": title, "settings": []})
    for key, section_id, label, kind, extra in EXTRAS:
        if key in settings:
            sec = next(s for s in sections if s["id"] == settings[key]["section"])
            sec["settings"].remove(key)
        entry = {"section": section_id, "label": label, "type": kind, "help": comments.get(key, "")}
        if key in settings and "help" not in extra:
            entry["help"] = settings[key]["help"]
        entry.update(extra)
        settings[key] = entry
        next(s for s in sections if s["id"] == section_id)["settings"].append(key)

    missing = [k for k in values if k not in settings and k not in HIDDEN_KEYS]
    if missing:
        raise SystemExit("ini keys with no setting in the schema: " + ", ".join(missing))
    for key, entry in settings.items():
        if key not in values:
            raise SystemExit(f"{key} is not in package/CoAVolFog.ini")
        entry["default"] = typed(values[key], entry)
        if key in global_only or entry["section"] in EXPERT_SECTIONS:
            entry["globalOnly"] = True
        entry["level"] = ("essential" if key in ESSENTIALS else
                          "expert" if entry["section"] in EXPERT_SECTIONS else "advanced")
    for section in sections:
        section["level"] = "expert" if section["id"] in EXPERT_SECTIONS else "advanced"

    order = [k for s in sections for k in s["settings"]]
    version = re.search(r"^## (\d+\.\d+\.\d+)", (ROOT / "CHANGELOG.md").read_text(encoding="utf-8"), re.M).group(1)
    return {
        "schemaVersion": SCHEMA_VERSION,
        "lumenveil": version,
        "essentials": ESSENTIALS,
        "sections": sections,
        "settings": {k: settings[k] for k in order},
    }


def check_presets(schema):
    presets = json.loads((ROOT / "settings" / "presets.json").read_text(encoding="utf-8"))
    ids = [p["id"] for p in presets["presets"]]
    if presets["default"] not in ids:
        sys.exit(f"presets.json: default {presets['default']} is not a preset")
    for preset in presets["presets"]:
        for key, value in preset["values"].items():
            entry = schema["settings"].get(key)
            if not entry:
                sys.exit(f"presets.json: {preset['id']} sets {key}, which is not a setting")
            kind = entry["type"]
            if kind == "bool" and not isinstance(value, bool):
                sys.exit(f"presets.json: {preset['id']}.{key} must be true or false")
            if kind in ("float", "int", "choice") and isinstance(value, bool):
                sys.exit(f"presets.json: {preset['id']}.{key} must be a number")
            if kind in ("float", "int") and not entry.get("engineMin", entry["min"]) <= value <= entry.get(
                    "engineMax", entry["max"]):
                sys.exit(f"presets.json: {preset['id']}.{key}={value} is out of range")
            if kind == "choice" and value not in [o["value"] for o in entry["options"]]:
                sys.exit(f"presets.json: {preset['id']}.{key}={value} is not one of its choices")
            if kind == "viewDistance" and value != entry["off"] and not entry["min"] <= value <= entry["max"]:
                sys.exit(f"presets.json: {preset['id']}.{key}={value} is out of range")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when settings/schema.json is out of date")
    args = parser.parse_args()
    schema = build()
    check_presets(schema)
    text = json.dumps(schema, indent=1, ensure_ascii=False) + "\n"
    if args.check:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            sys.exit("settings/schema.json is out of date: run python scripts/settings_schema.py")
        print("settings/schema.json is up to date")
        return
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(json.loads(text)['settings'])} settings")


if __name__ == "__main__":
    main()
