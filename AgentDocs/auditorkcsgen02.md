I’ll fetch four primary sources and compare feature preservation and GPU/OpenCL trade-offs.


| Method / primary source | Sharp-feature caveat | GPU complexity for OpenCL¹ |
|---|---|---|
| **Marching Cubes** — [Geiss, GPU Gems 3](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu) | No explicit crease/corner fitting; classic cases have topology ambiguities. | **Lowest baseline complexity:** classify/count → scan → emit. Source uses DX10, not OpenCL. |
| **Surface Nets** — [Gibson, MERL TR99-24](https://www.merl.com/publications/TR99-24) | Constrained relaxation retains sampled detail, not necessarily exact creases. Simple averaging variants round corners. | Simple variants are GPU-friendly; Gibson’s original binary-volume method needs iterative neighbor passes. |
| **Dual Contouring** — [Ju et al., author’s research page](https://www.cs.rice.edu/~jwarren/research/index.html) | Fits sharp features using Hermite intersections/normals and QEFs; not a blanket manifoldness guarantee. | **Higher complexity:** robust QEF solves and degeneracies; adaptive octrees add substantial difficulty. |
| **GPU allocation/compaction** — [Harris et al., Parallel Prefix Sum](https://developer.nvidia.com/gpugems/gpugems3/part-vi-gpu-computing/chapter-39-parallel-prefix-sum-scan-cuda) | Infrastructure, not a feature-preserving mesher. | Scan supports active-cell compaction and output offsets; CUDA-specific hardware assumptions need adaptation. |

- **Recommendation¹:** start with MC; choose DC when sharp contacts justify reliable normals and QEFs.
- **Geology:** unsampled thin beds remain lost; faults and material junctions need explicit handling.
- **Evidence:** GPU chapters fetched in full; Surface Nets abstract and DC author overview reviewed.
- ¹OpenCL complexity/recommendations are engineering assessments, not benchmark results.