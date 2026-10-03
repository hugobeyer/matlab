I’ll fetch primary sources and focus on finite fracture tips and connectivity-based piece IDs.


### Five verified primary sources

1. **[Grose et al. (2021) — LoopStructural 1.0](https://gmd.copernicus.org/articles/14/3915/2021/)**  
   §§2.1–2.2 describe 3D scalar fields, stratigraphic isosurfaces, structural coordinates, and finite-extent faults. **Useful:** separate stratigraphy from fault geometry and displacement. **Caveat:** a potential field is not automatically a metric signed-distance field; interpolation and regularisation affect thickness and geometry.

2. **[Mallet (2004) — Space–Time Mathematical Framework for Sedimentary Geology](https://doi.org/10.1023/B:MATG.0000016228.75495.7c)**  
   Defines a transformed space with horizontal horizons and faults removed, intended for modelling depositional properties. **Useful:** stratigraphic/material-space coordinates rather than world-space textures. **Caveat:** only the publisher’s abstract was accessible; it describes the space as approximate—not a verified physical restoration.

3. **[Los Alamos — dfnWorks](https://dfnworks.lanl.gov/)**  
   Explicitly represents individual fractures as **intersecting planar polygons in 3D**, with meshing, flow, and transport. **Useful:** finite fracture footprints instead of unrestricted plane cuts. **Caveat:** fracture-network connectivity for flow is not the same as connectivity of the remaining rock; this page does not provide a rock-piece labelling algorithm.

4. **[CGAL — 3D Triangulations, “Regular Triangulation”](https://doc.cgal.org/latest/Triangulation_3/index.html)**  
   Documents weighted geometry using squared Euclidean distance minus weights; zero weights recover Delaunay. **Useful:** the mathematical foundation behind power-diagram partitioning. **Caveat:** scalar weights do **not** introduce directional anisotropy; some weighted sites can be hidden.

5. **[Apache Commons Geometry — BSP Tree Tutorial](https://commons.apache.org/proper/commons-geometry/tutorials/bsp-tree.html)**  
   Explicitly states that a cut partitions a node’s **entire space**, even when inserted from a finite segment. **Useful:** explains why naïve BSP fracture insertion overcuts. **Caveat:** BSP cuts need not be physical boundaries; the tutorial also demonstrates structural cuts that preserve the represented region.

### Modelling conclusions — synthesis, not quotations

- **Voronoi/power diagrams partition space; finite fracture patches need not partition rock.**
- A shared positive-definite metric gives anisotropy via a linear coordinate transform. Bisectors remain planar; varying metrics need not preserve that property. This is a mathematical deduction, not a CGAL feature claim.
- **An internal disk-shaped crack leaves rock connected around its rim.** A boundary-spanning cut or a separating assembly of fractures can disconnect it.
- Therefore, `piece_id` should label **connected rock volumes**, not plane-sign combinations, fracture IDs, or raw BSP leaves.
- On a fracture-conforming volumetric mesh, block adjacency across actual fracture faces, then compute connected components through intact rock. Resolve tips and narrow bridges; coarse discretisation can create false splits or connections.
- Keep **material ID**, **fracture ID**, and **connected-piece ID** separate. Connectivity alone also does not establish mechanical detachability.

Only web fetches were used; no file edits, terminal, or git.