#!/usr/bin/env python3
"""Parse MobileGL's host-loader contract into machine-readable JSON.

TGL never *guesses* what a MobileGL host must export. MobileGL's own loader
(MobileGL/MG_Util/BackendLoaders/OpenGL/Loader.cpp) declares three groups:

  INIT_GLES_FUNC(name)           -> required GLES entry point (null = error log,
                                    DirectGLES may call it later => crash)
  INIT_GLES_FUNC_OPTIONAL(name)  -> extension entry point, null is expected
  INIT_EGL_FUNC(name)            -> required EGL entry point

This script re-derives those three sets straight from the upstream source so
the vendored snapshot in tools/mobilegl/host_contract.json can always be
re-verified (and regenerated) instead of trusted.

Usage:
  parse_host_contract.py --loader <path to Loader.cpp> [--json out.json]
  parse_host_contract.py --loader ... --compare tools/mobilegl/host_contract.json

Exit codes: 0 = ok / snapshot matches, 1 = mismatch, 2 = usage/IO error.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys

REQUIRED_GLES = re.compile(r"INIT_GLES_FUNC\((\w+)\)")
OPTIONAL_GLES = re.compile(r"INIT_GLES_FUNC_OPTIONAL\((\w+)\)")
REQUIRED_EGL = re.compile(r"INIT_EGL_FUNC\((\w+)\)")


def _dedupe_in_order(names):
    seen = set()
    out = []
    for name in names:
        if name not in seen:
            seen.add(name)
            out.append(name)
    return out


def parse_loader(text: str):
    """Split the loader body at AcquireEGLFunctions so each regex sees one macro."""
    gles_start = text.index("void AcquireGLESFunctions")
    egl_start = text.index("void AcquireEGLFunctions")
    gles_body, egl_body = text[gles_start:egl_start], text[egl_start:]
    required = _dedupe_in_order(REQUIRED_GLES.findall(gles_body))
    optional = _dedupe_in_order(OPTIONAL_GLES.findall(gles_body))
    # The macro *definitions* match the same regex; drop the literal "name".
    required = [n for n in required if n != "name"]
    optional = [n for n in optional if n != "name"]
    egl = [n for n in _dedupe_in_order(REQUIRED_EGL.findall(egl_body)) if n != "name"]
    return required, optional, egl


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--loader", required=True, help="path to MobileGL Loader.cpp")
    ap.add_argument("--json", help="write the parsed contract to this path")
    ap.add_argument("--compare", help="compare against an existing snapshot")
    args = ap.parse_args(argv)

    try:
        with open(args.loader, "rb") as fh:
            raw = fh.read()
    except OSError as exc:
        print(f"error: cannot read {args.loader}: {exc}", file=sys.stderr)
        return 2

    required, optional, egl = parse_loader(raw.decode("utf-8", "replace"))
    digest = hashlib.sha256(raw).hexdigest()
    contract = {
        "required_gles": required,
        "optional_gles": optional,
        "required_egl": egl,
        "counts": {
            "required_gles": len(required),
            "optional_gles": len(optional),
            "required_egl": len(egl),
        },
        "loader_sha256": digest,
    }

    if args.compare:
        with open(args.compare, "r", encoding="utf-8") as fh:
            snapshot = json.load(fh)
        mismatch = False
        for key in ("required_gles", "optional_gles", "required_egl"):
            if snapshot.get(key) != contract[key]:
                mismatch = True
                only_snapshot = sorted(set(snapshot.get(key, [])) - set(contract[key]))
                only_loader = sorted(set(contract[key]) - set(snapshot.get(key, [])))
                print(f"MISMATCH {key}: snapshot-only={only_snapshot} "
                      f"loader-only={only_loader}", file=sys.stderr)
        snap_hash = snapshot.get("provenance", {}).get("loader_sha256")
        if snap_hash and snap_hash != digest:
            print(f"note: upstream Loader.cpp changed (snapshot {snap_hash} "
                  f"!= local {digest})", file=sys.stderr)
        print("snapshot matches upstream loader" if not mismatch else "snapshot differs")
        return 1 if mismatch else 0

    print(json.dumps(contract, indent=2))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(contract, fh, indent=2)
            fh.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())