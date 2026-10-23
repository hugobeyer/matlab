from copy import deepcopy
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
GRID = Path(__file__).with_name("icon-editing-grid-1024.svg")
ICON_DIR = ROOT / "Resources" / "Icons"
SVG_NS = "http://www.w3.org/2000/svg"
INKSCAPE_NS = "http://www.inkscape.org/namespaces/inkscape"
SODIPODI_NS = "http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
XLINK_NS = "http://www.w3.org/1999/xlink"
ET.register_namespace("", SVG_NS)
ET.register_namespace("inkscape", INKSCAPE_NS)
ET.register_namespace("sodipodi", SODIPODI_NS)
ET.register_namespace("xlink", XLINK_NS)

def qname(namespace, name):
    return f"{{{namespace}}}{name}"

def main():
    grid_root = ET.parse(GRID).getroot()
    icon_groups = [
        group for group in grid_root.iter(qname(SVG_NS, "g"))
        if group.get("id", "").startswith("icon-") and group.get("id") != "icon-artwork"
    ]
    expected = {path.stem for path in ICON_DIR.glob("*.svg")}
    names = {group.get("id")[5:] for group in icon_groups}
    if names != expected:
        raise RuntimeError(
            f"Grid/source mismatch. Missing groups: {sorted(expected - names)}; "
            f"unexpected groups: {sorted(names - expected)}"
        )

    for index, group in enumerate(icon_groups):
        name = group.get("id")[5:]
        cell_x = (index % 16) * 64
        cell_y = (index // 16) * 64
        root = ET.Element(qname(SVG_NS, "svg"), {
            "width": "64", "height": "64", "viewBox": "0 0 64 64",
            "version": "1.1", "id": name
        })
        ET.SubElement(root, qname(SVG_NS, "title")).text = name
        cell_offset = ET.SubElement(root, qname(SVG_NS, "g"), {
            "id": f"{name}-cell-offset",
            "transform": f"translate({-cell_x},{-cell_y})"
        })
        cell_offset.append(deepcopy(group))
        ET.indent(root, space="  ")
        ET.ElementTree(root).write(
            ICON_DIR / f"{name}.svg", encoding="utf-8", xml_declaration=True
        )

    print(f"Exported {len(icon_groups)} edited icon groups to {ICON_DIR}.")

if __name__ == "__main__":
    main()
