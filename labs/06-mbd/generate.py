#!/usr/bin/env python3
"""generate.py - model-to-C generator for the FDIR decision logic (Session 11).

Reads labs/06-mbd/fdir_model.txt (the single source of truth) and emits:

    python3 labs/06-mbd/generate.py            C source of the step functions   -> stdout
    python3 labs/06-mbd/generate.py --header   C header (types, enums, API)     -> stdout
    python3 labs/06-mbd/generate.py --trace    transition traceability (md)     -> stdout
    python3 labs/06-mbd/generate.py --write    write all three files next to the model
    python3 labs/06-mbd/generate.py --check    fail if a committed generated file differs
                                               from what the model generates (catches hand edits)

The generator validates the model before it writes anything and refuses to produce output for a
model that is incomplete or inconsistent (see validate()). It is deliberately small: it expands
guards and actions into an if / else-if chain per state, in the order the rows are written.
It does no analysis of the C expressions themselves; the C compiler does that.

Python standard library only. Output is deterministic (no timestamps), so --check is exact.
"""
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(HERE, "fdir_model.txt")
REQS_PATH = os.path.join(HERE, "requirements.md")
OUT_C = os.path.join(HERE, "fdir_step_gen.c")
OUT_H = os.path.join(HERE, "fdir_step_gen.h")
OUT_TRACE = os.path.join(HERE, "traceability.md")

CTYPES = {"u8": "uint8_t", "u16": "uint16_t"}
IDENT = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
# Requests to the outside world: model helper -> generated runtime function.
HELPERS = {
    "EMIT": "fdir_gen_emit",
    "REQ_RECOVER": "fdir_gen_req_recover",
    "REQ_ENTER_SAFE": "fdir_gen_req_enter_safe",
}
# The event snapshot and the effect records need these variables to exist.
REQUIRED_VARS = ("attempts", "source_mask", "task_mask")


class ModelError(Exception):
    pass


class Model:
    def __init__(self):
        self.text = ""
        self.sha = ""
        self.states = []        # [(name, code)]
        self.vars = []          # [(name, ctype key, init)]
        self.inputs = []        # [(name, ctype key)]
        self.lets = []          # [(name, expr)]
        self.conds = {}         # name -> expr
        self.actions = {}       # name -> [statements]
        self.always = []        # [action names]
        self.rows = []          # [dict]

    # ---- convenience ----
    @property
    def var_names(self):
        return {v[0] for v in self.vars}

    @property
    def input_names(self):
        return {i[0] for i in self.inputs}

    @property
    def let_names(self):
        return {l[0] for l in self.lets}

    def state_code(self, name):
        return dict(self.states)[name]


# ------------------------------------------------------------------------------ parsing

def strip_comment(line):
    line = line.rstrip("\n")
    if line.lstrip().startswith("#"):
        return ""
    return re.sub(r"\s+#.*$", "", line).strip()


def split_def(rest, lineno, what):
    if "=" not in rest:
        raise ModelError("line %d: %s needs 'name = value'" % (lineno, what))
    name, value = rest.split("=", 1)
    name, value = name.strip(), value.strip()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) or not value:
        raise ModelError("line %d: bad %s definition" % (lineno, what))
    return name, value


def split_names(field, lineno):
    field = field.strip()
    if field in ("", "-"):
        return []
    return [a.strip() for a in field.split(";") if a.strip()]


def parse(text):
    m = Model()
    m.text = text
    m.sha = hashlib.sha256(text.encode("utf-8")).hexdigest()
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = strip_comment(raw)
        if not line:
            continue
        if re.match(r"T\d+\s*,", line):
            parts = [p.strip() for p in line.split(",")]
            if len(parts) not in (5, 6):
                raise ModelError("line %d: transition needs id, state, cond, next, actions[, reqs]" % lineno)
            m.rows.append({
                "id": int(parts[0][1:]), "label": parts[0], "state": parts[1], "cond": parts[2],
                "next": parts[3], "actions": split_names(parts[4], lineno),
                "reqs": parts[5].split() if len(parts) == 6 else [], "line": lineno,
            })
            continue
        word, _, rest = line.partition(" ")
        rest = rest.strip()
        if word == "STATE":
            name, value = split_def(rest, lineno, "STATE")
            if not value.isdigit():
                raise ModelError("line %d: STATE code must be a number" % lineno)
            m.states.append((name, int(value)))
        elif word == "VAR":
            f = rest.split()
            if len(f) != 3 or f[1] not in CTYPES:
                raise ModelError("line %d: VAR needs 'name u8|u16 init'" % lineno)
            m.vars.append((f[0], f[1], f[2]))
        elif word == "INPUT":
            f = rest.split()
            if len(f) != 2 or f[1] not in CTYPES:
                raise ModelError("line %d: INPUT needs 'name u8|u16'" % lineno)
            m.inputs.append((f[0], f[1]))
        elif word == "LET":
            m.lets.append(split_def(rest, lineno, "LET"))
        elif word == "COND":
            name, value = split_def(rest, lineno, "COND")
            if name in m.conds or name in ("ANY", "GROUND_CMD"):
                raise ModelError("line %d: COND %s defined twice or reserved" % (lineno, name))
            m.conds[name] = value
        elif word == "ACTION":
            name, value = split_def(rest, lineno, "ACTION")
            if name in m.actions:
                raise ModelError("line %d: ACTION %s defined twice" % (lineno, name))
            m.actions[name] = [s.strip() for s in value.split(";") if s.strip()]
        elif word == "ALWAYS":
            m.always = split_names(rest, lineno)
        else:
            raise ModelError("line %d: unknown directive '%s'" % (lineno, word))
    return m


# ------------------------------------------------------------------------------ validation

def action_statements(m, names):
    out = []
    for n in names:
        out.extend(m.actions[n])
    return out


def count_effects(m, names):
    n = 0
    for stmt in action_statements(m, names):
        n += sum(stmt.count(h + "(") for h in HELPERS)
    return n


def cycle_rows(m, state):
    return [r for r in m.rows if r["state"] == state and r["cond"] != "GROUND_CMD"]


def ground_rows(m, state):
    return [r for r in m.rows if r["state"] == state and r["cond"] == "GROUND_CMD"]


def validate(m, known_reqs):
    names = [s[0] for s in m.states]
    codes = [s[1] for s in m.states]
    if not names:
        raise ModelError("no STATE declared")
    if len(set(names)) != len(names) or len(set(codes)) != len(codes):
        raise ModelError("duplicate state name or state code")
    if any(c > 255 for c in codes):
        raise ModelError("state codes must fit in 8 bits")
    for n in names:
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", n):
            raise ModelError("state names are upper case identifiers: %s" % n)

    declared = [v[0] for v in m.vars] + [i[0] for i in m.inputs] + [l[0] for l in m.lets]
    if len(set(declared)) != len(declared) or "state" in declared:
        raise ModelError("VAR / INPUT / LET names must be unique and may not be 'state'")
    for v in REQUIRED_VARS:
        if v not in m.var_names:
            raise ModelError("the model must declare VAR %s (the event snapshot uses it)" % v)

    ids = [r["id"] for r in m.rows]
    if len(set(ids)) != len(ids):
        raise ModelError("duplicate transition id")
    if any(not 1 <= i <= 255 for i in ids):
        raise ModelError("transition ids must be T1..T255")

    for r in m.rows:
        where = "%s (line %d)" % (r["label"], r["line"])
        if r["state"] not in names or r["next"] not in names:
            raise ModelError("%s: unknown state" % where)
        if r["cond"] not in ("ANY", "GROUND_CMD") and r["cond"] not in m.conds:
            raise ModelError("%s: unknown condition '%s'" % (where, r["cond"]))
        for a in r["actions"]:
            if a not in m.actions:
                raise ModelError("%s: unknown action '%s'" % (where, a))
        for q in r["reqs"]:
            if q not in known_reqs:
                raise ModelError("%s: requirement %s is not in requirements.md" % (where, q))
        if not r["reqs"]:
            raise ModelError("%s: every transition must cite at least one requirement" % where)
    for a in m.always:
        if a not in m.actions:
            raise ModelError("ALWAYS: unknown action '%s'" % a)

    for s in names:
        rows = cycle_rows(m, s)
        anys = [r for r in rows if r["cond"] == "ANY"]
        if len(anys) != 1 or rows[-1]["cond"] != "ANY":
            raise ModelError("state %s: the cycle rows must end with exactly one ANY row "
                             "(the model must be total; rows after ANY could never fire)" % s)
        if len(ground_rows(m, s)) > 1:
            raise ModelError("state %s: more than one GROUND_CMD row" % s)

    # The ground command has no inputs and no derived values: its rows must not use them.
    local_only = m.input_names | m.let_names
    for r in m.rows:
        if r["cond"] == "GROUND_CMD":
            toks = set()
            for stmt in action_statements(m, r["actions"]):
                toks |= set(IDENT.findall(stmt))
            bad = toks & local_only
            if bad:
                raise ModelError("%s: GROUND_CMD rows cannot use inputs or LET values: %s"
                                 % (r["label"], ", ".join(sorted(bad))))

    # Every state must be reachable from the initial one.
    reach, todo = {names[0]}, [names[0]]
    while todo:
        cur = todo.pop()
        for r in m.rows:
            if r["state"] == cur and r["next"] not in reach:
                reach.add(r["next"])
                todo.append(r["next"])
    if reach != set(names):
        raise ModelError("unreachable state(s): %s" % ", ".join(sorted(set(names) - reach)))

    # Warnings only: vocabulary that no transition uses.
    used_conds = {r["cond"] for r in m.rows}
    used_actions = {a for r in m.rows for a in r["actions"]} | set(m.always)
    for c in sorted(set(m.conds) - used_conds):
        sys.stderr.write("generate.py: warning: COND %s is never used\n" % c)
    for a in sorted(set(m.actions) - used_actions):
        sys.stderr.write("generate.py: warning: ACTION %s is never used\n" % a)


def read_requirement_ids():
    ids = set()
    with open(REQS_PATH, encoding="utf-8") as f:
        for line in f:
            mo = re.match(r"\|\s*(FDIR-REQ-\d+)\s*\|", line)
            if mo:
                ids.add(mo.group(1))
    return ids


# ------------------------------------------------------------------------------ expression rewriting

def rewrite(m, expr):
    """Model expression -> C. Context VARs become c->name; INPUTs and LETs are locals."""
    names = m.var_names

    def sub(mo):
        tok = mo.group(0)
        return "c->" + tok if tok in names else tok

    expr = IDENT.sub(sub, expr)
    for helper, fn in HELPERS.items():
        expr = re.sub(r"\b%s\(" % helper, fn + "(c, out, ", expr)
    return expr


def emit_actions(m, names, lines, indent):
    for stmt in action_statements(m, names):
        lines.append("%s%s;" % (indent, rewrite(m, stmt)))


# ------------------------------------------------------------------------------ outputs

def banner(m, what):
    return [
        "/* GENERATED FILE - DO NOT EDIT BY HAND.",
        " *",
        " * %s" % what,
        " * Source model : labs/06-mbd/fdir_model.txt",
        " * Model SHA-256: %s" % m.sha,
        " * Generator    : labs/06-mbd/generate.py",
        " * Regenerate   : python3 labs/06-mbd/generate.py --write",
        " *",
        " * A change in behaviour is a change to the model. Edit the model, regenerate, and commit the",
        " * model and the regenerated files together. `generate.py --check` fails if this file no",
        " * longer matches the model.",
        " */",
    ]


def max_effects(m):
    best = 1
    for r in m.rows:
        extra = m.always if r["cond"] != "GROUND_CMD" else []
        best = max(best, count_effects(m, r["actions"] + extra))
    return best


def gen_header(m):
    L = banner(m, "Interface of the FDIR step component generated from the model.")
    L += ["#ifndef FDIR_STEP_GEN_H", "#define FDIR_STEP_GEN_H", "", "#include <stdint.h>", ""]
    L.append("/* State codes: the value is what an event reports in its state field. */")
    L.append("typedef enum {")
    for i, (n, c) in enumerate(m.states):
        L.append("    FDIR_GEN_STATE_%s = %d%s" % (n, c, "," if i + 1 < len(m.states) else ""))
    L.append("} fdir_gen_state_t;")
    L.append("")
    L.append("#define FDIR_GEN_N_STATES %du" % len(m.states))
    for n, _ in m.states:
        L.append("#define FDIR_GEN_HAS_STATE_%s 1" % n)
    L += ["", "#define FDIR_GEN_N_TRANSITIONS %du" % len(m.rows),
          "#define FDIR_GEN_MAX_EFFECTS %du" % max_effects(m), ""]
    L += ["/* One cycle's inputs (INPUT lines of the model). */", "typedef struct {"]
    for n, t in m.inputs:
        L.append("    %s %s;" % (CTYPES[t], n))
    L += ["} fdir_gen_in;", ""]
    L += [
        "/* What a step asks the outside world to do, in order. The component performs none of it:",
        " * the integrator turns each effect into a PUS event or a call into the platform. */",
        "typedef enum {",
        "    FDIR_GEN_FX_NONE = 0,",
        "    FDIR_GEN_FX_EVENT = 1,      /* report a PUS event                          */",
        "    FDIR_GEN_FX_RECOVER = 2,    /* run one local recovery action (`action`)    */",
        "    FDIR_GEN_FX_ENTER_SAFE = 3  /* put the vehicle in its safe configuration   */",
        "} fdir_gen_fx_kind;",
        "",
        "typedef struct {",
        "    uint8_t  kind;         /* fdir_gen_fx_kind                                       */",
        "    uint16_t event_id;     /* EVENT: EVT_*                                           */",
        "    uint8_t  severity;     /* EVENT: SEV_*                                           */",
        "    uint8_t  worst;        /* EVENT: health value the event reports                  */",
        "    uint8_t  state;        /* EVENT: state after the transition (a state code)       */",
        "    uint8_t  attempts;     /* EVENT: snapshot of the context at the moment of the event */",
        "    uint8_t  source_mask;  /* EVENT, RECOVER, ENTER_SAFE: monitors responsible       */",
        "    uint8_t  task_mask;    /* EVENT: stalled tasks responsible                       */",
        "    uint8_t  action;       /* RECOVER: recovery rung                                 */",
        "} fdir_gen_effect;",
        "",
        "typedef struct {",
        "    uint8_t         n_effects;                            /* valid entries in effect[]       */",
        "    uint8_t         transition;                           /* id of the row that fired, 0 = none */",
        "    uint8_t         overflow;                             /* 1 if an effect did not fit (a bug) */",
        "    fdir_gen_effect effect[FDIR_GEN_MAX_EFFECTS];",
        "} fdir_gen_out;",
        "",
    ]
    L += ["/* Component state (VAR lines of the model). */", "typedef struct {",
          "    fdir_gen_state_t state;"]
    for n, t, _ in m.vars:
        L.append("    %s %s;" % (CTYPES[t], n))
    L += ["} fdir_gen_ctx;", ""]
    params = [(n, t) for n, t, init in m.vars if init == "PARAM"]
    plist = ", ".join("%s %s" % (CTYPES[t], n) for n, t in params)
    L.append("/* Puts the component in its initial state. */")
    L.append("void fdir_init_gen(fdir_gen_ctx *c%s);" % (", " + plist if plist else ""))
    L += ["",
          "/* One FDIR cycle. Returns the new state; the requests are in `out`. */",
          "fdir_gen_state_t fdir_step_gen(fdir_gen_ctx *c, const fdir_gen_in *in, fdir_gen_out *out);",
          "",
          "/* The ground exit command (GROUND_CMD rows). Returns 1 if a transition fired, 0 if the",
          " * command has no effect in the current state. */",
          "int fdir_exit_safe_gen(fdir_gen_ctx *c, fdir_gen_out *out);",
          "",
          "/* Names and transition table, for logs and for model-coverage reports. */",
          "const char *fdir_gen_state_name(fdir_gen_state_t s);",
          "unsigned fdir_gen_transition_id(unsigned index);     /* index < FDIR_GEN_N_TRANSITIONS */",
          "const char *fdir_gen_transition_text(unsigned index);",
          "", "#endif /* FDIR_STEP_GEN_H */", ""]
    return "\n".join(L)


def transition_text(r):
    return "%s %s --%s--> %s" % (r["label"], r["state"], r["cond"], r["next"])


def gen_c(m):
    L = banner(m, "Step functions of the FDIR state machine generated from the model.")
    L += ["", '#include "fdir_step_gen.h"',
          '#include "../05-fdir/fdir.h" /* HEALTH_*, EVT_*, SEV_*, RECOVERY_*, FDIR_MAX_ATTEMPTS: the shared interface constants */',
          ""]
    L += [
        "/* Runtime support: effects are appended in the order they are requested. */",
        "static fdir_gen_effect *fdir_gen_push(fdir_gen_out *out) {",
        "    fdir_gen_effect *e = NULL;",
        "    if (out->n_effects < FDIR_GEN_MAX_EFFECTS) {",
        "        e = &out->effect[out->n_effects];",
        "        out->n_effects++;",
        "        *e = (fdir_gen_effect){ 0 };",
        "    } else {",
        "        out->overflow = 1u;",
        "    }",
        "    return e;",
        "}",
        "",
        "static void fdir_gen_emit(const fdir_gen_ctx *c, fdir_gen_out *out,",
        "                          uint16_t event_id, uint8_t severity, uint8_t worst) {",
        "    fdir_gen_effect *e = fdir_gen_push(out);",
        "    if (e != NULL) {",
        "        e->kind        = FDIR_GEN_FX_EVENT;",
        "        e->event_id    = event_id;",
        "        e->severity    = severity;",
        "        e->worst       = worst;",
        "        e->state       = (uint8_t)c->state;",
        "        e->attempts    = c->attempts;",
        "        e->source_mask = c->source_mask;",
        "        e->task_mask   = c->task_mask;",
        "    }",
        "}",
        "",
        "static void fdir_gen_req_recover(const fdir_gen_ctx *c, fdir_gen_out *out,",
        "                                 uint8_t action, uint8_t sources) {",
        "    fdir_gen_effect *e = fdir_gen_push(out);",
        "    (void)c;",
        "    if (e != NULL) {",
        "        e->kind        = FDIR_GEN_FX_RECOVER;",
        "        e->action      = action;",
        "        e->source_mask = sources;",
        "    }",
        "}",
        "",
        "static void fdir_gen_req_enter_safe(const fdir_gen_ctx *c, fdir_gen_out *out, uint8_t sources) {",
        "    fdir_gen_effect *e = fdir_gen_push(out);",
        "    (void)c;",
        "    if (e != NULL) {",
        "        e->kind        = FDIR_GEN_FX_ENTER_SAFE;",
        "        e->source_mask = sources;",
        "    }",
        "}",
        "",
    ]

    L.append("static const unsigned char fdir_gen_ids[FDIR_GEN_N_TRANSITIONS] = {")
    L.append("    " + ", ".join("%du" % r["id"] for r in m.rows))
    L.append("};")
    L.append("")
    L.append("static const char *const fdir_gen_texts[FDIR_GEN_N_TRANSITIONS] = {")
    for r in m.rows:
        L.append('    "%s",' % transition_text(r))
    L.append("};")
    L.append("")
    L.append("unsigned fdir_gen_transition_id(unsigned index) {")
    L.append("    return (index < FDIR_GEN_N_TRANSITIONS) ? (unsigned)fdir_gen_ids[index] : 0u;")
    L.append("}")
    L.append("")
    L.append("const char *fdir_gen_transition_text(unsigned index) {")
    L.append("    return (index < FDIR_GEN_N_TRANSITIONS) ? fdir_gen_texts[index] : \"?\";")
    L.append("}")
    L.append("")
    L.append("const char *fdir_gen_state_name(fdir_gen_state_t s) {")
    L.append("    const char *name = \"?\";")
    L.append("    switch (s) {")
    for n, _ in m.states:
        L.append('    case FDIR_GEN_STATE_%s: name = "%s"; break;' % (n, n))
    L.append("    default: break;")
    L.append("    }")
    L.append("    return name;")
    L.append("}")
    L.append("")

    params = [(n, t) for n, t, init in m.vars if init == "PARAM"]
    plist = ", ".join("%s %s" % (CTYPES[t], n) for n, t in params)
    L.append("void fdir_init_gen(fdir_gen_ctx *c%s) {" % (", " + plist if plist else ""))
    L.append("    c->state = FDIR_GEN_STATE_%s;" % m.states[0][0])
    for n, _, init in m.vars:
        L.append("    c->%s = %s;" % (n, n if init == "PARAM" else init))
    L.append("}")
    L.append("")

    # ---- the cycle step ----
    L.append("fdir_gen_state_t fdir_step_gen(fdir_gen_ctx *c, const fdir_gen_in *in, fdir_gen_out *out) {")
    for n, t in m.inputs:
        L.append("    const %s %s = in->%s;" % (CTYPES[t], n, n))
    for n, expr in m.lets:
        L.append("    const unsigned %s = %s;" % (n, rewrite(m, expr)))
    for n, _ in m.inputs:
        L.append("    (void)%s;" % n)
    for n, _ in m.lets:
        L.append("    (void)%s;" % n)
    L += ["", "    out->n_effects = 0u;", "    out->transition = 0u;", "    out->overflow = 0u;", ""]
    L.append("    switch (c->state) {")
    for sname, _ in m.states:
        L.append("    case FDIR_GEN_STATE_%s:" % sname)
        rows = cycle_rows(m, sname)
        for i, r in enumerate(rows):
            if r["cond"] == "ANY":
                head = "        } else {" if i > 0 else "        {"
            else:
                guard = rewrite(m, m.conds[r["cond"]])
                head = ("        if (%s) {" if i == 0 else "        } else if (%s) {") % guard
            L.append(head)
            L.append("            /* %s */" % transition_text(r))
            L.append("            out->transition = %du;" % r["id"])
            L.append("            c->state = FDIR_GEN_STATE_%s;" % r["next"])
            emit_actions(m, r["actions"], L, "            ")
        L.append("        }")
        L.append("        break;")
    L.append("    default:")
    L.append("        break; /* not a state of the model: do nothing */")
    L.append("    }")
    L.append("")
    if m.always:
        L.append("    /* ALWAYS */")
        emit_actions(m, m.always, L, "    ")
        L.append("")
    L.append("    return c->state;")
    L.append("}")
    L.append("")

    # ---- the ground command ----
    L.append("int fdir_exit_safe_gen(fdir_gen_ctx *c, fdir_gen_out *out) {")
    L.append("    int taken = 0;")
    L += ["", "    out->n_effects = 0u;", "    out->transition = 0u;", "    out->overflow = 0u;", ""]
    L.append("    switch (c->state) {")
    for sname, _ in m.states:
        g = ground_rows(m, sname)
        if not g:
            continue
        r = g[0]
        L.append("    case FDIR_GEN_STATE_%s:" % sname)
        L.append("        /* %s */" % transition_text(r))
        L.append("        out->transition = %du;" % r["id"])
        L.append("        c->state = FDIR_GEN_STATE_%s;" % r["next"])
        emit_actions(m, r["actions"], L, "        ")
        L.append("        taken = 1;")
        L.append("        break;")
    L.append("    default:")
    L.append("        break; /* the command has no effect here */")
    L.append("    }")
    L.append("")
    L.append("    return taken;")
    L.append("}")
    L.append("")
    return "\n".join(L)


def gen_trace(m):
    L = ["# FDIR Model - Transition Traceability", "",
         "> GENERATED from `fdir_model.txt` (SHA-256 `%s`) by `generate.py`. Do not edit by hand;" % m.sha,
         "> change the model or `requirements.md` and regenerate.", "",
         "Forward: every model transition names the requirement(s) it realizes. Backward: every",
         "requirement below is realized by at least one transition, or is listed as having none.", "",
         "## Transitions", "",
         "| ID | State | Condition | Next state | Actions | Requirements |",
         "|---|---|---|---|---|---|"]
    for r in m.rows:
        acts = "; ".join(r["actions"]) if r["actions"] else "-"
        L.append("| %s | %s | %s | %s | %s | %s |" % (
            r["label"], r["state"], r["cond"], r["next"], acts, ", ".join(r["reqs"])))
    L += ["", "## Requirements", "", "| Requirement | Transitions |", "|---|---|"]
    by_req = {}
    for r in m.rows:
        for q in r["reqs"]:
            by_req.setdefault(q, []).append(r["label"])
    for q in sorted(read_requirement_ids()):
        L.append("| %s | %s |" % (q, ", ".join(by_req.get(q, [])) or "none (verified outside the model)"))
    L += ["", "Requirement texts and the tests that verify them: `requirements.md`.", ""]
    return "\n".join(L)


# ------------------------------------------------------------------------------ main

def load():
    with open(MODEL_PATH, encoding="utf-8") as f:
        text = f.read()
    m = parse(text)
    validate(m, read_requirement_ids())
    return m


def main(argv):
    mode = argv[1] if len(argv) > 1 else "--c"
    if mode not in ("--c", "--header", "--trace", "--write", "--check"):
        sys.stderr.write(__doc__)
        return 2
    try:
        m = load()
    except ModelError as e:
        sys.stderr.write("generate.py: model error: %s\n" % e)
        return 1
    outputs = [(OUT_C, gen_c(m)), (OUT_H, gen_header(m)), (OUT_TRACE, gen_trace(m))]
    if mode == "--c":
        sys.stdout.write(outputs[0][1])
    elif mode == "--header":
        sys.stdout.write(outputs[1][1])
    elif mode == "--trace":
        sys.stdout.write(outputs[2][1])
    elif mode == "--write":
        for path, content in outputs:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(content)
            sys.stderr.write("generate.py: wrote %s\n" % os.path.relpath(path))
    else:  # --check
        bad = []
        for path, content in outputs:
            try:
                with open(path, encoding="utf-8", newline="") as f:
                    on_disk = f.read()
            except OSError:
                on_disk = None
            if on_disk != content:
                bad.append(os.path.relpath(path))
        if bad:
            sys.stderr.write("generate.py: --check FAILED: %s differ from the model's output.\n"
                             "  Never edit generated files: change fdir_model.txt and run --write.\n"
                             % ", ".join(bad))
            return 1
        sys.stderr.write("generate.py: --check ok: generated files match model sha256 %s\n" % m.sha[:12])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
