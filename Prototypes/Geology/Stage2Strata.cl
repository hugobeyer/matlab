#include "StrataField.clh"
#include "VolumeCombine.clh"

// Independent of Stage 1 buffers: both stages use the SAME coordinate helpers.
// Use identical domain/material parameters when comparing their outputs.
__kernel void geology_stage2(
    const uint4 resolution,
    const float4 domain_min,
    const float4 voxel_size,
    const float4 shear_fold,
    const float4 bedding,
    const float4 thickness,
    const uint seed,
    const float gap_fraction,
    const float4 block,
    const float4 cutaway,
    __global float *density,
    __global int *layerId,
    __global float *layerPhase,
    __global float *boundaryDistance,
    __global float *localThickness)
{
    uint x = (uint)get_global_id(0);
    uint y = (uint)get_global_id(1);
    uint z = (uint)get_global_id(2);
    if (x >= resolution.x || y >= resolution.y || z >= resolution.z)
        return;
    uint3 cell = (uint3)(x, y, z);
    size_t index = geology_index(cell, resolution);

    int valid = geology_parameters_valid(shear_fold, bedding, thickness) &&
        all(isfinite(domain_min.xyz)) && all(isfinite(voxel_size.xyz)) &&
        all(voxel_size.xyz > (float3)(0.0f)) &&
        all(isfinite(block.xyz)) && all(block.xyz > (float3)(0.0f)) &&
        all(isfinite(cutaway)) && isfinite(gap_fraction) &&
        gap_fraction >= 0.0f &&
        gap_fraction < 1.0f - 2.0f * (thickness.y + thickness.z);
    if (!valid)
    {
        density[index] = NAN;
        layerId[index] = GEOLOGY_INVALID_ID;
        layerPhase[index] = NAN;
        boundaryDistance[index] = NAN;
        localThickness[index] = NAN;
        return;
    }

    float3 p = geology_position(cell, domain_min, voxel_size);
    GeologyCoordinates q = geology_coordinates(p, shear_fold, bedding, seed);
    if (!all(isfinite(q.uvh)) || fabs(q.uvh.z / thickness.x) > 1000000.0f)
    {
        density[index] = NAN;
        layerId[index] = GEOLOGY_INVALID_ID;
        layerPhase[index] = NAN;
        boundaryDistance[index] = NAN;
        localThickness[index] = NAN;
        return;
    }
    GeologyStratigraphy bed = geology_stratigraphy(q, thickness, seed);
    StrataField slab = strata_field(q, bed, thickness.x, gap_fraction);
    density[index] = geology_combine_block(slab.field, p, block, cutaway);
    // Material identity extends through air for inspection; density gates rock.
    layerId[index] = bed.layer_id;
    layerPhase[index] = bed.phase;
    boundaryDistance[index] = slab.boundary_distance;
    localThickness[index] = slab.local_thickness;
}
