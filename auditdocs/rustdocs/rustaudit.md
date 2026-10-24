# Recommendation

**Build Ferrous Corrosion as a modifier of the accumulated material beneath it, using separate but correlated fields for metal loss, retained rust, and visible surface composition.**

For the first production version:

- Use **deterministic clustered features and analytic growth fields**.
- Read the incoming composite height and material before evaluating corrosion.
- Apply **signed, physically scaled height changes** while retaining the underlying shape.
- Drive color, roughness, metallic response, and relief from shared corrosion fields.
- Keep **coverage, metal loss, buildup, and flaking independently controllable**.
- Add iterative simulation later for **runoff transport and propagation around coating barriers**.

The strongest evidence for this separation is that **rust thickness can decrease through shedding while pits continue deepening**. A single “Age” value cannot reliably determine both. [mdpi.com](https://www.mdpi.com/1996-1944/19/6/1189?utm_source=chatgpt.com)

This should be a physically informed authoring model. Predictive corrosion rates would additionally require alloy, environment, coating, and exposure history that a material stack does not contain.

---

## 1. What the research means for the design

### Material identity matters

| Surface process | Relevant distinction | Mixtormat treatment |
|---|---|---|
| **Iron and steel rust** | Iron oxides and oxyhydroxides form while metal is consumed; products may remain, migrate, or detach. | Initial supported corrosion family. |
| **Copper patina** | Different compounds, optical properties, and protective behavior. Copper patinas can have several colors and structures. | Separate material mechanism. |
| **Aluminum oxidation** | A thin protective oxide and localized breakdown differ substantially from ordinary ferrous rust; active corrosion can produce white powdery products. | Separate mechanism. |
| **Mill scale** | Oxide formed during manufacturing is distinguishable from subsequent corrosion products, even when some mineral constituents overlap. | Existing substrate/coating state that corrosion can act upon. |
| **Deposited dirt or imported rust** | Material arrives from elsewhere; its presence does not establish local substrate loss. | Deposition/staining path. |

The Canadian Conservation Institute distinguishes copper, aluminum, and ferrous corrosion in its [metal identification guidance](https://www.canada.ca/en/conservation-institute/services/preventive-conservation/guidelines-collections/metal-objects.html), under **“Recognizing types of corrosion on common metals.”** Microscopy explicitly separating mill scale from newly formed rust appears in [Jiang et al., §3.1 and Figure 2](https://www.mdpi.com/1996-1944/16/13/4521). [Canada.ca](https://www.canada.ca/en/conservation-institute/services/preventive-conservation/guidelines-collections/metal-objects.html?utm_source=chatgpt.com)

**Metallic cannot identify the substrate.** Steel, copper, and aluminum can all have Metallic = 1; paint over steel normally does not. Without substrate metadata, the module should declare a **ferrous-substrate assumption for its selected scope**.

### Initiation and growth

- **Moisture:** supports the electrolyte needed for ordinary atmospheric corrosion. Retained water and repeated wetting matter; current surface wetness and accumulated corrosion are separate states.
- **Salts:** can retain moisture and promote continued attack. Chloride contamination can support activity at substantially lower humidity than a clean indoor surface.
- **Coating damage and scratches:** exposed metal can become an initiation site. A groove in height is only a candidate scratch; it does not prove a protective coating was breached.
- **Oxygen:** is not a simple “more exposure = more corrosion” multiplier. Differential aeration can support iron dissolution in an oxygen-poor region while the oxygen-rich region supports the cathodic reaction.
- **Drainage and exposure:** water can remain in cavities, carry products downstream, or wash material away. Those outcomes require environmental and orientation information beyond local texture height.

These mechanisms are described in [CCI Note 9/6, Introduction, Examination, and Coatings](https://www.canada.ca/en/conservation-institute/services/conservation-preservation-publications/canadian-conservation-institute-notes/care-iron.html), and the differential-aeration explanation in [Ricard et al., Introduction](https://www.nature.com/articles/s41598-023-45547-9). The latter’s nanofilm experiments support the mechanism, not a macroscopic steel growth rate. [Canada.ca](https://www.canada.ca/en/conservation-institute/services/conservation-preservation-publications/canadian-conservation-institute-notes/care-iron.html?utm_source=chatgpt.com)

### Appearance is not a universal progression

Support several related outcomes:

- Thin discoloration with little resolvable relief.
- Granular deposits over shallow or localized attack.
- Coalescing pits and connected eroded valleys.
- Compact crust with cracks.
- Layered products, partial shedding, and exposed corroded substrate.

Derusted steel scans show **sparse pits → overlapping pits → connected valleys**, supporting merged erosion shapes rather than a collection of isolated circular craters. See [To et al., §3.1, Figures 4–5](https://www.jstage.jst.go.jp/article/matertrans/59/8/59_MF201702/_html/-char/en). [jstage.jst.go.jp](https://www.jstage.jst.go.jp/article/matertrans/59/8/59_MF201702/_html/-char/en?utm_source=chatgpt.com)

Color and reflectance also resist a universal age mapping. [Gu et al., §4 and §5.4/Figure 9](https://cave.cs.columbia.edu/Statics/publications/pdfs/Gu_TOG06.pdf) measured chemically accelerated rusting steel with changing color and diminished specularity; the color trajectory was nonmonotonic. Their fitted Torrance–Sparrow width is **not an Unreal roughness value**, and their experimental minutes cannot be converted to outdoor years. [cave.cs.columbia.edu](https://cave.cs.columbia.edu/Statics/publications/pdfs/Gu_TOG06.pdf?utm_source=chatgpt.com)

Even pure hematite powders exhibit different colors with different microstructures. [Gerardin et al.’s open paper](https://www.aic-publishing.org/ojs/index.php/JAIC/article/view/238/227) is useful evidence against assigning every rust mineral one fixed RGB. Its powder measurements are not a ready-made rough-surface BRDF. [Journal of the International Colour Association](https://www.aic-publishing.org/ojs/index.php/JAIC/article/view/238?utm_source=chatgpt.com)

---

## 2. Numeric evidence suitable for calibration

**Classification:** **M** = measured in the stated experiment; **D** = derived/tabulated physical quantity; **G** = conservation guidance. These are reference cases, not global parameter limits.

| Property | Reported values | Conditions, method, and uncertainty | Free full text / relevant location |
|---|---|---|---|
| **Humidity associated with iron corrosion — G** | General indoor guidance: approximately **65% RH**; salt-contaminated iron can remain active around **20%**, sometimes **12% RH**. | Depends strongly on contamination and condition. These are not universal onset thresholds or a measured shader transfer function. | [CCI: care of iron](https://www.canada.ca/en/conservation-institute/services/conservation-preservation-publications/canadian-conservation-institute-notes/care-iron.html), **Storage**; [metal objects](https://www.canada.ca/en/conservation-institute/services/preventive-conservation/guidelines-collections/metal-objects.html), **Combined effects of pollutants and RH**. [Canada.ca](https://www.canada.ca/en/conservation-institute/services/conservation-preservation-publications/canadian-conservation-institute-notes/care-iron.html?utm_source=chatgpt.com) |
| **Rust-layer thickness, inland exposure — M** | **7.0–12.1 μm** at 15 days; **11.2–26.0 μm** at 30 days; **11.6–14.9 μm** at 90 days; **28.5–29.4 μm** at 365 days. | Q235 steel, 800-grit preparation, Chengdu, 45° south-facing exposure; mean **16.6°C**, **81% RH**, chloride deposition **0.37 mg/m²/day**. Cross-sectional SEM. Local observed ranges, not confidence intervals or pit depths. | [Wang et al.](https://pmc.ncbi.nlm.nih.gov/articles/PMC7061111/), **Table 2; §3.3, Figure 3**. [PMC](https://pmc.ncbi.nlm.nih.gov/articles/PMC7061111/?utm_source=chatgpt.com) |
| **Equivalent metal loss — M** | **17.42 μm/year**, reported after 365 days. | Same Chengdu exposure. Weight loss expressed as equivalent thickness loss: an area-averaged quantity, not local recession beneath each rust patch. | [Wang et al.](https://pmc.ncbi.nlm.nih.gov/articles/PMC7061111/), **§3.1**. [PMC](https://pmc.ncbi.nlm.nih.gov/articles/PMC7061111/?utm_source=chatgpt.com) |
| **Selected deepest pits, marine exposure — M** | At **6 / 12 / 24 months**: means **85.2 / 141.6 / 170.2 μm**; reported SD **5.0 / 15.8 / 38.9 μm**; maxima approximately **90 / 159 / 213 μm**. | Q235 steel, East China Sea platform; mean **20.5°C**, **77.7% RH**, chloride deposition **81 mg/m²/day**. Laser confocal microscopy after rust removal. **Five deepest pits per test surface, three specimens**: these are not averages of all pits. SD is not instrument uncertainty. | [Chen et al.](https://www.mdpi.com/1996-1944/19/6/1189), **Table 1; §3.2, Figure 5 and caption**. [PMC alternative](https://pmc.ncbi.nlm.nih.gov/articles/PMC13027690/). [mdpi.com](https://www.mdpi.com/1996-1944/19/6/1189?utm_source=chatgpt.com) |
| **Retained rust thickness in that marine exposure — M** | Approximately **62 → 180 → 85 μm**, at **6 → 12 → 24 months**. | Cross-sectional observations; later thinning associated with cracking and spalling. These values do not establish a universal thickness distribution. | [Chen et al.](https://www.mdpi.com/1996-1944/19/6/1189), **§3.2, Figure 7**.  |
| **Solid product/consumed-iron volume ratio — D** | Examples: magnetite **2.09**; goethite **2.94**; lepidocrocite **3.16**; akaganeite **3.29** in the cited table. | Calculated from phase molar volumes and iron stoichiometry. Excludes deposit porosity, transport, spalling, and flake air gaps. | [Bastidas et al., full PDF](https://revistademetalurgia.revistas.csic.es/index.php/revistademetalurgia/article/download/1480/1829/0), **§3.2, Table 3**. [revistademetalurgia.revistas.csic.es](https://revistademetalurgia.revistas.csic.es/index.php/revistademetalurgia/article/download/1480/1829/0?utm_source=chatgpt.com) |
| **Runoff streak width — M** | Approximately **2.5–3.0 mm**. | SPCC steel at **70°**; **50 ± 5 g/L NaCl**, **10–60 μL/min** droplet supply, **15–20°C**, **40–50% RH**. Width was comparable to the experiment’s droplet diameter. Relative profile heights are not absolute porous-layer thicknesses. | [Ishida et al.](https://www.mdpi.com/2624-5558/7/2/31), **§§2.4, 3.1–3.6**.  |
| **Rust staining of stone — M** | Surface deposits **a few μm** thick; penetration up to approximately **1 mm** into Carrara marble. | Artificial iron staining of marble/travertine; cross-sectional optical microscopy. “Few” is the authors’ qualitative estimate. The millimeter value is internal penetration, **not displacement**. | [Reale et al., full PDF](https://mdpi-res.com/d_attachment/molecules/molecules-28-01582/article_deploy/molecules-28-01582.pdf), **§2.1, Figure 3; §4.1 methods**. [mdpi-res.com](https://mdpi-res.com/d_attachment/molecules/molecules-28-01582/article_deploy/molecules-28-01582.pdf?utm_source=chatgpt.com) |

### What expansion ratios actually mean

The stoichiometrically correct solid-phase ratio is:

\[
\alpha=
\frac{V_m(\text{corrosion product})}
{n_{\mathrm{Fe}}V_m(\mathrm{Fe})}
\]

Here, \(n_{\mathrm{Fe}}\) counts iron atoms in the product formula.

These ratios are **not measured outward displacement multipliers**. Even the phase assumptions matter: a [2017 table](https://revistademetalurgia.revistas.csic.es/index.php/revistademetalurgia/article/download/1425/1669/0) gives chloride-containing akaganeite as **3.59**, while the 2020 table gives **3.29** using different formula/tabulated-volume assumptions. That difference should not be presented as experimental uncertainty. [revistademetalurgia.revistas.csic.es](https://revistademetalurgia.revistas.csic.es/index.php/revistademetalurgia/article/download/1425/1669/0?utm_source=chatgpt.com)

Under an idealized same-area, dense, fully retained deposit:

\[
t_{\mathrm{product}}=\alpha d_{\mathrm{loss}},
\qquad
\Delta H=(\alpha-1)d_{\mathrm{loss}}
\]

Real surfaces depart from that construction through porosity, dissolution, lateral transport, pit filling, and shedding. **Use expansion as a possible calibrated prior; keep loss and retained relief separate. Never multiply the incoming height by it.**

---

## 3. Recommended procedural construction

The following is an **engineering proposal**, rather than a measured corrosion law.

### A. Read an immutable incoming composite

At the module’s evaluation point, capture:

- Composite height and its declared physical scale.
- Base color, roughness, metallic, AO, normals, and surface optics.
- Channel validity and normal/AO provenance.
- Current scope, material coordinates, and optional context.

Re-evaluation must start from that input, not repeatedly corrode the module’s previous cached output.

### B. Build correlated initiation and morphology

1. Create broad susceptibility regions containing smaller initiation sites.
2. Assign persistent IDs and variation to each region and its children.
3. Bias placement weakly using incoming relief; accept optional moisture, damage, or exposure overrides.
4. Expand irregular feature supports to form the corrosion footprint.
5. Generate separate recession and deposition fields from the shared regions.
6. Apply granular texture, crust organization, and flake breakup within those regions.

Use noise for boundary irregularity and texture. **Do not independently randomize pits, orange patches, roughness, and metallic masks.**

Correlated does not mean identical: a deep pit can have little retained product, while a deposit can accumulate away from the source of metal loss.

### C. Compose one signed height change

Define:

- \(H_0\): incoming composite height.
- \(L\ge0\): metal recession.
- \(E_{\mathrm{top}}\ge0\): retained rust’s visible top envelope above the receded substrate.
- \(w\): module/application weight.

Then:

\[
\boxed{H_{\mathrm{out}}=H_0+w(E_{\mathrm{top}}-L)}
\]

The corrosion footprint is already incorporated into \(L\) and \(E_{\mathrm{top}}\); do not multiply it again through another blend stage.

**For lifted flakes, envelope height is not solid rust thickness.** Real lift includes air underneath. A height field stores the visible envelope and cannot represent that void.

### Internal fields and output relationships

| Internal field | Meaning | Outputs it governs |
|---|---|---|
| **Substrate eligibility / protection** | Ferrous eligibility and optional coating integrity. | Permits local metal loss; controls where attack may initiate. |
| **Susceptibility + persistent region IDs** | Shared organization from procedural variation and optional context. | Correlates all corrosion features without making channels identical. |
| **Attack footprint** | Area affected before shedding. | Bounds corrosion-generated recession and deposits. |
| **Metal recession \(L\)** | Removed substrate depth, including localized pits and merged erosion. | Negative height contribution; exposed substrate shape. |
| **Retained product + flake state** | Product retention, chips, plate tiers, and bounded lift. | Positive envelope height, visible rust coverage, local occlusion. |
| **Visible oxide coverage** | Rust actually visible after breakup or shedding. | Base-color replacement, metallic attenuation, roughness and optical mixing. |
| **Unresolved microstructure** | Detail smaller than the represented height bandwidth. | Fitted/artistic roughness; optional independent micro-normal detail. |
| **Transported stain** | Imported corrosion products on a receiving surface. | Color/optics and possibly small deposit relief; no automatic substrate recession. |
| **Final composed height** | The actual surface after application weighting. | Height-derived normals and local height-based occlusion. |

---

## 4. Practical GPU methods

| Method | Best use | Recommendation and limits |
|---|---|---|
| **Hierarchical feature seeds + periodic noise + analytic distances** | Colonies, clustering, pits, granular deposits, irregular boundaries. | **Default.** Fast to edit and easy to keep deterministic. Distance alone does not respect barriers. |
| **Distance transforms / jump flooding** | Borders around arbitrary scratches, masks, or selected regions. | Useful helper. JFA is approximate Euclidean propagation, not a weighted path solver. Its passes depend on grid size. |
| **Weighted front propagation** | Growth around coating barriers or along preferred pathways. | Worthwhile advanced option when those barriers materially change the pattern. |
| **Random walks / cellular growth** | History-dependent spreading. | Optional. Avoid grid-direction artifacts and tying growth distance to texel count. |
| **Reaction–diffusion** | Secondary breakup or deliberately stylized patterns. | Gray–Scott is not a ferrous-corrosion model. Using it here is an artistic approximation. |
| **Water and product transport** | Runoff, merging streaks, wash-off, downstream deposition. | Strongest justification for iterative simulation. Requires direction and source/receiver context. |

Relevant free technical sources:

- [Mérillou et al., **§3.1–3.2**](https://graphicsinterface.org/wp-content/uploads/gi2001-20.pdf): initiation, stochastic spreading, and joint height/appearance changes. Its example pit factor of 10 is explicitly empirical. [graphicsinterface.org](https://graphicsinterface.org/wp-content/uploads/gi2001-20.pdf?utm_source=chatgpt.com)
- [Rong and Tan, **§3; error analysis §§5–6**](https://www.comp.nus.edu.sg/~tants/jfa/i3d06.pdf): GPU jump flooding and approximation limits. [Version with Errata.doc](https://www.comp.nus.edu.sg/~tants/jfa/i3d06.pdf?utm_source=chatgpt.com)
- [Pearson, **Equations 1–2**](https://arxiv.org/pdf/patt-sol/9304003): the dimensionless Gray–Scott reaction system. [arxiv.org](https://arxiv.org/pdf/patt-sol/9304003?utm_source=chatgpt.com)
- [Dorsey et al., **§§2–3**](https://graphics.cs.yale.edu/sites/default/files/dorsey_flow.pdf): a phenomenological flow, absorption, and deposition model. [graphics.cs.yale.edu](https://graphics.cs.yale.edu/sites/default/files/dorsey_flow.pdf?utm_source=chatgpt.com)

### Flakes in a height field

Use irregular, persistent flake regions with:

- Several ordered elevation tiers.
- Finite thickness and broken borders.
- Asymmetric edge lift.
- Projected overlap and chips exposing lower product or substrate.

Avoid lifting every perimeter equally: that produces crater rims rather than plates.

This can represent top surfaces, steps, and raised edges. **True undercuts, detached sheets, underside cavities, and overhang silhouettes require geometry.**

### Tiling and resolution independence

- Store feature positions and sizes in continuous material coordinates.
- Seed by persistent feature IDs, not texel coordinates.
- Make noise, warp vectors, distances, filters, derivatives, and simulation boundaries periodic.
- Use physical filter radii and depth units.
- Resample the same features when export resolution changes.
- Filter unresolved detail rather than enlarging it to remain visible.

[Gustavson and McEwan’s periodic noise paper, §§1–3](https://www.jcgt.org/published/0011/01/02/paper-lowres.pdf), provides an implementation basis with analytic derivatives. Its 2D periods have restrictions—integer X and even-integer Y—which must also be respected by octave construction. Arbitrary lacunarity does not automatically preserve tiling. [jcgt.org](https://www.jcgt.org/published/0011/01/02/paper-lowres.pdf?utm_source=chatgpt.com)

Two production constraints remain:

- A periodic modifier cannot make a nonperiodic incoming composite seamless while preserving it unchanged.
- Physical calibration is essential. A **1 m tile at 4096² has approximately 0.244 mm texels**; a 0.1 mm-wide pit is unresolved regardless of how accurately its depth is specified.

---

## 5. Height inference and channel-contract requirements

### What height can actually provide

| Available from calibrated height | Additional information required |
|---|---|
| Local slope, curvature, recesses, ridges, and feature scale. | World orientation, gravity, rain direction, and drainage boundary conditions. |
| Candidate scratches or narrow grooves. | Whether a coating exists and whether the groove breaches it. |
| Local geometric shelter within the represented patch. | True scene exposure, oxygen access, humidity, salt history, and nearby objects. |
| Existing surface shape. | Alloy identity, hidden substrate, wall thickness, and internal corrosion. |

**Default behavior should remain useful with no references:** assume ferrous material in the selected scope, use a modest geometric bias, and generate isotropic corrosion. Directional runoff should require an explicit direction or a valid mesh-derived frame.

For world gravity, project gravity onto the actual surface and transform it through the surface parameterization. UV “down” has no inherent relationship to gravity.

### Contract changes worth making

**No additional exported texture channel is inherently required for V1.** The important changes are internal semantics and metadata:

1. **Signed height contribution and scale.** Distinguish a height modifier from replacement height. Preserve physical range/offset; do not normalize each result to 0–1.
2. **Material state separate from metallic.** Add optional substrate/protection metadata or a declared preset assumption.
3. **Normal provenance.** Separate height-derived orientation from independent imported detail.
4. **Occlusion provenance.** Separate local height-derived occlusion from unrelated imported AO where possible.
5. **One owner for application weighting.** Module masks and layer opacity must not apply the same contribution twice.

For painted steel, a flattened visible material does not contain the buried steel appearance. Use retained layer provenance automatically where available. A generated steel fallback can support an automatic preset, but it should be identified as an assumption.

### Output rules

- **Base color:** unlit material appearance. No AO, cast shadows, curvature shading, or painted highlights.
- **Metallic:** follows the material visibly covering the pixel. Rust-covered regions use nonmetal treatment; exposed steel retains conductor response. Fractional values represent filtered mixtures, not physical “partial metallicity.”
- **Roughness:** represents unresolved reflection spread. Micrometer \(R_a\), pit depth, and Unreal Roughness are different quantities.
- **Surface optics:** preserve incoming properties outside affected coverage. Wet-film response remains independent of corrosion structure.
- **AO:** derive local occlusion from the resulting surface once; do not multiply duplicated cavity occlusion.

These distinctions agree with [Epic’s UE 5.8 Material Inputs documentation, Base Color / Metallic / Roughness](https://dev.epicgames.com/documentation/unreal-engine/material-inputs-in-unreal-engine). It supplies no universal rust RGB or roughness range. [Epic Developer Community](https://dev.epicgames.com/documentation/unreal-engine/material-inputs-in-unreal-engine?utm_source=chatgpt.com)

**Normals must follow the actual composed height once.** Differentiate the masked displacement itself; multiplying a previously computed normal by a mask misses boundary slopes. Combine only independent residual detail afterward. For displaced previews, also avoid adding the same slope again over geometry already carrying it.

[Mikkelsen’s surface-gradient framework, §3 and §3.1](https://jcgt.org/published/0009/03/04/paper-lowres.pdf), supports composing bump influences before resolving the normal. The provenance bookkeeping above is the proposed Mixtormat integration. [jcgt.org](https://jcgt.org/published/0009/03/04/paper-lowres.pdf?utm_source=chatgpt.com)

---

## 6. Artist interface: eight primary controls

**All defaults below are artistic starting values.** They are not measured constants or fitted results. Physical units make them calibratable; a preset becomes **fitted** only after matching a named reference dataset.

| Control | Proposed default / UI range | Coupled behavior | Classification |
|---|---|---|---|
| **Coverage** | **35%**, 0–100% | Changes the affected footprint and associated material transitions. Does not rescale pit depths. Visible surviving rust may decrease after shedding. | Artistic |
| **Structure** | **Granular**; segmented **Smooth / Granular / Layered** | Changes deposit organization, grain/plate distribution, and associated microtexture. Preserves explicit depth limits. | Artistic |
| **Pit Depth** | **0.10 mm**; soft range 0–1 mm | Maximum recession budget; drives erosion height and its derived normal/occlusion. | Artistic, dimensional |
| **Buildup** | **0.10 mm**; soft range 0–2 mm | Maximum retained surface envelope above the corroded substrate, including bounded lift. | Artistic, dimensional |
| **Flaking** | **0**; 0–1 | Breakup, partial shedding, and edge lift of layered deposits. Does not increase metal loss or the buildup cap. | Artistic |
| **Feature Size** | **5 mm**; soft range 0.25–50 mm | Characteristic colony size; coordinated pit, grain, and plate scales follow preset ratios. | Artistic, dimensional |
| **Surface Bias** | **0.25**; 0–1 | Neutral placement through increasing preference for local recesses. Explicitly a geometric heuristic. | Artistic |
| **Variation** | Seed **1**; unsigned integer | Changes shared regional layout and related variation coherently. Preserves metric budgets. | Artistic |

Soft ranges are interface suggestions, not physical limits.

**Thin discoloration** comes from Smooth structure, very low buildup, and appropriate optical coverage. Keeping “thin” out of the structure label avoids contradicting an independently chosen thickness.

Optional controls can expose:

- Pit localization, grain/colony ratio, plate aspect, and retention.
- Palette and fitted optical response.
- Moisture/salt/damage/exposure overrides and present wetness.
- Gravity/flow context and transport.
- Physical tile dimensions and height-unit calibration.

A large buildup with zero loss should be interpreted as deposited material or an artistic override.

### Mechanism-based presets

| Preset | Distinct behavior |
|---|---|
| **Atmospheric wet/dry** | Distributed initiation, coalescing colonies, granular or compact retained products. |
| **Salt-contaminated localized attack** | Stronger localization and uneven recession/retention; does not imply a universal salt-to-depth equation. |
| **Coating breach** | Initiation at generated or inherited defects, with later propagation beneath protection. |
| **Layered accumulation and shedding** | Retained plates, cracks, and exposed previously attacked substrate after shedding. |
| **Runoff stain** | Transported products affecting a receiving surface without automatically removing that surface. |

Runoff deserves a distinct path. CCI documents iron staining on marble and concrete, including an iron anchor staining its base: [Outdoor Objects → Staining and Corrosion, Figures 12–13](https://www.canada.ca/en/conservation-institute/services/preventive-conservation/guidelines-collections/outdoor-objects.html). [Canada.ca](https://www.canada.ca/en/conservation-institute/services/preventive-conservation/guidelines-collections/outdoor-objects.html?utm_source=chatgpt.com)

A single material tile cannot discover a nearby rust source. Cross-object transport needs scene context or an optional supplied transfer field.

---

## 7. Phased implementation

### Phase 1 — Useful production module

- Immutable composite input, signed height delta, physical-scale metadata.
- Clustered analytic initiation, coherent pits/deposits, basic granular and layered structures.
- Eight primary controls and automatic ferrous fallback.
- Optional masks/references; no mandatory wiring.
- Correct normal finalization and channel preservation.
- Deterministic caching keyed by input revisions, scope, seed, scale, and settings.

### Phase 2 — Better morphology and calibration

- Improved overlapping plates, chips, asymmetric lift, and retention.
- Automatic coating/substrate provenance where the stack supports it.
- Reference-calibrated presets using cross-sections, derusted profiles, and optical captures.
- Better filtering of unresolved relief into roughness.
- Mesh-aware gravity and exposure inputs.

### Phase 3 — Simulation where it adds value

- Weighted propagation around protection barriers.
- Water/product transport with deposition and wash-off.
- Optional wet/dry history and retained-product state.
- Simulation spacing and timestep independent of final export resolution.

Keep ordinary clustered rust, pits, and crust available through the analytic path.

---

## 8. Visual acceptance tests

These are proposed implementation gates.

| Test | Required result |
|---|---|
| **Zero-effect identity** | Disabled module, zero Coverage, or globally zero application mask returns the incoming bundle unchanged, including validity/provenance. |
| **Substrate preservation** | Apply to stamped letters, rivets, grooves, and broad dents. Original relief remains recognizable; only local signed corrosion changes it. |
| **Height-offset invariance** | Add a constant to incoming height. Corrosion morphology remains unchanged and the output shifts by the same constant. |
| **Loss versus buildup** | Buildup = 0 produces recession only. Pit Depth = 0 produces no metal recession. Removing a flake does not restore previously lost metal. |
| **Control independence** | Coverage changes do not renormalize remaining pit depths. Structure changes preserve depth budgets. Seed changes preserve scale. |
| **Normals once** | Under moving grazing light, compare against normals reconstructed from final height. No duplicated relief or inverted mask-edge slopes. |
| **Actual relief** | Height-capable preview/export contains pits and buildup in height, rather than only in the normal channel. |
| **Masking** | Hard/soft masks and layer opacity apply once. No distant leakage; any normal/AO influence near boundaries matches the documented filter footprint. |
| **Tiling** | Inspect a 3×3 repeat in every output, including normals, AO, region IDs, and optional transport. |
| **Resolution stability** | At 1K, 2K, and 4K, feature locations and physical dimensions remain stable; only resolvable detail changes. |
| **Material eligibility** | Explicit nonferrous or protected regions reject local ferrous attack. Stain mode can affect a receiver without creating pits. |
| **Lighting independence** | Rotating illumination changes shading, while base color contains no fixed highlights or cavity shadows. |
| **Extreme settings** | Full coverage, zero scale limits, large depth inputs, complete shedding, and missing references produce finite, bounded results and no automatic height normalization. |
| **Stack and persistence** | Re-evaluation, reordering, duplication, copy/paste, and instances preserve seeds, units, scope, and reference behavior. |

**The first release should be judged on composition correctness, coherent morphology, and stable physical scale. Simulation should earn its place by producing better transport or barrier-dependent growth.**

## Source follow-up — localized pits, oxidation spots and peeling (2026-10-24)

Implemented only in `Shaders/Private/MixtormatRust.usf`; runtime appearance and shader
compilation remain unverified. The sections above remain a design proposal, not a record
of shipped physical calibration, substrate inference or automatic ferrous fallback.
Current Rust still uses relative units and the existing authored palette/controls.

- Removed the universal `0.14 * PitDepth` recession floor and broad valley component.
  Pit loss now follows bounded cell-distance mouths with varied radii, steep finite walls,
  irregular floors and subordinate micro-pits. Shape support is separate from coverage.
- Product rims now follow the actual pit-mouth distance, broken by correlated variation;
  the old pigment contour of `ColonyCore` is no longer used as a geometric rim proxy.
- Non-layered deposits are reduced inside pit mouths, with a bounded product contribution
  around their edges. This changes existing Rust appearance; it is not a preset-only fix.
- Pigment now blends dark-body, red-brown and sparse bloom coordinates in the existing
  color ramp using colony-correlated spot masks. Fine grain is subordinate. Variation=0
  selects a uniform body coordinate; custom palettes remain authoritative, so darkening
  depends on their authored colors. No light-dependent highlights or shadows are added.
- Layered scale has a lower retained envelope beneath the upper plates. Upper seams and
  shedding can expose lower oxide or the recessed substrate. Reserved buildup headroom
  permits asymmetric edge lift without flattening every high tier through saturation.
- Both retained layers and pit residue disappear at Flaking=1; metal loss remains.
  Buildup=0 still permits thin oxide, while PitDepth=0 eliminates metal recession.
  No new controls, parameter bindings, shared apply contracts or iterative passes added.

Remaining: independent artist controls for pit/grain/plate scale and oxidation spots,
explicit projected overlaps beyond the two-level envelope, transport and physical
calibration. True detached sheets, underside voids and overhangs still require geometry.
Do not describe this analytic source change as a corrosion simulation or a validated
match to the conceptual image board.

Owner visual checks (not run):

1. Compare board 05 against Buildup=0: localized pit mouths, uncut gaps, no broad depth floor.
2. Compare board 07/08 in Layered mode while sweeping Flaking: lifted upper plates,
   lower-scale exposure and final bare recessed metal; no restoration of consumed metal.
3. Compare base color alone with the old result: dark bodies, red-brown patches and limited
   orange blooms. Check the default and a custom palette, including Variation=0.
4. Check Coverage=0/1, PitDepth=0, Buildup=0 and Flaking=1, plus channel-write toggles and
   masks. Fully covered existing pits must not be renormalized as Coverage increases.
5. Inspect 3×3 tiling, 1K/2K/4K output and small FeatureSize under grazing light for mouth,
   rim and seam aliasing. Check substrate detail, final normals and layer ordering.

No commands, builds, tests, diagnostics, installs or Unreal launches were run.

## Rewrite — Noise-module ferrous corrosion (2026-10-24)

`Shaders/Private/MixtormatRust.usf` rewritten on the shared Noise module
(`MixtormatNoiseFbm/Billow/Ridged`, `MixtormatNoiseV2Worley/Curl`,
`MixtormatNoiseValueD`); custom `RustNoiseD/RustSingleNoise` lattice code removed.
Same shader parameters, outputs, presets and controls; no C++ or runtime change.

- Colonies: two-level curl warp + FBM susceptibility + Worley nuclei,
  freckle satellites and ridged filiform creep at borders.
- Loss: pit mouths coalesce through the F1/F2 saddle and a broad erosion
  valley inside dense colonies; irregular floors, steep finite walls.
- Product: Granular = billow mounds + Worley nodule domes + powder grain
  relief; Layered = flat plates, tier steps, footprint-scaled seams,
  one-sided lift, lower scale bed; Smooth = thin mottled film.
- Pigment: near-black pit cores, dark body, red-brown patches, sparse
  orange bloom on rims/crests, two-octave powder jitter, color-only stain
  halo beyond the relief footprint. Roughness follows pits/bloom/grain.
- Still excluded: coating breach (09), wet crevice (10), runoff transport
  (12), physical units, independent pit/grain/plate scale controls.

Owner visual checks (not run): board 03/06 Granular at 2K grazing light;
05 with Buildup=0 for merged cavities; 07/08 Layered Flaking sweep;
02 Smooth halo without relief; 13–16 FeatureSize×Coverage grid;
3×3 tiling and 1K/2K/4K stability. Shader compilation remains unverified.