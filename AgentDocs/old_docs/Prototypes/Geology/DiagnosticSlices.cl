#include "GeologyMath.clh"

// These kernels ONLY view existing 3D buffers. They never construct geology.
// axis 0: YZ image, width=Nz, height=Ny
// axis 1: XZ image, width=Nx, height=Nz
// axis 2: XY image, width=Nx, height=Ny
inline uint3 geology_slice_cell(uint column, uint row, uint axis, uint slice)
{
    if (axis == 0u)
        return (uint3)(slice, row, column);
    if (axis == 1u)
        return (uint3)(column, slice, row);
    return (uint3)(column, row, slice);
}

inline uint2 geology_slice_size(uint4 resolution, uint axis)
{
    if (axis == 0u)
        return (uint2)(resolution.z, resolution.y);
    if (axis == 1u)
        return (uint2)(resolution.x, resolution.z);
    return (uint2)(resolution.x, resolution.y);
}

inline float4 geology_scalar_color(float value, float2 range)
{
    if (!isfinite(value) || !all(isfinite(range)) || range.y <= range.x)
        return (float4)(1.0f, 0.0f, 1.0f, 1.0f);
    float t = clamp((value - range.x) / (range.y - range.x), 0.0f, 1.0f);
    float3 blue = (float3)(0.10f, 0.27f, 0.75f);
    float3 white = (float3)(0.92f, 0.94f, 0.96f);
    float3 red = (float3)(0.85f, 0.20f, 0.10f);
    float3 rgb = t < 0.5f ? mix(blue, white, 2.0f * t) :
        mix(white, red, 2.0f * t - 1.0f);
    return (float4)(rgb, 1.0f);
}

// component 0/1/2 of geologyUVW gives U/V/W. Also accepts frame/diagnostic buffers.
__kernel void geology_slice_vector(
    const uint4 resolution, const uint axis, const uint slice,
    const uint component, const float2 range,
    __global const float4 *volume, __global float4 *rgba)
{
    uint column = (uint)get_global_id(0);
    uint row = (uint)get_global_id(1);
    uint2 size = geology_slice_size(resolution, axis);
    if (column >= size.x || row >= size.y)
        return;
    size_t pixel = (size_t)column + (size_t)size.x * row;
    uint3 cell = geology_slice_cell(column, row, axis, slice);
    if (axis > 2u || component > 3u || any(cell >= resolution.xyz))
    {
        rgba[pixel] = (float4)(1.0f, 0.0f, 1.0f, 1.0f);
        return;
    }
    float4 value = volume[geology_index(cell, resolution)];
    rgba[pixel] = geology_scalar_color(value[component], range);
}

// Suitable for density, phase, boundaryDistance and localThickness.
__kernel void geology_slice_scalar(
    const uint4 resolution, const uint axis, const uint slice,
    const float2 range, __global const float *volume, __global float4 *rgba)
{
    uint column = (uint)get_global_id(0);
    uint row = (uint)get_global_id(1);
    uint2 size = geology_slice_size(resolution, axis);
    if (column >= size.x || row >= size.y)
        return;
    size_t pixel = (size_t)column + (size_t)size.x * row;
    uint3 cell = geology_slice_cell(column, row, axis, slice);
    if (axis > 2u || any(cell >= resolution.xyz))
    {
        rgba[pixel] = (float4)(1.0f, 0.0f, 1.0f, 1.0f);
        return;
    }
    rgba[pixel] = geology_scalar_color(volume[geology_index(cell, resolution)], range);
}

// Integer nearest-voxel sampling: IDs must never be linearly interpolated.
__kernel void geology_slice_ids(
    const uint4 resolution, const uint axis, const uint slice,
    const uint seed, const uint mask_air,
    __global const int *ids, __global const float *density,
    __global float4 *rgba)
{
    uint column = (uint)get_global_id(0);
    uint row = (uint)get_global_id(1);
    uint2 size = geology_slice_size(resolution, axis);
    if (column >= size.x || row >= size.y)
        return;
    size_t pixel = (size_t)column + (size_t)size.x * row;
    uint3 cell = geology_slice_cell(column, row, axis, slice);
    if (axis > 2u || any(cell >= resolution.xyz))
    {
        rgba[pixel] = (float4)(1.0f, 0.0f, 1.0f, 1.0f);
        return;
    }
    size_t index = geology_index(cell, resolution);
    int id = ids[index];
    if (id == GEOLOGY_INVALID_ID || !isfinite(density[index]))
        rgba[pixel] = (float4)(1.0f, 0.0f, 1.0f, 1.0f);
    else if (mask_air && density[index] >= 0.0f)
        rgba[pixel] = (float4)(0.04f, 0.04f, 0.05f, 1.0f);
    else
    {
        float3 rgb = (float3)(geology_random(id, seed, 4001u),
            geology_random(id, seed, 4003u), geology_random(id, seed, 4007u));
        rgba[pixel] = (float4)(0.2f + 0.7f * rgb, 1.0f);
    }
}
