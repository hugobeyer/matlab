"""Inspect/upgrade a DUPLICATE Mixtormat shared master in Unreal Editor 5.8.

No work runs on import. See upgrade_surface_optics.md for explicit invocations.
Only save_upgrade() saves. No source master, settings, functions or instances are edited.
"""

import json

import unreal


LIB = unreal.MaterialEditingLibrary
PREFIX = "Mixtormat.SurfaceOptics.v1/"
SOURCE_TAG = "Mixtormat.SurfaceOptics.Source"
STATE_TAG = "Mixtormat.SurfaceOptics.State"
TEXTURES = ("DA_Optics", "DA_FuzzColorMap", "DA_Subsurface")
SHARED_TEXTURES = ("DA_BaseColor", "DA_Normal", "DA_RAMH", "DA_Height")
ENABLE = "DA_UseSurfaceOptics"
RENDERER_WARNING = (
    "Combined fuzz + per-pixel MFP Diffusion requires Substrate Adaptive GBuffer on a "
    "supported platform/rendering path. Blendable GBuffer prioritizes Fuzz over SSS and "
    "does not support per-pixel MFP Diffusion. Check Project Settings > Rendering and "
    "Material Editor > Substrate for simplification. This script does NOT inspect or "
    "change project settings, nor certify runtime/platform support."
)


def _require(condition, message):
    if not condition:
        raise RuntimeError("Mixtormat optical upgrade: " + message + " Nothing saved by this call.")


def _assets():
    return unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


def _api(required=None):
    # These methods are documented in the official 5.8 Python API, not guessed Slab properties.
    methods = required if required is not None else (
        "get_material_expressions", "get_material_expression_input_names",
        "get_material_expression_output_names", "get_inputs_for_material_expression",
        "get_input_node_output_name_for_material_expression", "duplicate_material_expression",
        "disconnect_material_expressions", "connect_material_expressions",
        "create_material_expression", "delete_material_expression", "refresh_material_editor",
        "get_material_property_input_node", "get_material_property_input_node_output_name",
        "recompile_material", "get_nanite_override_material", "get_texture_parameter_names",
        "get_scalar_parameter_names", "get_vector_parameter_names", "get_static_switch_parameter_names",
    )
    for name in methods:
        _require(callable(getattr(LIB, name, None)),
                 f"Missing MaterialEditingLibrary.{name}; use UE 5.8 or wire the duplicate "
                 "manually using the recipe in upgrade_surface_optics.md. No alternate API is assumed.")


def _material(path):
    asset = _assets().load_asset(path)
    _require(isinstance(asset, unreal.Material), f"Expected a base Material, not an instance: {path}.")
    return asset


def _nodes(material):
    expressions = list(LIB.get_material_expressions(material))
    result = {node.get_name(): node for node in expressions}
    _require(len(result) == len(expressions), "Expression names are not unique.")
    return result


def _inputs(node):
    names = list(LIB.get_material_expression_input_names(node))
    _require(all(names) and len(set(names)) == len(names),
             f"{node.get_name()} has unnamed/duplicate input pins; cannot address it safely.")
    return names


def _pin(node, label, output=False):
    names = (list(LIB.get_material_expression_output_names(node)) if output else _inputs(node))
    # Spaces/case only: do not infer aliases, pin indices, or undocumented Substrate properties.
    normalized = lambda value: "".join(value.lower().split())
    matches = [name for name in names if normalized(name) == normalized(label)]
    _require(len(matches) == 1,
             f"{node.get_name()} needs {'output' if output else 'input'} '{label}', "
             f"but reports {names}. Inspect and wire manually; no guessed pin was used.")
    return matches[0]


def _output(node):
    names = list(LIB.get_material_expression_output_names(node))
    _require(len(names) == 1, f"{node.get_name()} does not have one unambiguous output: {names}.")
    return names[0]


def _resolve(material, node):
    # Active-editor queries may return the editor's preview expression objects.
    original = _nodes(material).get(node.get_name())
    _require(original is not None and original.get_class() == node.get_class(),
             "Active editor returned an expression outside this material. Apply/close the "
             "duplicate editor and retry; do not save a partial upgrade.")
    return original


def _links(material, node):
    """Read exact input->(source, output), without assuming the input-set array is pin-indexed.

    5.8 documents a SET of upstream nodes, not input-index correspondence. Isolate each input
    on a disposable expression copy, refresh its editor graph, then query its sole connection.
    This also disambiguates one texture feeding several inputs through different output masks.
    Never disconnect any input on the artist's original node during inspection.
    """
    pins = _inputs(node)
    result = {}
    original_sources = {
        source.get_name() for source in LIB.get_inputs_for_material_expression(material, node)
        if source is not None
    }
    for pin in pins:
        probe = LIB.duplicate_material_expression(material, None, node)
        _require(probe is not None, f"Cannot duplicate {node.get_name()} to inspect '{pin}'.")
        try:
            probe.set_editor_property("desc", PREFIX + "probe")
            for other in pins:
                if other != pin:
                    LIB.disconnect_material_expressions(probe, other)
            LIB.refresh_material_editor(material)
            sources = [source for source in LIB.get_inputs_for_material_expression(material, probe)
                       if source is not None]
            _require(len(sources) <= 1,
                     f"Input isolation failed for {node.get_name()}.{pin}. Use manual wiring.")
            if not sources:
                result[pin] = None
                continue
            source = sources[0]
            output = LIB.get_input_node_output_name_for_material_expression(probe, source)
            _require(output is not None,
                     f"Cannot resolve the exact output mask for {node.get_name()}.{pin}.")
            canonical = _resolve(material, source)
            outputs = list(LIB.get_material_expression_output_names(canonical))
            _require(outputs.count(output) == 1,
                     f"Ambiguous output '{output}' on {canonical.get_name()}; use manual wiring.")
            # A shared upstream node can feed several pins, so a one-node set alone does
            # not prove isolation. Removing the retained pin must empty the probe completely.
            _require(LIB.disconnect_material_expressions(probe, pin),
                     f"Could not disconnect isolated input {node.get_name()}.{pin}; use manual wiring.")
            LIB.refresh_material_editor(material)
            remaining = [source for source in LIB.get_inputs_for_material_expression(material, probe)
                         if source is not None]
            _require(not remaining,
                     f"Input isolation left other connections on {node.get_name()}.{pin}; "
                     "cannot safely identify output masks. Use manual wiring.")
            result[pin] = [canonical.get_name(), output]
        finally:
            LIB.delete_material_expression(material, probe)
            LIB.refresh_material_editor(material)
    _require({link[0] for link in result.values() if link} == original_sources,
             f"Editor connection queries disagree for {node.get_name()}. Use manual wiring.")
    return result


def _open(material, editor_changes_applied):
    _require(editor_changes_applied,
             "Apply and close any edits in the DUPLICATE material editor first, then explicitly "
             "pass editor_changes_applied=True. Refresh discards unapplied editor-preview edits.")
    editors = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
    _require(editors.open_editor_for_assets([material]), "Could not open the duplicate material editor.")
    LIB.refresh_material_editor(material)


def _root(material, prop):
    node = LIB.get_material_property_input_node(material, prop)
    if node is None:
        return None
    canonical = _resolve(material, node)
    output = LIB.get_material_property_input_node_output_name(material, prop)
    _require(output is not None, f"Cannot inspect material property {prop}.")
    return [canonical.get_name(), output]


def _compile(material):
    errors = LIB.recompile_material(material)
    _require(errors is not None, "Recompile did not return the UE 5.8 error array; cannot certify saving.")
    _require(not errors, "Material compilation failed:\n" + "\n".join(str(error) for error in errors))
    LIB.refresh_material_editor(material)


def _duplicate(path):
    material = _material(path)
    source = _assets().get_metadata_tag(material, SOURCE_TAG)
    _require(source and source != material.get_path_name(),
             "Refusing an unmarked asset or the source master. Call duplicate_master() first.")
    _require(material.get_path_name().startswith("/Game/"), "The upgrade destination must be under /Game/.")
    return material


def inspect_material(path):
    """Read-only inventory; no graph mutation, editor refresh, compilation or saving."""
    # Inspection should still work when this engine cannot safely perform an upgrade.
    _api(("get_material_expressions", "get_material_expression_input_names",
          "get_material_expression_output_names"))
    material = _material(path)
    report = {
        "material": material.get_path_name(),
        "source": _assets().get_metadata_tag(material, SOURCE_TAG),
        "has_upgrade_state": bool(_assets().get_metadata_tag(material, STATE_TAG)),
        "automatic_upgrade_status": (
            "Not validated by inventory. upgrade_duplicate() performs topology/API preflight. "
            "Hidden/multi-Slab, profiles, Wrap/Volume and custom F0 require manual integration."
        ),
        "renderer_warning": RENDERER_WARNING,
        "nodes": [
            {"name": node.get_name(), "class": node.get_class().get_name(),
             "description": node.get_editor_property("desc"),
             "inputs": list(LIB.get_material_expression_input_names(node)),
             "outputs": list(LIB.get_material_expression_output_names(node))}
            for node in _nodes(material).values()
        ],
    }
    unreal.log(json.dumps(report, indent=2))
    return report


def duplicate_master(source_path, destination_path):
    """Create an unsaved /Game duplicate, or reuse ONLY our same-source marked duplicate."""
    _api()
    source = _material(source_path)
    _require(destination_path.startswith("/Game/") and "." not in destination_path,
             "Destination must be a /Game/Folder/Asset package path (no object suffix).")
    _require(source.get_path_name().split(".")[0] != destination_path, "Cannot overwrite the source master.")
    assets = _assets()
    if assets.does_asset_exist(destination_path):
        duplicate = _duplicate(destination_path)
        _require(assets.get_metadata_tag(duplicate, SOURCE_TAG) == source.get_path_name(),
                 "Destination belongs to a different source. Choose a new destination.")
        return duplicate
    # Refuse BEFORE creating an asset: an upgraded source carries stale ownership/state.
    _require(not assets.get_metadata_tag(source, STATE_TAG),
             "Source was already upgraded. Reuse its existing marked duplicate or an untouched source.")
    folder, name = destination_path.rsplit("/", 1)
    _require(bool(name), "Missing destination asset name.")
    duplicate = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(name, folder, source)
    _require(isinstance(duplicate, unreal.Material), "AssetTools could not duplicate the source material.")
    assets.set_metadata_tag(duplicate, SOURCE_TAG, source.get_path_name())

    unreal.log(f"Created UNSAVED duplicate: {duplicate.get_path_name()}. Source unchanged.")
    return duplicate


def _require_shared_parameters(material):
    # An optical upgrade must remain selectable by the live shared preview, not merely compile.
    names = {str(name) for name in LIB.get_texture_parameter_names(material)}
    missing = sorted(set(SHARED_TEXTURES) - names)
    _require(not missing,
             f"Shared master is missing texture parameters: {missing}. Preserve/wire the existing "
             "BC/Normal/RAMH/Height contract on the duplicate before upgrading; custom bake "
             "mappings do not supply the live preview contract. No replacement inputs are invented.")


def _textures(paths):
    _require(set(paths) == set(TEXTURES), f"Provide exactly these texture paths: {TEXTURES}.")
    textures = {}
    for parameter in TEXTURES:
        texture = _assets().load_asset(paths[parameter])
        _require(isinstance(texture, unreal.Texture2D), f"{parameter} needs a real Texture2D default.")
        _require(not texture.get_editor_property("srgb"), f"{parameter} must be linear, not sRGB.")
        _require(texture.get_editor_property("compression_settings") == unreal.TextureCompressionSettings.TC_HDR_F32,
                 f"{parameter} requires HDR F32 (RGBA float), matching the bake contract. "
                 "Use exported optical textures, not an 8-bit/BC6H placeholder.")
        _require(not texture.get_editor_property("compression_no_alpha"),
                 f"{parameter} must preserve alpha; Optics.A is the SSS amount.")
        _require(not texture.get_editor_property("virtual_texture_streaming"),
                 f"{parameter} must be a non-virtual texture, matching compositor render targets.")
        textures[parameter] = texture
    return textures


def _snapshot(material, monitored_names):
    nodes = _nodes(material)
    snapshot = {"nodes": {}, "all_nodes": {name: node.get_class().get_name() for name, node in nodes.items()}}
    for name in monitored_names:
        _require(name in nodes, f"Missing monitored expression {name}.")
        node = nodes[name]
        record = {"class": node.get_class().get_name(), "links": _links(material, node),
                  "desc": node.get_editor_property("desc")}
        if isinstance(node, unreal.MaterialExpressionSubstrateSlabBSDF):
            record["sss_type"] = str(node.get_editor_property("sub_surface_type"))
            profile = node.get_editor_property("subsurface_profile")
            record["profile"] = profile.get_path_name() if profile else None
        if node.get_editor_property("desc").startswith(PREFIX):
            if isinstance(node, unreal.MaterialExpressionTextureSampleParameter2D):
                texture = node.get_editor_property("texture")
                record.update(parameter=str(node.get_editor_property("parameter_name")),
                              texture=texture.get_path_name() if texture else None,
                              sampler=str(node.get_editor_property("sampler_type")))
                record["sampling"] = {name: str(node.get_editor_property(name)) for name in (
                    "automatic_view_mip_bias", "const_coordinate", "const_mip_value",
                    "mip_value_mode", "sampler_source", "gather_mode",
                )}
            if isinstance(node, unreal.MaterialExpressionScalarParameter):
                record.update(parameter=str(node.get_editor_property("parameter_name")),
                              default=node.get_editor_property("default_value"))
            if isinstance(node, unreal.MaterialExpressionLinearInterpolate):
                record["constants"] = [node.get_editor_property(name) for name in ("const_a", "const_b", "const_alpha")]
        snapshot["nodes"][name] = record
    snapshot["roots"] = {
        name: _root(material, getattr(unreal.MaterialProperty, name)) for name in (
            "MP_FRONT_MATERIAL", "MP_WORLD_POSITION_OFFSET", "MP_NORMAL", "MP_AMBIENT_OCCLUSION",
            "MP_OPACITY", "MP_OPACITY_MASK", "MP_EMISSIVE_COLOR",
        )
    }
    # Displacement input is not exposed by MaterialProperty in the documented 5.8 Python enum.
    # We never reconnect ANY root input, and never write displacement settings.
    snapshot["settings"] = {name: str(material.get_editor_property(name)) for name in (
        "blend_mode", "material_domain", "is_thin_surface", "tangent_space_normal",
        "enable_tessellation", "enable_displacement_fade", "use_material_attributes",
    )}
    profile = material.get_editor_property("subsurface_profile")
    snapshot["settings"]["profile"] = profile.get_path_name() if profile else None
    override = LIB.get_nanite_override_material(material)
    snapshot["settings"]["nanite_override"] = override.get_path_name() if override else None
    for property_name, fields in (
        ("displacement_scaling", ("magnitude", "center")),
        ("displacement_fade_range", ("start_size_pixels", "end_size_pixels")),
    ):
        value = material.get_editor_property(property_name)
        snapshot["settings"][property_name] = {field: value.get_editor_property(field) for field in fields}
    return snapshot


def _state(material):
    encoded = _assets().get_metadata_tag(material, STATE_TAG)
    return json.loads(encoded) if encoded else None


def _validate_state(material, state):
    _require(state and state.get("version") == 1, "No complete v1 upgrade state; do not save partial work.")
    _require(_snapshot(material, state["monitored"]) == state["snapshot"],
             "Graph/settings changed since upgrade. Refusing to overwrite artist edits or save "
             "unverified wiring. Inspect the duplicate; start from a fresh duplicate if needed.")
    _require_shared_parameters(material)
    _textures(state["textures"])


def upgrade_duplicate(path, texture_paths, *, editor_changes_applied=False):
    """Upgrade in memory; reruns verify/reuse tagged nodes and may update default textures.

    F0 = lerp(legacy F0, lerp(Optics.R, existing BaseColor, existing Metallic), enable).
    Fuzz amount/roughness/tint retain their original input chains at enable=0.
    SSS uses effective MFP = Subsurface.RGB * Optics.A in cm (not a coverage opacity).
    """
    _api()
    material = _duplicate(path)
    textures = _textures(texture_paths)
    _require_shared_parameters(material)
    _open(material, editor_changes_applied)
    unreal.log_warning(RENDERER_WARNING)
    state = _state(material)
    if state:
        _validate_state(material, state)
        changed = []
        try:
            for node in _nodes(material).values():
                if isinstance(node, unreal.MaterialExpressionTextureSampleParameter2D) and node.get_editor_property("desc").startswith(PREFIX):
                    parameter = str(node.get_editor_property("parameter_name"))
                    if parameter in textures:
                        changed.append((node, node.get_editor_property("texture")))
                        node.set_editor_property("texture", textures[parameter])
            _compile(material)
            state["textures"] = {key: value.get_path_name() for key, value in textures.items()}
            state["snapshot"] = _snapshot(material, state["monitored"])
            _assets().set_metadata_tag(material, STATE_TAG, json.dumps(state, sort_keys=True))
        except Exception:
            for node, original_texture in changed:
                node.set_editor_property("texture", original_texture)
            LIB.refresh_material_editor(material)
            raise
        unreal.log("Verified/reused tagged optical nodes; updated defaults. UNSAVED; source unchanged.")
        return material

    nodes = _nodes(material)
    _require(not any(node.get_editor_property("desc").startswith(PREFIX) for node in nodes.values()),
             "Partial/tagged upgrade without complete state. Discard the unsaved duplicate and "
             "start from a new destination; this script will not delete artist nodes.")
    names = set(str(name) for name in LIB.get_texture_parameter_names(material))
    names.update(str(name) for name in LIB.get_scalar_parameter_names(material))
    names.update(str(name) for name in LIB.get_vector_parameter_names(material))
    names.update(str(name) for name in LIB.get_static_switch_parameter_names(material))
    _require(not names.intersection((*TEXTURES, ENABLE)),
             "Optical parameter names already exist without our state. Inspect/wire manually; "
             "their ownership and existing connections are unknown.")
    _require(material.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_OPAQUE
             and material.get_editor_property("material_domain") == unreal.MaterialDomain.MD_SURFACE
             and not material.get_editor_property("is_thin_surface")
             and not material.get_editor_property("use_material_attributes"),
             "Only an opaque, non-thin Surface material with direct Front Material is supported.")
    _require(material.get_editor_property("subsurface_profile") is None,
             "Root has an assigned subsurface profile; per-pixel MFP would be ignored. "
             "Resolve the profile design on the duplicate manually; it is not cleared automatically.")
    _require(LIB.get_nanite_override_material(material) is None,
             "Nanite override would bypass this shared master. Resolve the override manually.")
    slabs = [node for node in nodes.values() if isinstance(node, unreal.MaterialExpressionSubstrateSlabBSDF)]
    _require(len(slabs) == 1, "Need exactly one top-level Slab. Layered/function-hidden topology requires manual integration.")
    slab = slabs[0]
    front = _root(material, unreal.MaterialProperty.MP_FRONT_MATERIAL)
    _require(front == [slab.get_name(), _output(slab)],
             "Slab must feed Front Material directly. Vertical/other operators require manual topology review.")
    _require(slab.get_editor_property("subsurface_profile") is None,
             "Slab has a profile; remove/reconcile it manually on the duplicate before using per-pixel MFP.")
    sss_type = slab.get_editor_property("sub_surface_type")
    _require(sss_type in (unreal.MaterialSubSurfaceType.MSS_NONE, unreal.MaterialSubSurfaceType.MSS_DIFFUSION),
             "Existing SSS is Wrap/Volume; changing it would alter the legacy branch. Integrate manually.")
    slab_links = _links(material, slab)
    slab_pins = {name: _pin(slab, name) for name in (
        "F0", "Diffuse Albedo", "Fuzz Amount", "Fuzz Roughness", "Fuzz Color", "SSS MFP",
    )}
    for name in ("F0", "Diffuse Albedo", "Fuzz Amount", "Fuzz Roughness", "Fuzz Color"):
        _require(slab_links[slab_pins[name]] is not None,
                 f"Slab '{name}' is unconnected. Connect its EXISTING/default value explicitly "
                 "on the duplicate first. Python exposes no reliable per-pin default; it is not invented here.")
    helper = nodes[slab_links[slab_pins["F0"]][0]]
    _require(isinstance(helper, unreal.MaterialExpressionSubstrateMetalnessToDiffuseAlbedoF0),
             "F0 is not directly driven by Metalness-To-DiffuseAlbedo-F0. Custom specular transforms require manual integration.")
    _require(slab_links[slab_pins["F0"]] == [helper.get_name(), _pin(helper, "F0", output=True)]
             and slab_links[slab_pins["Diffuse Albedo"]] == [helper.get_name(), _pin(helper, "Diffuse Albedo", output=True)],
             "Slab diffuse/F0 do not share the expected metalness conversion outputs.")
    helper_links = _links(material, helper)
    base = helper_links[_pin(helper, "Base Color")]
    metallic = helper_links[_pin(helper, "Metallic")]
    _require(base is not None and metallic is not None,
             "Connect the existing Base Color and Metallic values explicitly on the metalness helper.")
    if sss_type == unreal.MaterialSubSurfaceType.MSS_NONE:
        _require(slab_links[slab_pins["SSS MFP"]] is None,
                 "MSS_NONE has a dormant MFP input. Cannot enable it without changing the old branch; resolve manually.")
    else:
        _require(slab_links[slab_pins["SSS MFP"]] is not None,
                 "Existing Diffusion MFP must be explicitly connected so its legacy behavior can be retained.")
    # Copy the shared UV/mip sampling setup, not just UV0: tiling and normal/displacement alignment stay intact.
    ram_samples = [node for node in nodes.values()
                   if isinstance(node, unreal.MaterialExpressionTextureSampleParameter2D)
                   and str(node.get_editor_property("parameter_name")) == "DA_RAMH"]
    _require(len(ram_samples) == 1, "Need one top-level DA_RAMH TextureSampleParameter2D as the shared UV/mip source.")
    ram_sample = ram_samples[0]
    _require(ram_sample.get_editor_property("gather_mode") == unreal.TextureGatherMode.TGM_NONE,
             "DA_RAMH uses a texture gather instead of ordinary channel sampling. Integrate manually.")
    ram_links = _links(material, ram_sample)
    for pin, link in ram_links.items():
        if link is not None:
            _require("".join(pin.lower().split()) in ("uvs", "coordinates", "mipvalue", "ddx(uvs)", "ddy(uvs)", "applyviewmipbias"),
                     f"DA_RAMH has an unsupported external input '{pin}' (e.g. a texture object override). Integrate manually.")
    monitored = [slab.get_name(), helper.get_name(), ram_sample.get_name()]
    before = _snapshot(material, monitored)
    created = []
    expected = {}

    def make(role, cls):
        node = LIB.create_material_expression(material, cls, -1600, 300 * len(created))
        _require(node is not None, f"Cannot create {role}.")
        created.append(node)
        node.set_editor_property("desc", PREFIX + role)
        expected[node.get_name()] = {pin: None for pin in _inputs(node)}
        return node

    def wire(link, target, pin):
        source = _nodes(material)[link[0]]
        _require(LIB.connect_material_expressions(source, link[1], target, pin),
                 f"Could not connect {link} to {target.get_name()}.{pin}.")
        expected.setdefault(target.get_name(), dict(slab_links))[pin] = link

    def out(node, label=None):
        return [node.get_name(), _pin(node, label, output=True) if label else _output(node)]

    def lerp(role, a, b, alpha):
        node = make(role, unreal.MaterialExpressionLinearInterpolate)
        if a is None:
            # Only used for MSS_NONE: zero MFP means no scattering, not an invented Slab default.
            node.set_editor_property("const_a", 0.0)
        else:
            wire(a, node, _pin(node, "A"))
        wire(b, node, _pin(node, "B"))
        wire(alpha, node, _pin(node, "Alpha"))
        return out(node)

    try:
        samples = {}
        for parameter in TEXTURES:
            # Create fresh parameters (fresh identities), copying documented sampling settings
            # and inspected wires rather than duplicating DA_RAMH's parameter GUID.
            sample = make(parameter, unreal.MaterialExpressionTextureSampleParameter2D)
            for property_name in ("automatic_view_mip_bias", "const_coordinate", "const_mip_value",
                                  "mip_value_mode", "sampler_source", "gather_mode"):
                sample.set_editor_property(property_name, ram_sample.get_editor_property(property_name))
            expected[sample.get_name()] = {pin: None for pin in _inputs(sample)}
            for pin, link in ram_links.items():
                if link:
                    wire(link, sample, _pin(sample, pin))
            sample.set_editor_property("parameter_name", parameter)
            sample.set_editor_property("texture", textures[parameter])
            sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
            samples[parameter] = sample
        enable = make(ENABLE, unreal.MaterialExpressionScalarParameter)
        enable.set_editor_property("parameter_name", ENABLE)
        enable.set_editor_property("default_value", 0.0)
        enable.set_editor_property("slider_min", 0.0)
        enable.set_editor_property("slider_max", 1.0)
        optics = samples["DA_Optics"]
        metal_f0 = lerp("MetalAwareF0", out(optics, "R"), base, metallic)
        changes = {
            "F0": metal_f0,
            "Fuzz Amount": out(optics, "G"),
            "Fuzz Roughness": out(optics, "B"),
            "Fuzz Color": out(samples["DA_FuzzColorMap"], "RGB"),
        }
        mfp = make("EffectiveMFP_cm", unreal.MaterialExpressionMultiply)
        wire(out(samples["DA_Subsurface"], "RGB"), mfp, _pin(mfp, "A"))
        wire(out(optics, "A"), mfp, _pin(mfp, "B"))
        changes["SSS MFP"] = out(mfp)
        for name, optical in changes.items():
            pin = slab_pins[name]
            mixed = lerp("Select_" + name.replace(" ", ""), slab_links[pin], optical, out(enable))
            wire(mixed, slab, pin)
        slab.set_editor_property("sub_surface_type", unreal.MaterialSubSurfaceType.MSS_DIFFUSION)
        LIB.refresh_material_editor(material)
        for name, links in expected.items():
            _require(_links(material, _nodes(material)[name]) == links,
                     f"Connection readback mismatch for {name}; cannot trust the edited graph.")
        # Every non-optical input (normal, roughness, F90, etc.), helper input and root is retained.
        after_existing = _snapshot(material, monitored)
        for name in monitored:
            expected_links = expected.get(name, before["nodes"][name]["links"])
            _require(after_existing["nodes"][name]["links"] == expected_links,
                     f"Protected inputs changed on {name}.")
        _require(after_existing["roots"] == before["roots"] and after_existing["settings"] == before["settings"],
                 "Protected root connections/material settings changed.")
        _compile(material)
        monitored.extend(node.get_name() for node in created)
        state = {
            "version": 1, "monitored": monitored,
            "textures": {key: value.get_path_name() for key, value in textures.items()},
            "snapshot": _snapshot(material, monitored),
        }
        _assets().set_metadata_tag(material, STATE_TAG, json.dumps(state, sort_keys=True))
    except Exception as error:
        # Restore ONLY the five inputs we own, and remove ONLY nodes created by this invocation.
        # If the editor bridge cannot restore, report it and leave the duplicate unsaved.
        try:
            for name in ("F0", "Fuzz Amount", "Fuzz Roughness", "Fuzz Color", "SSS MFP"):
                pin = slab_pins[name]
                link = slab_links[pin]
                if link:
                    _require(LIB.connect_material_expressions(nodes[link[0]], link[1], slab, pin), "Rollback connection failed.")
                else:
                    LIB.disconnect_material_expressions(slab, pin)
            slab.set_editor_property("sub_surface_type", sss_type)
            for node in reversed(created):
                LIB.delete_material_expression(material, node)
            LIB.refresh_material_editor(material)
            _require(_snapshot(material, before["nodes"]) == before, "Rollback readback differs from the original duplicate.")
        except Exception as restore_error:
            raise RuntimeError(f"{error}\nRollback also failed: {restore_error}\n"
                               "DO NOT SAVE the duplicate. Discard unsaved changes/start a new destination.") from error
        raise
    unreal.log("Optical graph upgraded and compiler returned no errors. UNSAVED. "
               "Inspect the duplicate and call save_upgrade() explicitly; no master replacement performed.")
    return material


def save_upgrade(path, *, editor_changes_applied=False):
    """Explicit opt-in save of ONLY the marked duplicate, after graph readback + compilation."""
    _api()
    material = _duplicate(path)
    _open(material, editor_changes_applied)
    _validate_state(material, _state(material))
    _compile(material)
    unreal.log_warning(RENDERER_WARNING)
    _require(_assets().save_loaded_asset(material, only_if_is_dirty=False), "Saving the duplicate failed.")
    unreal.log(f"Saved duplicate only: {material.get_path_name()}. "
               "Original master, project settings and all material parents are unchanged.")
    return material
