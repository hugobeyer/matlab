# Recommended design

**Build one repeatable Wood Finish module with finish-family presets. Internally, separate substrate treatment, deposited material, pore filling, and the outer coating.**

That gives Mixtormat a consistent interface while preserving the differences between oil, dye, pigment, wax, filler, and film finishes.

- **Always evaluate against the incoming scoped wood surface.** Preserve its color, grain placement, height, and material state.
- **Keep substrate relief and coating relief separate internally.** Leveling a clear coating must not blur the wood’s color or overwrite the stored wood surface.
- **Use calibrated thickness.** A finish’s build must be independent of the input height’s minimum and maximum.
- **Ship a useful single-surface approximation first.** Accurate smooth coating over rough wood requires additional material fields and a layered renderer path.

**Evidence labels used below:** **M** = measured; **R** = technical/reference value; **E** = established mechanism; **A** = proposed artistic or engineering approximation; **F** = renderer parameter fitted to measurements. I am not claiming that any proposed preset has already been fitted.

---

## 1. Recommended model for each finish family

The “model” column is my implementation recommendation. The cited references support the underlying behavior, not a ready-made shader.

| Finish family | Supported behavior | Recommended model and limitation |
|---|---|---|
| **Penetrating drying oil** | Wetting, color saturation, uptake, and eventual crosslinking. Excess application can leave material in low areas. | Modify the existing substrate’s optical response and preserve most relief. A wiped-oil preset should have negligible resolved film build, but **oil must not universally mean zero film**. Product/application-specific oil films are measured below. |
| **Dissolved dye** | Transparent coloration rather than a layer of suspended pigment particles. | Apply an absorption-like color transformation to the original wood, modulated by optional absorbency information. Preserve the grain’s positions and geometry. Carrier-induced grain raising is a separate effect. |
| **Pigment stain** | Particles can accumulate in grain and surface retention sites; increased deposition can obscure the substrate. | Track deposited pigment coverage separately from absorption. Bias retention toward supplied pores and geometric recesses. Do not treat pigment concentration as a roughness or height value. |
| **Oil/alkyd varnish** | A reactive film; formulation and application determine tint, leveling, and durability. | Use the shared coating model, with independently specified film tint, build, and sheen. “Varnish” is too broad to prescribe one optical or aging response. |
| **Lacquer** | The name covers different formulations. Nitrocellulose, acrylic, and catalyzed products should not share every assumption. | Share geometry machinery, but use explicitly named formulation presets. Application defects belong to the coating surface. |
| **Polyurethane** | Oil-modified, waterborne, and two-component formulations differ. Resin and hardener chemistry affect yellowing. | Separate clear/stable and amber/yellowing-prone presets. Do not infer gloss, thickness, or yellowing simply from the word “polyurethane.” |
| **Shellac** | An evaporative resin finish available in substantially different initial colors; moisture can produce white haze. | Use a film with grade-dependent tint. Keep initial amber coloration, later discoloration, and moisture haze independent. |
| **Wax** | Thin deposits, retention in recesses, and polishing/buffing affect appearance. Excess can remain as pale residue. | Track **deposit amount and polish state separately**. More wax must not automatically mean more gloss. A thin surface approximation is useful; exact wax scattering needs product-specific characterization. |
| **Pore/grain filler** | Material is packed into exposed pores and excess removed from the surrounding surface. | Add material within selected depression capacity. Preserve the surrounding wood. Clear and colored fillers need different optical treatment. A conformal coating is not equivalent to filling. |

Sources and relevant sections:

- [Canadian Conservation Institute — *Care of Furniture Finishes*](https://www.canada.ca/en/conservation-institute/services/conservation-preservation-publications/canadian-conservation-institute-notes/care-furniture-finishes.html), **Natural resin coatings; Oils**. It also explains why historical finish names do not define clean chemical categories. [Canada.ca](https://www.canada.ca/en/conservation-institute/services/conservation-preservation-publications/canadian-conservation-institute-notes/care-furniture-finishes.html?utm_source=chatgpt.com)
- [Target Coatings — *Dye vs. Pigment Stains*](https://www.targetcoatings.com/2020/11/19/dye-pigment-wood-stains/), main technical explanation. Commercial stains can contain both mechanisms; waterborne/solventborne is a separate distinction. [Target Coatings](https://www.targetcoatings.com/2020/11/19/dye-pigment-wood-stains/?utm_source=chatgpt.com)
- [Eastman — *2K Acrylic Urethane Wood Coatings*](https://www.eastman.com/content/dam/eastman/corporate/en/literature/c/coatind14974.pdf), **p. 2, Application overview**, for resin/hardener distinctions. [eastman.com](https://www.eastman.com/content/dam/eastman/corporate/en/literature/c/coatind14974.pdf?utm_source=chatgpt.com)
- [CAMEO — Shellac](https://cameo.mfa.org/wiki/Shellac), **Description; Risks**. [CAMEO](https://cameo.mfa.org/wiki/Shellac?utm_source=chatgpt.com)
- [NPS Museum Handbook, Appendix N](https://www.nps.gov/subjects/museums/upload/MHI_AppN_WoodObjects.pdf), **printed pp. N:18–N:19**, for wax filling, residue, and buffing. [nps.gov](https://www.nps.gov/subjects/museums/upload/MHI_AppN_WoodObjects.pdf?utm_source=chatgpt.com)
- [American Hardwood Information Center — *The Basics of Hardwood Finishing*](https://www.hardwoodinfo.com/specifying-professionals/basics-hardwood-finishing/), **Colorants; Stains; Wood Fillers**. [American Hardwood Information Center](https://www.hardwoodinfo.com/specifying-professionals/basics-hardwood-finishing/?utm_source=chatgpt.com)

### One module, one finishing step

Use repeatable instances for a sequence such as:

**Existing wood → dye → filler → clear film → wax**

This keeps order and wear understandable. Removing the clear film exposes the dyed/filled surface beneath it. Commercial combination products can be represented by curated recipes or explicit compound presets.

---

## 2. What the existing wood can—and cannot—tell the finish

### Anatomy and preparation matter

**Open grain is a finishing description of exposed pore texture; it is not synonymous with ring-porous anatomy.** Earlywood/latewood arrangement, exposed vessels, cut orientation, and preparation all affect the result.

The USDA handbook documents that:

- Oak and ash are examples requiring pore filling for a smooth finish.
- Small-pored woods behave differently.
- Unequal staining can accentuate grain; in some softwoods it can reverse earlywood/latewood color contrast.
- Sealers reduce subsequent uptake.
- End grain takes up water differently from face grain, but this does not establish a universal oil/stain absorption multiplier.

See [USDA *Wood Handbook*, verified 1999 chapter](https://research.fs.usda.gov/download/treesearch/7145.pdf), **pp. 15–30 to 15–32, Table 15–6; moisture discussion pp. 15–10 to 15–13**. [research.fs.usda.gov](https://research.fs.usda.gov/download/treesearch/7145.pdf?utm_source=chatgpt.com)

Sanding also leaves directional scratches and damaged fiber fragments. Landry’s microscopy found visible sanding arcs and localized fiber lifting after staining; some aggregate roughness measures nevertheless decreased. Therefore, “water raises grain” is not a rule that every roughness statistic must increase. See [Landry et al. 2013, full PDF](https://bioresources.cnr.ncsu.edu/wp-content/uploads/2016/06/BioRes_08_2_1997_b_Landry_BC_Water_Solvent_Stains_Grains_Rising_Birch_2991.pdf), **Figs. 2–6 and Table 3**. [bioresources.cnr.ncsu.edu](https://bioresources.cnr.ncsu.edu/wp-content/uploads/2016/06/BioRes_08_2_1997_b_Landry_BC_Water_Solvent_Stains_Grains_Rising_Birch_2991.pdf?utm_source=chatgpt.com)

### Input inference policy

The following is a recommended implementation policy, not anatomical classification:

| Property | Reliable from ordinary height/color? | Recommended handling |
|---|---|---|
| Surface depressions, ridges, slopes | Yes, within resolution and scale limits | Derive geometric fields. |
| Dominant visible texture direction | Sometimes | Estimate with a structure tensor and retain confidence. |
| Anatomical vessel/pore identity | No | Accept an optional published pore field; otherwise call the result **depression detection**. |
| Earlywood/latewood identity and absorbency | No universal inference | Optional semantic fields or an explicitly artistic response preset. |
| End grain | Not reliably | Optional generator metadata or artist annotation. |
| Three-dimensional fiber inclination | No | Preserve upstream data if available. |
| Object edges, handling zones, gravity | No | Require mesh/context information or an explicit artist direction. |

**A dark region is not automatically porous. A cavity is not automatically a vessel. A tile boundary is not an object edge.**

### Why wetting can darken wood

**E:** Filling air-containing surface structures with a transparent medium reduces refractive-index contrast and changes scattering. Less pale surface scattering can make the substrate’s existing selective absorption more apparent. A surface film can also alter internal reflection and light escape.

**M:** Baar et al. studied **13 wood species** under humidity conditioning, immersion, and vacuum water impregnation. Stronger darkening accompanied free water in the structure; ordinary moisture changes below fiber saturation generally produced much smaller changes. This is evidence for water-related optical behavior, not a calibrated oil-finish transfer function.

See [Baar et al. 2019, full PDF](https://wfs.swst.org/index.php/wfs/article/download/2721/2526/7428), **Introduction; Results and Discussion**. [wfs.swst.org](https://wfs.swst.org/index.php/wfs/article/download/2721/2526/7428?utm_source=chatgpt.com)

Consequences for Mixtormat:

- Keep **wetting enhancement** separate from **added colorant**.
- Do not simulate wetting by painting brown into the grain.
- Do not use AO multiplication as wetting.
- Do not promise uniform contrast enhancement: substrate, finish, and viewing conditions matter.

Measured finished wood also exhibits a colored, directional **subsurface fiber highlight**, distinct from the surface highlight. Marschner’s measurements and model show why ordinary surface anisotropy alone cannot reproduce every figured-wood effect. Height and color cannot recover the missing three-dimensional fiber orientation. See [Marschner et al. 2005, full PDF](https://www.cs.cornell.edu/~srm/publications/SG05-wood-lr.pdf), **§§2–4**. [cs.cornell.edu](https://www.cs.cornell.edu/~srm/publications/SG05-wood-lr.pdf?utm_source=chatgpt.com)

---

## 3. Thickness and optical evidence

### Measured film thicknesses

**These are application-specific examples, not recommended universal ranges.**

#### Dataset A — lacquer and polyurethane on *Melia dubia*

Three brushed coats; wood sanded through 80/100/120 grit; **12 specimens per coating, 60 ultrasonic thickness readings per coating**.

| Tested coating | Observed dry film range | Mean ± SD | Mean 60° gloss |
|---|---:|---:|---:|
| Solvent NC lacquer | 30–50 µm | 38 ± 5.1 µm | 27.6 GU |
| Waterborne NC lacquer | 27–41 µm | 34 ± 3.2 µm | 18.6 GU |
| Solvent polyurethane | 29–51 µm | 35 ± 6.5 µm | 92.4 GU |
| Waterborne polyurethane | 28–37 µm | 32 ± 2.7 µm | 42.6 GU |

**M:** PosiTector 200 ultrasonic thickness measurement; 60° gloss meter. [Shukla et al. 2019, full PDF](https://www.drvnaindustrija.com/site/assets/files/1434/1_drv_ind_vol_70_2_shukla.pdf), **methods pp. 108–109; Tables 2 and 4, pp. 112–113**. These results do not establish that all waterborne finishes are duller or thinner. [drvnaindustrija.com](https://www.drvnaindustrija.com/site/assets/files/1434/1_drv_ind_vol_70_2_shukla.pdf?utm_source=chatgpt.com)

#### Dataset B — shellac on *Dalbergia sissoo*

| Application | Observed film range | Mean ± SD |
|---|---:|---:|
| Two cloth-applied coats | 46–55 µm | 49.9 ± 2.5 µm |
| Four coats | 50–59 µm | 54.5 ± 3.0 µm |
| Six coats | 49–68 µm | 56.6 ± 5.7 µm |

**M:** Shellac formulation **3 g per 50 mL denatured alcohol**; preceding coats allowed to dry; ten specimens per group, three ultrasonic readings per specimen. [Gupta et al. 2016, full PDF](https://www.scielo.cl/pdf/maderas/v18n3/aop4016.pdf), **methods pp. 444–445; Table 1, p. 446**. Coat count clearly does not provide a universal linear thickness scale. [scielo.cl](https://www.scielo.cl/pdf/maderas/v18n3/aop4016.pdf?utm_source=chatgpt.com)

#### Dataset C — oil and wax-containing products on Turkey oak

| Tested system | Reported average film thickness |
|---|---:|
| One linseed-oil coat | 30 ± 10 µm |
| Linseed-oil undercoat plus two hardwax-oil coats | 80 ± 10 µm |
| One commercial “hard wax” coat | 30 ± 10 µm |

**M:** Native and thermally modified *Quercus cerris*; conditioned **14 days at 23 ± 2°C and 50 ± 5% RH**; ultrasonic measurement. The “hard wax” contained **linseed oil, beeswax, carnauba wax, and drier**. These are not measurements of pure buffed wax or every wiped-oil treatment.

[Vidholdová et al. 2021, open article](https://www.mdpi.com/2079-6412/11/2/143), **§§2.1–2.2**; [readable full-text mirror](https://www.academia.edu/110504153/Quality_of_Oil_and_Wax_Based_Surface_Finishes_on_Thermally_Modified_Oak_Wood). The ± values above are reproduced as reported, not relabeled as standard deviations. [Academia](https://www.academia.edu/110504153/Quality_of_Oil_and_Wax_Based_Surface_Finishes_on_Thermally_Modified_Oak_Wood?utm_source=chatgpt.com)

### Wet application is not cured build

Mohawk’s Finisher’s Choice NC lacquer specifies **3–4 wet mils per light application**, equivalent to **76.2–101.6 µm wet**, while limiting the **entire dry system** to **4 mils = 101.6 µm**. Its listed volume solids are approximately **13.83%**.

**R:** Manufacturer specification, not a measured universal lacquer range. See [technical data sheet](https://www.mohawkfinishing.com/mhk_cds/product_pds/612-2580_MHK_TDS.pdf), **p. 1 properties; p. 2 directions**. [Technical Data Sheet](https://www.mohawkfinishing.com/mhk_cds/product_pds/612-2580_MHK_TDS.pdf?utm_source=chatgpt.com)

For an ideal retained coating, wet thickness × volume-solids fraction provides an initial dry-volume estimate. On wood, penetration, wiping, sanding, and curing behavior complicate that estimate. **Mixtormat should expose final build directly**, rather than imply that wet application thickness is final displacement.

### Optical and roughness measurements

| Property | Value | Status and limitation |
|---|---:|---|
| Liquid linseed oil refractive index | \(n_D^{20}=1.4795\) | **R:** Listed for Sigma product 430021 at 20°C, sodium D line. Not a cured-film measurement. [Properties](https://www.sigmaaldrich.com/US/en/product/aldrich/430021). [sigmaaldrich.com](https://www.sigmaaldrich.com/US/en/product/aldrich/430021?utm_source=chatgpt.com) |
| Shellac refractive index | \(n=1.516\) | **R:** CAMEO tabulation; wavelength, temperature, and method are unspecified. [Physical properties](https://cameo.mfa.org/wiki/Shellac). [CAMEO](https://cameo.mfa.org/wiki/Shellac?utm_source=chatgpt.com) |
| Tested alkyd wood lacquer refractive index | \(1.506\pm0.014\) at **1550 nm** | **M:** OCT optical/geometric thickness comparison. Near-infrared, not visible-light calibration. [Dingemans et al. 2017](https://link.springer.com/article/10.1186/s41476-017-0068-2), **Results — calibration procedure**. [Springer Nature Link](https://link.springer.com/article/10.1186/s41476-017-0068-2?utm_source=chatgpt.com) |
| Yellow-birch veneer RMS height \(S_q\) | Untreated **8.8 µm**; water-stained **8.2 µm**; solvent-stained **5.8 µm** | **M:** Confocal profilometry, 20 × 20 mm regions, 5 µm sampling; differing formulations. These are surface-height statistics, **not Unreal roughness**. [Landry, Table 3](https://bioresources.cnr.ncsu.edu/wp-content/uploads/2016/06/BioRes_08_2_1997_b_Landry_BC_Water_Solvent_Stains_Grains_Rising_Birch_2991.pdf). [bioresources.cnr.ncsu.edu](https://bioresources.cnr.ncsu.edu/wp-content/uploads/2016/06/BioRes_08_2_1997_b_Landry_BC_Water_Solvent_Stains_Grains_Rising_Birch_2991.pdf?utm_source=chatgpt.com) |

For a nonabsorbing dielectric interface in air:

\[
F_0=\left(\frac{n-1}{n+1}\right)^2
\]

Thus **\(n=1.5\) gives \(F_0=0.04\)**. This is a derived example, not a measurement of all wood finishes.

**Evidence gaps:** the reviewed sources do not justify universal renderer roughness, visible absorption coefficients, pure-wax thickness, or pore-filler thickness. Gloss units, micrometre roughness, and normalized BRDF roughness are different quantities.

---

## 4. Practical GPU implementation

Everything in this section is a proposed implementation unless explicitly identified otherwise.

### A. Capture the incoming surface once

Retain an immutable scoped input:

- Linear base color.
- Calibrated height and its normal.
- Roughness, AO, metallic, and existing surface optics.
- Channel validity and ownership.
- Optional grain direction, pore, absorbency, end-grain, and existing-coating information.

Analyze this snapshot—not the module’s progressively modified result.

**An earlier coating matters:** a later oil or dye instance must not silently penetrate through a sealed surface. Where coating state is available, restrict penetration to accessible wood. Pigment glazing over a finish is a separate operation.

### B. Establish physical scale

Both are required:

1. **Height scale:** distance represented by a height-unit change.
2. **Lateral scale:** physical width and height of the UV tile.

Use these for derivatives, pore-fill radii, scratch widths, and build.

- \(1\,\mu\text{m}=10^{-6}\,\text{m}=10^{-4}\,\text{cm}\).
- **40 µm = 0.004 cm**.
- Never establish thickness by normalizing the current height extrema.
- Without scale, label controls **relative**, with no micrometre claim.
- Heightfield vertical separation approximates normal coating thickness only at modest slopes; steep geometry requires a surface metric or an acknowledged approximation.

### C. Fill depressions without erasing the substrate

A practical fast method is a bounded upper envelope.

Let \(H\) be the existing height and \(E_r(H)\) a grayscale morphological closing at physical radius \(r\):

\[
D(x)=\max\!\left(E_r(H)(x)-H(x),0\right)
\]

\(D\) measures **geometric depression capacity**.

For a filler:

\[
0\le F(x)\le D(x),\qquad H_b(x)=H(x)+F(x)
\]

- A supplied pore mask can restrict placement.
- Without one, expose **Depression Fill**, not anatomically verified pore filling.
- Limit the radius so broad wood shape survives.
- Excess filler should saturate the available capacity or become explicitly enabled residue.
- Preserve the original wood color; only the filler’s own tint/opacity may obscure it.

A conformal coating instead adds roughly constant thickness and retains the depression. That distinction must be visible in the result.

### D. Level the coating using a build budget

For Film mode, depression buildup should consume the **existing coating budget**, rather than create extra material.

After any separate filler stage, compute a new depression field \(D_b\) from \(H_b\). A useful static approximation is:

\[
t(x)=\max\!\left(0,a+\lambda D_b(x)\right)
\]

Choose \(a\) so that:

\[
\int M(x)t(x)\,dA
=
B\int M(x)\,dA
\]

where:

- \(M\) is scoped coverage.
- \(B\) is mean final build over the treated area.
- \(\lambda\) is an artistic leveling preference.
- \(t\) is thickness **where coating is present**.

Then:

\[
H_{\text{top}}=H_b+t
\]

This provides predictable behavior:

- \(\lambda=0\): conformal build.
- Increasing leveling redistributes material toward depressions.
- Insufficient build can leave peaks uncovered.
- The original substrate is never cut away by leveling.
- A smaller mask does not concentrate the full tile’s original volume into a tiny region.

Use GPU reductions and a scalar solve. Keep coverage separate from thickness; avoid applying the same mask attenuation twice.

### Which leveling method is best?

| Method | Assessment |
|---|---|
| Blur the entire wood height/color | **Reject.** It destroys the substrate and has no coating-volume accounting. |
| Bounded envelope plus build budget | **Recommended MVP.** Fast, controllable, and preserves the substrate; visibly an approximation. |
| Constrained smoothing of the coating surface | Useful quality upgrade. Enforce nonnegative thickness, scope boundaries, and volume. |
| Thin-film flow simulation | Later option when process behavior justifies the cost and additional parameters. |

Physical leveling depends on capillarity, viscosity, evaporation, and substrate topography. [Figliuzzi et al., *Numerical simulation of thin paint film flow*](https://link.springer.com/article/10.1186/2190-5983-2-1), **§2.1 and §4**, provides a validated numerical framework on painted steel. Its parameters are not wood-finish defaults. [Springer Nature Link](https://link.springer.com/article/10.1186/2190-5983-2-1?utm_source=chatgpt.com)

A future solver could evolve coating thickness conservatively from capillary pressure. It would need positivity preservation, stable integration, and explicit penetration/evaporation models. Ordinary Gaussian blur should never be presented as that simulation.

**Edge thinning:** local crests can receive less build in this approximation. Actual object-edge thinning, runs, and gravity-driven buildup require additional geometry/application context.

### E. Separate color mechanisms

Use distinct internal responses:

- **Wetting:** a bounded transformation of the original substrate response. Initially artistic; later fitted from registered untreated/treated samples.
- **Dye:** absorption-like coloration modulated by uptake.
- **Pigment:** deposited coverage and scattering/opacity.
- **Film tint:** transmission through the coating, dependent on thickness.
- **Wax/filler residue:** the deposited material’s own color.

Hauptmann’s oil study found a relationship between appearance and nanoscale surface change, but explicitly could not isolate a universal causal mapping. Its color and AFM measurements also used different specimen types. It therefore cannot support an oil-amount-to-roughness lookup table. See [full article](https://bioresources.cnr.ncsu.edu/resources/the-optical-appearance-of-wood-related-to-nanoscale-surface-roughness/), **Oil Coatings; Results; Conclusions**. [bioresources.cnr.ncsu.edu](https://bioresources.cnr.ncsu.edu/resources/the-optical-appearance-of-wood-related-to-nanoscale-surface-roughness/?utm_source=chatgpt.com)

For a **purely absorbing** film, an artist or measurement can specify transmission \(T_{\rm ref}\) at reference thickness \(d_{\rm ref}\):

\[
\sigma_a=-\frac{\ln T_{\rm ref}}{d_{\rm ref}}
\]

This defines a coefficient from an explicit reference. It does not invent a universal one. Haze introduces scattering and needs more than this absorption-only model.

### F. Application marks and scratches

Use stable material-space seeds and periodic primitives:

- Wrapped directional filtering of periodic noise for brush/application streaks.
- Wrapped line segments or curves for scratches.
- A coating-only height field for orange peel.
- Separate polish variation from deposited thickness.

**Do not simply rotate arbitrary tiled UV noise:** arbitrary rotation can break the original rectangular repeat. Wrapped sampling or toroidal primitives preserve it.

Keep grain-following direction separate from application direction. Without context, label the latter **UV Direction**, not world-space brushing.

Resolved marks affect height/normals; unresolved detail contributes to a fitted or artistic roughness approximation. Do not claim sub-texel geometry that the output cannot represent.

### G. Wear removes material

For film wear depth \(w\):

\[
t_{\rm remaining}=\max(t-w,0)
\]

At breakthrough:

- Remove the coating’s optical coverage.
- Restore the immediate underlying height, color, roughness, and optics.
- Preserve earlier dye, oil, or filler treatments.
- Do not leave a zero-thickness coating with a residual Fresnel lobe.

Scratches before breakthrough affect the coating. Wood abrasion after breakthrough is a separate operation.

Penetrated oil/dye cannot be erased simply by removing a surface film. Their removal needs an explicitly declared treatment-depth approximation or actual substrate abrasion.

---

## 5. Internal fields and material-output mapping

These are conceptual fields; they need not all become persistent full-resolution textures.

| Internal field | Purpose | Output mapping |
|---|---|---|
| **Input surface snapshot** | Preserve immediate underlying material | Identity, exposed regions, wear-through |
| **Absorbency + confidence** | Treatment uptake; semantic or artistic | Wetting/dye response; not AO |
| **Depression capacity / optional pores** | Distinguish geometry from anatomy | Filler and film allocation |
| **Filler amount and material** | Retained deposit | Height, normals, color, roughness, optics |
| **Treated substrate color** | Oil/dye/pigment result | Base color or lower Slab albedo |
| **Coat thickness and coverage** | Physical build and presence | Height, wear, layered transmission |
| **Coat normal and roughness** | Outer interface | Current normal/roughness approximation or independent coating inputs |
| **Coat IOR/F0 and transmission** | Interface reflection and film coloration | Surface optics / coating Slab |
| **Application and wear fields** | Local relief and removal | Coating height, roughness, coverage |
| **Defect states** | Yellowing, haze, cracks, delamination | Separate optical/geometric modifications |

Output rules:

- **Base color:** preserve wood registration; no lighting, cavity shading, or AO multiplication.
- **Height:** publish actual additive build/fill and wear removal, not only normal-map changes.
- **Normals:** derive from calibrated final height; retain the substrate normal for the layered path.
- **Roughness:** one resolved response in MVP; independent substrate/coat responses in the extension.
- **AO:** recompute owned height-derived AO when geometry changes. Arbitrary imported AO cannot reliably be “unbaked.”
- **Metallic:** ordinary wood and these finishes are dielectric. Preserve unrelated scoped material ownership rather than resetting the whole composite.
- **Surface optics:** reuse existing fields where sufficient; extend the contract where independent coating data cannot survive composition and export.

---

## 6. Compact artist interface

**Use eight primary rows with family/preset buttons.** Unavailable rows remain visible and disabled with a short reason.

The numerical UI ranges below are **A: proposed authoring ranges**, not measured material constants.

| Primary control | Proposed default/range | Behavior and availability |
|---|---|---|
| **Finish Type** | Default: **Wiped Oil**; family and preset buttons | Oil, Dye, Pigment, Film, Wax, Filler. Film presets name their formulation. |
| **Amount** | **1**, range **0–1 — A** | Treatment-dose multiplier. Zero is exact identity. For Film it scales the build budget, not an additional opacity fade. Scope coverage stays separate. |
| **Color Response** | Neutral tint; wetting strength **0.35**, range **0–1 — A** for the oil preview | Two independent subvalues: **Wet-out** and **Tint/Colorant**. Do not combine them into one brownness slider. |
| **Film Build** | Film preview **40 µm**, soft range **0–200 µm — A** | Final build over treated area. Disabled for pure Dye/Pigment/Filler and the unbuilt wiped-oil preset. Thin-film oil variants can enable it. |
| **Depression Fill** | Film **0**; Filler **1**; range **0–1 — A** | Film redistributes its existing budget; Filler adds a separate retained deposit. Show “Pore Fill” only when anatomical data exists. |
| **Sheen** | **0.5**, range **0–1 — A** | Controls outer microtexture/polish through a documented preset mapping. Disabled for colorant-only modes. It is not a gloss-unit measurement. |
| **Application Texture** | **0**, range **0–1 — A** | Coating marks/residue variation. Direction and physical width are secondary values. Does not change wood grain frequency. |
| **Wear** | **0**, range **0–1 — A** | Progresses from intact to removable surface finish lost. Disabled for purely penetrating treatment until its removal model is explicitly enabled. |

### Coordination rules

- **Build and sheen remain independent.** A thick matte film and a thin glossy film are both valid authoring targets.
- **Tint and uptake remain independent.** More uptake does not have to mean more brown pigment.
- **Filling and leveling remain distinct.** Film mode spends coating volume; Filler mode supplies another material.
- **Polish and wax amount remain independent.**
- **Wear reveals underlying state**, rather than simply increasing roughness.

Expose secondary overrides inline beneath the relevant control:

- Physical scale and leveling radius.
- UV application angle and persistent seed.
- Absorption/pore bias and inference confidence.
- IOR or reference transmission.
- Individual defect strengths.

Until measured BRDF fitting is performed, sheen-to-roughness curves remain **A**, not **F**. Preserve per-mode settings when switching presets so disabled controls do not silently lose values.

---

## 7. Unreal Engine 5.8 and the material contract

### What current outputs can achieve

With one normal and one roughness, Mixtormat can produce:

- Dye and pigment treatments.
- A useful oil/wetting approximation.
- Wax sheen and residue.
- Selective filling and real height buildup.
- Correct material restoration at wear-through.
- Modest film looks where separate interface responses are not essential.

**The limitation appears when a smooth coating must reflect over rough, visible wood.** Choosing the coating normal suppresses the lower normal response; choosing the wood normal stamps grain into the outer highlight.

### Verified renderer capabilities

| Renderer path | Capability | Contract implication |
|---|---|---|
| **One Substrate Slab, one roughness** | Current simplified surface | Existing output path |
| **One Slab, second roughness lobe** | Two specular widths | Add second roughness/weight; this still shares the Slab’s single normal input |
| **Legacy Clear Coat with dual normals** | Separate coat roughness and underlying normal | Compact coating extension; requires the documented second-normal setup |
| **Two Slabs with Vertical Layer** | Independent interfaces and thickness-dependent coating behavior | Preserve substrate data and publish an explicit coating record |

Epic’s [Slab API, Variables](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UMaterialExpressionSubstrateSlab-) lists one Normal, two roughness inputs, F0/F90, tangent, anisotropy, and chromatic MFP. The shared-normal limitation of its two lobes follows from that interface. [Epic Developer Community](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UMaterialExpressionSubstrateSlab-?utm_source=chatgpt.com)

[Clear Coat dual-normal documentation](https://dev.epicgames.com/documentation/unreal-engine/using-dual-normals-with-clear-coat-in-unreal-engine) specifies the project setting and bottom-normal path. It is a useful fallback, but not a general absorbing/scattering film model. [Epic Developer Community](https://dev.epicgames.com/documentation/unreal-engine/using-dual-normals-with-clear-coat-in-unreal-engine?utm_source=chatgpt.com)

[Vertical Layer API, Variables](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UMaterialExpressionSubstrateVert-?lang=en-US) specifies **top-layer thickness in centimetres**. Its parameter-blending option merges inputs rather than preserving separate evaluation. [Epic Developer Community](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UMaterialExpressionSubstrateVert-?lang=en-US\&utm_source=chatgpt.com)

### Required extension

Add a typed coating record containing:

**Coverage, final thickness, top normal, top roughness, IOR/F0, and transmission at a reference thickness**, with optional scattering/haze.

Retain the underlying normal, roughness, and material state.

Further constraints:

- Coated wood can remain **opaque overall**; its upper layer transmits to opaque wood.
- Use the supported transmission-to-MFP authoring path. MFP is a distance, not a color.
- The **Thin-Film** helper is for interference; ordinary varnish build belongs to the layer thickness input.
- Rough coating blur of lower layers is a separate, experimental feature; ordinary coat roughness does not require it.

See [UE 5.8 Substrate overview](https://dev.epicgames.com/documentation/unreal-engine/overview-of-substrate-materials-in-unreal-engine?application_version=5.8), **Helpers; Layer Thickness; MFP; project settings**. [Epic Developer Community](https://dev.epicgames.com/documentation/unreal-engine/overview-of-substrate-materials-in-unreal-engine?application_version=5.8\&utm_source=chatgpt.com)

**Do not assume enabling Substrate guarantees full layering.** Epic distinguishes the default performance-oriented **Blendable GBuffer** from **Adaptive GBuffer**, which supports richer concurrent material responses at greater cost. Validate closure budgets and platform fallback. See [MaterialX documentation, final Substrate section](https://dev.epicgames.com/documentation/unreal-engine/importing-materialx-files-in-unreal-engine). [Epic Developer Community](https://dev.epicgames.com/documentation/unreal-engine/importing-materialx-files-in-unreal-engine?utm_source=chatgpt.com)

For Mixtormat diagnostics, ordinary scene-texture lookup returns only the **first closure** under Adaptive GBuffer; it cannot be assumed to describe the complete layered material. [Programming with Substrate GBuffer Formats — Scene Texture Data](https://dev.epicgames.com/documentation/unreal-engine/programming-with-substrate-gbuffer-formats). [Epic Developer Community](https://dev.epicgames.com/documentation/unreal-engine/programming-with-substrate-gbuffer-formats?utm_source=chatgpt.com)

### Delivery sequence

1. **MVP:** shared treatment/deposition model, physical build, deterministic marks, selective filling, and wear-through using current outputs.
2. **Coating extension:** independent top/interface fields and preserved substrate response.
3. **Later:** richer haze, multiple distinct coatings, and optional wood fiber scattering.

Repeated compatible coats may be deliberately merged. Different tinted or scattering coats require preserved ordering or an explicitly identified approximation.

---

## 8. Aging and defects must remain separate

| Effect | Mechanism distinction | Procedural representation |
|---|---|---|
| **Yellowing** | Formulation-dependent resin chemistry; substrate discoloration is separate | Change film transmission/tint independently of wood color |
| **Lacquer blush** | Moisture/condensation and application conditions | Film-local scattering/haze |
| **Oil bloom** | Can arise from inadequately dried underlying stain, glaze, or filler | Separate compatibility/drying defect |
| **Orange peel / brush marks** | Application and leveling behavior | Outer coating height and normal texture |
| **Cracking/crazing** | Film stress, excessive build, aging, substrate movement, or adhesion failure | Connected film damage, bounded by coating thickness |
| **Moisture damage** | May involve haze, blisters, loss of adhesion, or changes in the wood | Separate optical haze, raised film, and missing film states |
| **Wax residue** | Retained excess and incomplete buffing | Deposited material in retention sites, separate from polish |
| **Scratches/polishing** | Mechanical changes to the outer surface | Mark depth, roughness, and eventual breakthrough |

Mohawk’s [*Solving Common Wood Finishing Problems*](https://www.mohawk-finishing.com/mhk_cds/PFS_Problem_Solver/Solving%20Common%20Wood%20Finishing%20Problems.pdf), **p. 6 white spots/oil bloom/blushing; p. 7 orange peel; pp. 8–9 cracking and adhesion**, distinguishes several of these causes. It supplies process guidance, not universal aging kinetics. [mohawk-finishing.com](https://www.mohawk-finishing.com/mhk_cds/PFS_Problem_Solver/Solving%20Common%20Wood%20Finishing%20Problems.pdf?utm_source=chatgpt.com)

Eastman’s **p. 2** contrasts yellowing-prone alkyd/aromatic formulations with more stable acrylic/aliphatic urethanes; **p. 5** discusses matting and gloss control. This supports separate build, sheen, and discoloration controls. [eastman.com](https://www.eastman.com/content/dam/eastman/corporate/en/literature/c/coatind14974.pdf?utm_source=chatgpt.com)

**Do not ship one “Age” slider that simultaneously browns, whitens, cracks, and roughens every finish.** Use finish-specific optional defect presets, initially off. Without exposure/context data, their placement is artistic—not inferred moisture history or elapsed years.

---

## 9. Visual acceptance tests

These are proposed implementation gates, not tests already run against Mixtormat.

| Test | Passing result |
|---|---|
| **Untreated identity** | Amount = 0 preserves all input channels, validity, scale, and optics. |
| **Zero mask** | Outside scope remains unchanged; no filter halo or deposition leakage. |
| **Grain preservation** | Oil and clear-film operations retain the original grain registration and color detail. |
| **Depression versus pore** | A scratch is not automatically classified as an anatomical pore; optional pore data changes selection deliberately. |
| **Conformal coat** | Increasing build with leveling off follows existing depressions rather than automatically filling them flat. |
| **Filler** | Selected depressions rise toward their envelope; surrounding wood remains intact. Colored filler stays in retained material. |
| **Budgeted leveling** | More leveling redistributes a fixed build budget; it does not create material or lower the substrate. |
| **Coat buildup** | Height output and its derived normal agree. The result is not normal-only detail. |
| **Two-interface highlight** | In the extended path, a smooth outer highlight crosses visible wood detail without inheriting every substrate pore. |
| **Wear-through** | Wood → dye → coat reveals dyed wood when the coat is removed. No ghost coat reflection remains. |
| **Penetrating treatment wear** | Surface-coat removal does not automatically erase absorbed oil/dye. |
| **Tiling** | Height, normals, coverage, and non-axis-aligned application marks remain seamless at all borders. |
| **Scale and resolution** | The same physical tile at 1K and 4K retains the same build and mark widths, subject to representable detail. |
| **Masked build** | Reducing treated area does not increase local thickness by concentrating the old total volume. |
| **Extreme settings** | No negative coating thickness, substrate flattening, NaNs, unexpected hue clipping, or undocumented cross-control coupling. |
| **Layering and serialization** | Reordering changes results appropriately; copy/paste/instances retain seeds, physical units, presets, and coating state. |
| **Renderer fallback** | Blendable, Adaptive, and simplified export paths disclose and consistently reproduce their intended approximation. |

The highest-value release gate is **smooth coat over preserved grain, followed by complete wear-through to the correct underlying material**. That tests the core surfacing contract far more effectively than an attractive isolated varnish preview.