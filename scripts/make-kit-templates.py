#!/usr/bin/env python3
"""
Make the simulator's kit templates from the bundled demos: same drum program and samples, no notes.

    python3 scripts/make-kit-templates.py

Kick/demos/AwesomeKick Groove -> Kick/templates/Electronic Kit
Kick/demos/Rusty Kit          -> Kick/templates/Acoustic Kit

A new project made from a template starts with every pad assigned and an empty 4-bar sequence.
The sample paths keep "@PROJECT_DIR@"; the simulator fills in the new project's folder.

SPDX-License-Identifier: AGPL-3.0-or-later
"""
import os
import shutil
import xml.etree.ElementTree as ET

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Kick")
KITS = {"AwesomeKick Groove": "Electronic Kit", "Rusty Kit": "Acoustic Kit"}

for demo, name in KITS.items():
    src = os.path.join(ROOT, "demos", demo)
    dst = os.path.join(ROOT, "templates", name)
    shutil.rmtree(dst, ignore_errors=True)
    os.makedirs(dst)
    shutil.copytree(os.path.join(src, "Samples"), os.path.join(dst, "Samples"))
    shutil.copy(os.path.join(src, demo + ".png"), os.path.join(dst, name + ".png"))

    tree = ET.parse(os.path.join(src, demo + ".kickPrj"))
    for notelist in tree.getroot().iter("NOTELIST"):
        for note in list(notelist):
            notelist.remove(note)
    for seq in tree.getroot().iter("SEQUENCE"):
        seq.set("name", "Sequence1")
        seq.set("length", "4")
        seq.set("loopEnd", "4")
    tree.write(os.path.join(dst, name + ".kickPrj"), encoding="UTF-8", xml_declaration=True)
    print("%-16s <- %s" % (name, demo))
