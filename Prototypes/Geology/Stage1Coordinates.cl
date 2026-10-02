#include "StratigraphicCoordinates.clh"

// Dispatch a 3D NDRange. float4 outputs use 16 bytes/voxel; .w is reserved.
__kernel void geology_stage1(
    const uint4 resolution,
    const float4 domain_min,
    const float4 voxel_size,
    const float4 shear_fold,
    const float4 bedding,
    const float4 thickness,
    const uint seed,
    __global float4 *geologyUVW,
    __global float4 *beddingNormal,
    __global float4 *beddingTangent,
    __global float4 *coordinateDiagnostics)
{
    uint x = (uint)get_global_id(0);
    uint y = (uint)get_global_id(1);
    uint z = (uint)get_global_id(2);
    if (x >= resolution.x || y >= resolution.y || z >= resolution.z)
        return;
    uint3 cell = (uint3)(x, y, z);
    size_t index = geology_index(cell, resolution);

    if (!geology_parameters_valid(shear_fold, bedding, thickness) ||
        !all(isfinite(domain_min.xyz)) || !all(isfinite(voxel_size.xyz)) ||
        !all(voxel_size.xyz > (float3)(0.0f)))
    {
        geologyUVW[index] = (float4)(NAN);
        beddingNormal[index] = (float4)(NAN);
        beddingTangent[index] = (float4)(NAN);
        coordinateDiagnostics[index] = (float4)(NAN);
        return;
    }

    float3 p = geology_position(cell, domain_min, voxel_size);
    GeologyCoordinates q = geology_coordinates(p, shear_fold, bedding, seed);
    // Avoid undefined float-to-int conversion for unusable material domains.
    if (!all(isfinite(q.uvh)) || fabs(q.uvh.z / thickness.x) > 1000000.0f)
    {
        geologyUVW[index] = (float4)(NAN);
        beddingNormal[index] = (float4)(NAN);
        beddingTangent[index] = (float4)(NAN);
        coordinateDiagnostics[index] = (float4)(NAN);
        return;
    }
    GeologyStratigraphy bed = geology_stratigraphy(q, thickness, seed);
    geologyUVW[index] = (float4)(q.uvh.x, q.uvh.y, bed.w, 0.0f);
    beddingNormal[index] = (float4)(bed.normal, 0.0f);
    beddingTangent[index] = (float4)(bed.tangent, 0.0f);
    // H, first-order normal thickness, det(dUVW/dXYZ), orthogonality residual.
    coordinateDiagnostics[index] = (float4)(q.uvh.z, bed.local_thickness,
        bed.jacobian, fabs(dot(bed.normal, bed.tangent)));
}
