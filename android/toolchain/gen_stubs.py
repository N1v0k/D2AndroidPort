#!/usr/bin/env python3
"""Generate NDK-style stub sources + version scripts from AOSP .map.txt files.

Same idea as the NDK's ndkstubgen: every exported symbol of a system library that
is available to apps at the given API level becomes an empty function (or a
variable), compiled into a stub .so with the right SONAME. Apps link against the
stub; at run time the device's real library is used.

usage: gen_stubs.py <map.txt> <arch> <api> <out.c> <out.version-script>
"""
import re
import sys

CODENAMES = {
    "G": 9, "I": 14, "J": 16, "J-MR1": 17, "J-MR2": 18, "K": 19, "L": 21, "L-MR1": 22,
    "M": 23, "N": 24, "N-MR1": 25, "O": 26, "O-MR1": 27, "P": 28, "Q": 29, "R": 30,
    "S": 31, "Sv2": 32, "Tiramisu": 33, "UpsideDownCake": 34, "VanillaIceCream": 35,
    "Baklava": 36,
}
ARCHES = {"arm", "arm64", "x86", "x86_64", "riscv64"}
EXCLUDE_TAGS = {"platform-only", "apex", "llndk", "systemapi", "future", "vndk"}


def api_of(value):
    if value.isdigit():
        return int(value)
    if value in CODENAMES:
        return CODENAMES[value]
    if value == "future":
        return 10000
    raise ValueError("unknown API level " + value)


def parse_tags(comment, arch):
    tags = comment.split()
    info = {"arches": set(), "introduced": 0, "var": False, "weak": False, "excluded": False}
    for t in tags:
        if t in ARCHES:
            info["arches"].add(t)
        elif t.startswith("introduced=") or t.startswith("introduced-" + arch + "="):
            info["introduced"] = api_of(t.split("=", 1)[1])
        elif t.startswith("introduced-"):
            pass  # another arch's level
        elif t == "var":
            info["var"] = True
        elif t == "weak":
            info["weak"] = True
        elif t.split("=")[0] in EXCLUDE_TAGS:
            info["excluded"] = True
    return info


def main():
    map_path, arch, api, out_c, out_vs = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
    text = open(map_path).read()
    versions = []  # (name, base, [(sym, info)])
    block_re = re.compile(r"^\s*([A-Za-z0-9_]+)\s*\{\s*(#.*)?$")
    cur = None
    section = "global"
    for raw in text.splitlines():
        line = raw.rstrip()
        if not line.strip() or line.strip().startswith("#"):
            continue
        m = block_re.match(line)
        if m and cur is None:
            binfo = parse_tags((m.group(2) or "").lstrip("#"), arch)
            cur = {"name": m.group(1), "info": binfo, "syms": [], "base": None}
            section = "global"
            continue
        if cur is not None and line.strip().startswith("}"):
            rest = line.strip()[1:].strip().rstrip(";").strip()
            cur["base"] = rest or None
            versions.append(cur)
            cur = None
            continue
        s = line.strip()
        if s in ("global:", "local:"):
            section = s[:-1]
            continue
        if cur is None or section != "global":
            continue
        sym, _, comment = s.partition("#")
        sym = sym.strip().rstrip(";").strip()
        if not sym or sym == "*":
            continue
        if sym.startswith("extern "):
            continue  # C++ patterns: not needed for C code
        cur["syms"].append((sym, parse_tags(comment, arch)))

    c_lines = ["/* generated stub: do not ship */"]
    vs_lines = []
    seen = set()

    def wanted(v):
        name, binfo = v["name"], v["info"]
        if name.endswith("_PRIVATE") or name.endswith("_PLATFORM") or binfo["excluded"]:
            return False
        if binfo["arches"] and arch not in binfo["arches"]:
            return False
        return binfo["introduced"] <= api

    emitted = {v["name"] for v in versions if wanted(v)}
    for v in versions:
        if not wanted(v):
            continue
        if v["base"] and v["base"] not in emitted:
            v["base"] = None
        name, binfo = v["name"], v["info"]
        kept = []
        for sym, info in v["syms"]:
            if info["excluded"]:
                continue
            if info["arches"] and arch not in info["arches"]:
                continue
            if max(info["introduced"], binfo["introduced"]) > api:
                continue
            kept.append(sym)
            if sym in seen:
                continue
            seen.add(sym)
            weak = "__attribute__((weak)) " if info["weak"] else ""
            if info["var"]:
                c_lines.append("%sint %s = 0;" % (weak, sym))
            else:
                c_lines.append("%svoid %s(void) {}" % (weak, sym))
        body = "  global:\n" + "".join("    %s;\n" % s for s in kept) if kept else ""
        base = (" " + v["base"]) if v["base"] else ""
        vs_lines.append("%s {\n%s}%s;\n" % (name, body, base))
    open(out_c, "w").write("\n".join(c_lines) + "\n")
    open(out_vs, "w").write("".join(vs_lines))
    print("%s: %d symbols" % (map_path, len(seen)))


if __name__ == "__main__":
    main()
