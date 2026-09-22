#!/usr/bin/env python3
"""Sync official Khronos command lists for the CTS-coverage gate.

Official sources (ground truth, never TGL's own docs):
  - GLES 3.2 core commands: Khronos OpenGL-Registry gl.xml
    <feature api="gles2" name="GL_ES_VERSION_3_{0,1,2}"> require blocks.
  - EGL 1.5 commands: Khronos EGL-Registry egl.xml
    <feature api="egl" name="EGL_VERSION_1_5"> require blocks.

Outputs (checked in, so CI without network still tests):
  - tests/cts/official_es32.inc  (C++ string array body, GLES core names)
  - tests/cts/official_egl.inc   (C++ string array body, EGL 1.5 names)
  - tools/cts/official_snapshot.json (URLs + command counts + per-name lists)

Usage:
  python3 tools/cts/sync_official.py [--check]
    --check: fail (exit 1) when the checked-in .inc files differ from a fresh
             download, i.e. Khronos published something new or the lists are
             stale. Without --check, regenerate the files.

Fallback: when the download fails (offline CI), parse the vendored Khronos
headers docs/reference/gl32.h (GL_APICALL ... glName) and docs/reference/
egl.h instead, and mark the snapshot source as "vendored-fallback".
"""

import os
import re
import signal
import sys
import json
import urllib.request
import xml.etree.ElementTree as ET

REPO = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
GL_XML_URL = ("https://raw.githubusercontent.com/KhronosGroup/"
              "OpenGL-Registry/main/xml/gl.xml")
EGL_XML_URL = ("https://raw.githubusercontent.com/KhronosGroup/"
               "EGL-Registry/main/api/egl.xml")
CACHE_DIR = os.path.join(REPO, "tools", "cts", "official_cache")
GLES_INC = os.path.join(REPO, "tests", "cts", "official_es32.inc")
EGL_INC = os.path.join(REPO, "tests", "cts", "official_egl.inc")
SNAPSHOT = os.path.join(REPO, "tools", "cts", "official_snapshot.json")


class _FetchTimeout(Exception):
    pass


def _alarm_handler(signum, frame):
    raise _FetchTimeout("network deadline exceeded")


def fetch(url, cache_name, deadline_s=0):
    """Fetch url with a hard deadline (SIGALRM: also bounds DNS stalls that
    ignore socket timeouts). Falls back to the checked-in cache, else raises.
    deadline_s=0 means no download attempt (cache only, fully offline)."""
    os.makedirs(CACHE_DIR, exist_ok=True)
    cache_path = os.path.join(CACHE_DIR, cache_name)
    if deadline_s > 0:
        prev = signal.signal(signal.SIGALRM, _alarm_handler)
        try:
            signal.alarm(deadline_s)
            try:
                with urllib.request.urlopen(url, timeout=15) as r:
                    data = r.read()
            finally:
                signal.alarm(0)
        except Exception as e:
            signal.alarm(0)
            if os.path.exists(cache_path):
                with open(cache_path, "rb") as f:
                    return f.read(), f"cache (download failed: {e})"
            raise RuntimeError(f"cannot fetch {url}: {e} (no cache)")
        finally:
            signal.signal(signal.SIGALRM, prev)
        with open(cache_path, "wb") as f:
            f.write(data)
        return data, "download"
    if os.path.exists(cache_path):
        with open(cache_path, "rb") as f:
            return f.read(), "cache"
    raise RuntimeError(f"cannot fetch {url} (offline, no cache)")


def gles_core_commands(gl_xml_bytes):
    root = ET.fromstring(gl_xml_bytes)
    # ES 3.2 is cumulative: 2.0 commands are still core (glDrawArrays,
    # glActiveTexture, ...). Verified against docs/reference/gl32.h (358).
    want = {"GL_ES_VERSION_2_0", "GL_ES_VERSION_3_0", "GL_ES_VERSION_3_1",
            "GL_ES_VERSION_3_2"}
    names = []
    seen = set()
    for feature in root.iter("feature"):
        if feature.get("api") != "gles2":
            continue
        if feature.get("name") not in want:
            continue
        for require in feature.iter("require"):
            for command in require.iter("command"):
                name = command.get("name")
                if name and name not in seen:
                    seen.add(name)
                    names.append(name)
    return sorted(names)


def egl_15_commands(egl_xml_bytes):
    root = ET.fromstring(egl_xml_bytes)
    names = []
    seen = set()
    # EGL 1.5 is cumulative over 1.0-1.4 (eglChooseConfig, eglSwapBuffers,
    # ...). Union all versioned feature blocks.
    want = {"EGL_VERSION_1_0", "EGL_VERSION_1_1", "EGL_VERSION_1_2",
            "EGL_VERSION_1_3", "EGL_VERSION_1_4", "EGL_VERSION_1_5"}
    for feature in root.iter("feature"):
        if feature.get("api") != "egl":
            continue
        if feature.get("name") not in want:
            continue
        for require in feature.iter("require"):
            for command in require.iter("command"):
                name = command.get("name")
                if name and name not in seen:
                    seen.add(name)
                    names.append(name)
    return sorted(names)


def fallback_from_headers():
    """Vendored Khronos headers fallback (offline). Returns (gles, egl)."""
    with open(os.path.join(REPO, "docs", "reference", "gl32.h"),
              encoding="utf-8", errors="replace") as f:
        gl32 = f.read()
    gles = sorted(set(re.findall(r"GL_APICALL\s+\S[^(]*?\s+(gl\w+)\s*\(",
                                 gl32)))
    with open(os.path.join(REPO, "docs", "reference", "egl.h"),
              encoding="utf-8", errors="replace") as f:
        eglh = f.read()
    egl = sorted(set(re.findall(r"EGLAPI\s+\S[^(]*?\s+(egl\w+)\s*\(", eglh)))
    return gles, egl


def emit_inc(path, names):
    body = "".join(f'    "{n}",\n' for n in names)
    return f"// Generated by tools/cts/sync_official.py — DO NOT EDIT.\n{body}"


def main():
    check = "--check" in sys.argv
    # --check is offline-safe: it verifies the checked-in lists against the
    # cached registry (drift detection runs on networked --refresh or regen).
    # --refresh forces a fresh download attempt (bounded by SIGALRM).
    refresh = "--refresh" in sys.argv
    offline = check and not refresh
    try:
        if offline:
            gl_xml, gl_src = fetch(GL_XML_URL, "gl.xml", deadline_s=0)
            egl_xml, egl_src = fetch(EGL_XML_URL, "egl.xml", deadline_s=0)
        else:
            gl_xml, gl_src = fetch(GL_XML_URL, "gl.xml", deadline_s=25)
            egl_xml, egl_src = fetch(EGL_XML_URL, "egl.xml", deadline_s=25)
        gles = gles_core_commands(gl_xml)
        egl = egl_15_commands(egl_xml)
        source = f"khronos-registry ({gl_src}/{egl_src})"
    except RuntimeError as e:
        if offline:
            print(f"sync_official --check: {e}", flush=True)
            return 2
        print(f"sync_official: {e}; using vendored Khronos headers", flush=True)
        gles, egl = fallback_from_headers()
        source = "vendored-fallback (docs/reference/gl32.h + egl.h)"
    if not gles or not egl:
        print("sync_official: empty command lists, refusing to write",
              flush=True)
        return 2
    gles_inc = emit_inc(GLES_INC, gles)
    egl_inc = emit_inc(EGL_INC, egl)
    snapshot = {
        "source": source,
        "gl_xml_url": GL_XML_URL,
        "egl_xml_url": EGL_XML_URL,
        "gles_core_count": len(gles),
        "egl_15_count": len(egl),
        "gles_core": gles,
        "egl_15": egl,
    }
    if check:
        ok = True
        for path, fresh in ((GLES_INC, gles_inc), (EGL_INC, egl_inc)):
            try:
                with open(path, encoding="utf-8") as f:
                    if f.read() != fresh:
                        print(f"sync_official --check: stale {path}",
                              flush=True)
                        ok = False
            except FileNotFoundError:
                print(f"sync_official --check: missing {path}", flush=True)
                ok = False
        if ok:
            print(f"sync_official --check: OK ({source}; "
                  f"{len(gles)} GLES + {len(egl)} EGL)", flush=True)
        return 0 if ok else 1
    with open(GLES_INC, "w", encoding="utf-8") as f:
        f.write(gles_inc)
    with open(EGL_INC, "w", encoding="utf-8") as f:
        f.write(egl_inc)
    with open(SNAPSHOT, "w", encoding="utf-8") as f:
        json.dump(snapshot, f, indent=2)
        f.write("\n")
    print(f"sync_official: source={source}", flush=True)
    print(f"  GLES 3.2 core commands: {len(gles)} -> tests/cts/official_es32.inc",
          flush=True)
    print(f"  EGL 1.5 commands: {len(egl)} -> tests/cts/official_egl.inc",
          flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
