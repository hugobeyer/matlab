"""
Builds the brick/stone edge-profile (bevel/chamfer/slope) training-data
network described in Mixtormat_ONNX_Generator_Integration_Audit.md.

Creates:
    /obj/geo_neural_trainer                (geo container, "in a geo node")
        cop_bevel_trainer                  (COP Network SOP, holds the chain)
            tile_pattern    (Tile Pattern)       -> id
            id_to_sdf       (ID to SDF)           -> sdf
            rand_bevel      (Random Mono, seed A)
            stats_bevel     (Statistics by ID)    -> perBrickBevelWidth
            rand_profile    (Random Mono, seed B)
            stats_profile   (Statistics by ID)    -> perBrickProfileMix
            combine_*       (best-effort custom-math node, see NOTE below)

NOTE on the combine step:
Houdini's docs describe a "custom COP with VOPs" workflow but the exact
Copernicus-context node TYPE NAME for it isn't nailed down from docs alone
(legacy COP2 used "vopcop2filter"/"vopcop2gen", which may not exist under
the new /cop network category). This script tries a short list of
candidate type names and tells you which one worked. If none work, it
still builds everything up to that point and prints instructions for
finishing the combine step by hand from the Tab menu (type "vop" or
"function" and see what Houdini offers you at that point).

Run from Houdini's Python Source Editor / Python Shell.
"""

import hou


CANDIDATE_COMBINE_TYPES = [
    "vopcop2filter",
    "vopcop2gen",
    "vop",
    "copvop",
]


def find_node_types(category_name, keyword):
    """Utility: print real node type names in a category matching a keyword.
    Run this first if anything below fails, to self-correct the guesses."""
    category = hou.nodeTypeCategories()[category_name]
    matches = [t for t in category.nodeTypes().keys() if keyword.lower() in t.lower()]
    print("Matches for '{}' in {}: {}".format(keyword, category_name, matches))
    return matches


def safe_set_parm(node, parm_name, value):
    try:
        node.parm(parm_name).set(value)
    except (AttributeError, TypeError):
        print("  [warn] {}: could not set parm '{}' = {} (parm name may differ in your build)"
              .format(node.path(), parm_name, value))


def safe_create(parent, node_type, node_name):
    try:
        n = parent.createNode(node_type, node_name)
        print("  created {} ({})".format(n.path(), node_type))
        return n
    except hou.OperationFailed as e:
        print("  [FAIL] could not create type '{}': {}".format(node_type, e))
        return None


def build():
    obj = hou.node("/obj")
    geo = obj.createNode("geo", "geo_neural_trainer")
    print("Created container: {}".format(geo.path()))

    cop_net = safe_create(geo, "copnet", "cop_bevel_trainer")
    if cop_net is None:
        print("Could not create COP Network SOP ('copnet'). Run:")
        print("  find_node_types('Sop', 'cop')")
        print("to see what's actually available in your build, then rerun with the right type.")
        return

    # --- Stage 1: brick/stone layout + per-brick ID -----------------------
    tile = safe_create(cop_net, "tilepattern", "tile_pattern")
    if tile:
        safe_set_parm(tile, "seed", 1)
        # Pattern type / bond style parm name unverified - check in the UI:
        # likely something like "patterntype" or "pattern"
        safe_set_parm(tile, "patterntype", 2)  # guess: stretcher/running bond

    # --- Stage 2: SDF from ID ----------------------------------------------
    id_to_sdf = safe_create(cop_net, "idtosdf", "id_to_sdf")
    if id_to_sdf and tile:
        id_to_sdf.setInput(0, tile)

    # --- Stage 3: per-brick random scalars ---------------------------------
    rand_bevel = safe_create(cop_net, "randommono", "rand_bevel")
    if rand_bevel:
        safe_set_parm(rand_bevel, "seed", 101)

    stats_bevel = safe_create(cop_net, "statisticsbyid", "stats_bevel")
    if stats_bevel:
        if rand_bevel:
            stats_bevel.setInput(0, rand_bevel)
        if tile:
            stats_bevel.setInput(1, tile)  # id layer, second input (verify in UI)
        safe_set_parm(stats_bevel, "statistic", 0)  # guess: 0 = average

    rand_profile = safe_create(cop_net, "randommono", "rand_profile")
    if rand_profile:
        safe_set_parm(rand_profile, "seed", 202)

    stats_profile = safe_create(cop_net, "statisticsbyid", "stats_profile")
    if stats_profile:
        if rand_profile:
            stats_profile.setInput(0, rand_profile)
        if tile:
            stats_profile.setInput(1, tile)
        safe_set_parm(stats_profile, "statistic", 0)

    # --- Stage 4: combine sdf + perBrickBevelWidth + perBrickProfileMix ---
    combine = None
    for candidate in CANDIDATE_COMBINE_TYPES:
        combine = safe_create(cop_net, candidate, "combine_height_delta")
        if combine is not None:
            break

    if combine is not None:
        for i, src in enumerate([id_to_sdf, stats_bevel, stats_profile]):
            if src:
                try:
                    combine.setInput(i, src)
                except hou.OperationFailed:
                    print("  [warn] could not wire input {} into combine node".format(i))
        print("Combine node created as type '{}'. Open it and build the VOP/VEX "
              "network by hand:".format(combine.type().name()))
        print("  t = clamp(sdf / bevelWidth, 0, 1)")
        print("  chamfer = -depth * (1 - t)")
        print("  bevel   = -depth * sqrt(max(0, 1 - t*t))")
        print("  heightDelta = lerp(chamfer, bevel, profileMix)")
    else:
        print("[FAIL] None of {} exist as node types in your build.".format(CANDIDATE_COMBINE_TYPES))
        print("Run this to find the right one:")
        print("  find_node_types('Cop', 'vop')")
        print("Then wire sdf / stats_bevel / stats_profile into it manually and")
        print("build the height-delta formula above inside it.")

    cop_net.layoutChildren()
    geo.layoutChildren()
    print("Done. Network root: {}".format(cop_net.path()))


if __name__ == "__main__":
    build()
