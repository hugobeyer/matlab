from copy import deepcopy
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
ICON_DIR = ROOT / "Resources" / "Icons"
OUTPUT = Path(__file__).with_name("icon-editing-grid-1024.svg")
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

def parse_viewbox(root):
    raw = root.get("viewBox")
    if raw:
        values = [float(value) for value in re.split(r"[ ,]+", raw.strip())]
        if len(values) == 4 and values[2] > 0 and values[3] > 0:
            return values
    def numeric(value):
        match = re.match(r"[0-9.]+", value or "64")
        return float(match.group(0)) if match else 64.0
    return [0.0, 0.0, numeric(root.get("width")), numeric(root.get("height"))]

def prefix_ids(elements, prefix):
    id_map = {}
    for element in elements:
        for descendant in element.iter():
            old_id = descendant.get("id")
            if old_id:
                id_map[old_id] = f"{prefix}-{old_id}"
    for element in elements:
        for descendant in element.iter():
            old_id = descendant.get("id")
            if old_id:
                descendant.set("id", id_map[old_id])
            for key, value in list(descendant.attrib.items()):
                if key == "id":
                    continue
                for old_id, new_id in id_map.items():
                    value = value.replace(f"url(#{old_id})", f"url(#{new_id})")
                    if value == f"#{old_id}":
                        value = f"#{new_id}"
                    value = value.replace(f" #{old_id} ", f" #{new_id} ")
                descendant.set(key, value)

def main():
    files = sorted(ICON_DIR.glob("*.svg"), key=lambda path: path.name.casefold())
    if len(files) > 256:
        raise RuntimeError("The 16x16 grid only holds 256 icons.")

    root = ET.Element(qname(SVG_NS, "svg"), {
        "width": "1024", "height": "1024", "viewBox": "0 0 1024 1024",
        "version": "1.1", "id": "mixtormat-icon-editing-grid"
    })
    ET.SubElement(root, qname(SVG_NS, "title")).text = "Mixtormat icon editing grid — 64 px cells"
    ET.SubElement(root, qname(SVG_NS, "desc")).text = (
        "Editable 16 by 16 grid. Each named icon group contains embedded SVG vector artwork."
    )

    grid = ET.SubElement(root, qname(SVG_NS, "g"), {
        "id": "grid-guides", "{http://www.inkscape.org/namespaces/inkscape}label": "Grid guides",
        "fill": "none", "stroke": "#70777b", "stroke-width": "0.5"
    })
    path_data = " ".join(
        [f"M {index * 64} 0 V 1024" for index in range(17)]
        + [f"M 0 {index * 64} H 1024" for index in range(17)]
    )
    ET.SubElement(grid, qname(SVG_NS, "path"), {"d": path_data})

    artwork = ET.SubElement(root, qname(SVG_NS, "g"), {
        "id": "icon-artwork", "{http://www.inkscape.org/namespaces/inkscape}label": "Icon artwork"
    })

    for index, path in enumerate(files):
        source_root = ET.parse(path).getroot()
        vx, vy, vw, vh = parse_viewbox(source_root)
        scale = min(64.0 / vw, 64.0 / vh)
        offset_x = (64.0 - vw * scale) * 0.5
        offset_y = (64.0 - vh * scale) * 0.5
        x = (index % 16) * 64
        y = (index // 16) * 64
        name = path.stem
        icon = ET.SubElement(artwork, qname(SVG_NS, "g"), {
            "id": f"icon-{name}",
            "{http://www.inkscape.org/namespaces/inkscape}label": name,
            "transform": (
                f"translate({x + offset_x:.6f},{y + offset_y:.6f}) "
                f"scale({scale:.9f}) translate({-vx:.6f},{-vy:.6f})"
            )
        })
        children = [
            deepcopy(child) for child in source_root
            if child.tag not in {qname(SODIPODI_NS, "namedview"), qname(SVG_NS, "title"), qname(SVG_NS, "desc")}
        ]
        prefix_ids(children, f"{name}-")
        for child in children:
            icon.append(child)

    ET.indent(root, space="  ")
    ET.ElementTree(root).write(OUTPUT, encoding="utf-8", xml_declaration=True)
    print(f"Wrote {OUTPUT} with {len(files)} named icon groups.")

if __name__ == "__main__":
    main()
