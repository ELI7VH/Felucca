#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Target-side cost estimate of the hot DSP code, from the pi32v2 objdump listing tools/build.py writes
(build/felucca.dis). For each engine's render function and the audio ISR: the instructions
inside loops (address ranges closed by a reachable backward branch: the per-sample loops and what they
contain), and the hardware divides and calls in them. cost = the loop instructions, each weighted 4 per
level of nesting (an inner loop runs several times per sample) and 1 + 8 for a divide (many cycles). A static count, not a cycle count: it changes only when the compiled code changes,
so it is exact run to run, and it catches a render loop that grew (more work per sample, a new divide,
something not inlined any more). Compared with tests/target_budget.txt at +10 %. The host
CPU check (regress.c) is the primary one; this one sees the target compiler's code.
  tests/target_budget.py [DIS [BUDGET]]      BUDGET_UPDATE=1 rewrites BUDGET"""
import os
import re
import sys

FUNCS = ["analog_render", "digital_render", "digital_render_legacy", "digital_render_custom", "phase_render", "lofi_render", "sample_render", "formant_render",
         "trio_render", "trio_pass", "wheel_render", "wheel_block",
         "grain_render", "grain_block", "phys_render", "drum_render", "noise_render", "fm6_render", "fm6_op_run", "fm6_op_fb", "px_modal_block", "px_modal_run", "px_memb_block",
         "px_string_excite", "px_string_run", "px_symp_run",
         "dv_metal_run", "dv_kick_run", "dv_snare_run", "dv_clap_run", "dv_hat_run", "dv_tom_run",   # drum_voice.c
         "dv_rim_run", "dv_bell_run", "dv_cym_run", "dv_yama_run", "dv_run", "dv_out", "dv_metal_mix",
         "slicer_track",
         "slice_render", "slc_rev",                          # SLICE (eng_slice.c): the render, the reverse windows
         "fm1_alnk0_irq", "fm1_timer5_irq",               # the audio ISR; TIMER5: the key / LED scan (hal/fm1_input.h)
         "mod_begin", "mod_voice", "mod_end",                 # the modulation matrix (mod.c), called when active
         "perf_begin", "perf_mute", "perf_pre", "perf_block", "perf_master",   # the FX layer (perform.c), when busy
         "rev_room", "rev_spring"]                # the reverb bus (fx.c): REVERB TYPE ROOM / SPRING
# built only with FELUCCA_FM4=1 (DIGITAL, src/eng_digital.c; not in the default build, so not in BUDGET): absent,
# they are skipped; present, checked against these (their budget lines until the engine was retired in 1.0)
OPTIONAL = {"digital_render": 12, "digital_render_legacy": 333, "digital_render_custom": 558}
TOL = 0.10                      # exact (no noise): small edits pass, a grown render loop does not
DIV_W = 8                       # a divide weighs 1 + 8 instructions
NEST = 4                        # an instruction in a loop inside a loop weighs 4, two deep 16, ...
MAXD = 4

dis = sys.argv[1] if len(sys.argv) > 1 else "build/felucca.dis"
budget = sys.argv[2] if len(sys.argv) > 2 else "tests/target_budget.txt"
LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9.]*):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
TARGET = re.compile(r"goto -?\d+ <[^>]*: ([0-9a-f]+) >")
CONDITIONAL = re.compile(r"^ifs?\b")
RETURN = re.compile(r"(?:\{[^}]*\bpc\b[^}]*\}|\bpc)\s*=\s*\[sp\+\+\]|\b(?:rts|rti|rtx|rte)\s*$")
INDIRECT = re.compile(r"\b(?:goto|tbb|tbh)\b|\bpc\s*=")


def functions(path):
    """{name: [(addr, text)]}: a function runs to the next label not starting with '.' (the compiler's
    jump-table labels are inside functions); jump-table data ('< n : 0x.. >') is left out"""
    out, cur, predicated = {}, None, 0
    with open(path) as f:
        for line in f:
            if line.strip().startswith("}"):
                predicated = max(0, predicated - 1)
            m = LABEL.match(line.strip())
            if m:
                name = m.group(1)
                if not name.startswith("."):
                    cur = out.setdefault(name, []) if name in FUNCS else None
                    predicated = 0
                continue
            m = INSN.match(line)
            if cur is not None and m and not m.group(3).lstrip().startswith("<"):
                text = m.group(3).strip()
                # pi32v2 also prints conditional execution as `if (...) {` followed
                # by indented instructions and an unaddressed `}`. A return/goto
                # inside that block is not an unconditional flow barrier.
                if predicated and (RETURN.search(text) or INDIRECT.search(text)):
                    text = "if (predicated) " + text
                cur.append((int(m.group(1), 16), text))
                if CONDITIONAL.match(text) and text.endswith("{"):
                    predicated += 1
    return out


def flow_graph(insns):
    """Instruction successors; None means an unresolved indirect branch.

    Calls fall through. Direct unconditional gotos and returns do not. Unknown
    jump-table destinations stay conservative: reaching one may reach any node,
    so an unparsed switch can never make us silently drop a real loop.
    """
    positions = {addr: i for i, (addr, _) in enumerate(insns)}
    graph = []
    for i, (_, text) in enumerate(insns):
        following = [i + 1] if i + 1 < len(insns) else []
        conditional = CONDITIONAL.match(text)
        target = TARGET.search(text)
        if target:
            destination = positions.get(int(target.group(1), 16))
            successors = [destination] if destination is not None else []
            graph.append(successors + following if conditional else successors)
        elif RETURN.search(text):
            graph.append(following if conditional else [])
        elif INDIRECT.search(text):
            graph.append(None)
        else:
            graph.append(following)
    return positions, graph


def reachable(graph, start, end):
    pending, seen = [start], set()
    while pending:
        node = pending.pop()
        if node == end or graph[node] is None:
            return True
        if node not in seen:
            seen.add(node)
            pending.extend(graph[node])
    return False


def cost(insns):
    """loops: one span per loop head (the farthest backward branch to it); an instruction inside d
    spans weighs NEST ** (d - 1) (an inner loop runs several times per pass of the outer one)"""
    lo = insns[0][0]
    positions, graph = flow_graph(insns)
    heads = {}
    for a, t in insns:
        m = TARGET.search(t)
        if m and lo <= int(m.group(1), 16) < a:
            h = int(m.group(1), 16)
            # A cold block placed after a return may jump backward into the hot
            # path without forming a cycle. Only its reachable backedges close
            # loops; keep the existing span/nesting weights for those loops.
            if h in positions and reachable(graph, positions[h], positions[a]):
                heads[h] = max(heads.get(h, a), a)
    n = divs = calls = 0
    w = 0
    for a, t in insns:
        d = min(sum(1 for h, e in heads.items() if h <= a <= e), MAXD)
        if not d:
            continue
        k = NEST ** (d - 1)
        n += 1
        if re.search(r"= r\d+ / r\d+", t):
            divs += 1
            k *= 1 + DIV_W
        calls += t.startswith("call")
        w += k
    return {"insns": len(insns), "loop": n, "div": divs, "call": calls, "cost": w}


def main():
    if not os.path.exists(dis):
        print(f"target: skip ({dis} missing: run ./build.sh)")
        return 0
    fns = functions(dis)
    res = {n: cost(fns[n]) for n in FUNCS if fns.get(n)}
    missing = [n for n in FUNCS if n not in res and n not in OPTIONAL]
    base = {}
    if os.path.exists(budget):
        for line in open(budget):
            p = line.split()
            if len(p) >= 2 and not line.startswith("#"):
                base[p[0]] = int(p[1])
    if os.environ.get("BUDGET_UPDATE"):
        with open(budget, "w") as f:
            f.write("# FELUCCA target cost budget (tests/target_budget.py): instructions in the loops of each\n"
                    f"# function in build/felucca.dis, x{NEST} per nesting level, divides x{1 + DIV_W}. The check allows "
                    f"+{TOL * 100:.0f} %.\n# Rewritten by BUDGET_UPDATE=1.\n")
            for n, r in res.items():
                if n not in OPTIONAL:
                    f.write(f"{n} {r['cost']}\n")
        print(f"target: budget {budget} rewritten ({len(res)} functions)")
    fail = 0
    for n in missing:
        print(f"target: FAIL {n} not found in {dis} (renamed? inlined? update FUNCS)")
        fail += 1
    for n, r in res.items():
        b = base.get(n, OPTIONAL.get(n))
        state = "no budget (BUDGET_UPDATE=1 adds it)" if b is None else "ok"
        if b is not None and not os.environ.get("BUDGET_UPDATE"):
            if r["cost"] > b * (1 + TOL):
                state = f"OVER BUDGET (+{(r['cost'] / b - 1) * 100:.0f} %, limit +{TOL * 100:.0f} %)"
                fail += 1
            elif r["cost"] < b * (1 - TOL):
                state = f"note: {(r['cost'] / b - 1) * 100:.0f} % (BUDGET_UPDATE=1 to keep it)"
        print(f"target: {n:15s} {r['insns']:5d} instructions, {r['loop']:4d} in loops, {r['div']} divides, "
              f"{r['call']:2d} calls there: cost {r['cost']:5d} (budget {b if b is not None else '-'}) {state}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
