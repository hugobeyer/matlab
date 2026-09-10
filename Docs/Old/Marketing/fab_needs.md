For Mixtormat, I’d treat Fab as a product launch, not just “upload the plugin when it’s done.”

The clean path is:

Ship Mixtormat as a Code Plugin.
Bundle a small, polished starter material/mask library inside the plugin so it has immediate value out of the box.
Keep larger material packs as separate Fab products later.
Build a proper demo/overview project, documentation, screenshots, and a short video before submission.

Fab currently requires Unreal code plugins to include at least one code module and the plugin package must contain the .uplugin, Source, Content, and Config structure. Code plugins can include content needed for the plugin to function. That fits Mixtormat well: the tool itself is the product, and the starter surfaces/masks are supporting content.

I would not make the launch package enormous. Something like:

Mixtormat
├─ Plugin
├─ 12–20 strong materials
├─ 15–30 useful masks
├─ a few effect presets/examples
├─ documentation
└─ showcase/demo material

Enough that a buyer installs it and immediately gets results, but not so many materials that people think they’re buying a texture pack rather than a compositor.

Then later you can do separate products:

Mixtormat — Concrete & Plaster Pack
Mixtormat — Brick & Masonry Pack
Mixtormat — Metals & Rust Pack
Mixtormat — Stone Pack

Those can also serve as marketing for the main plugin.

For the actual Fab publisher setup, you’ll need to enroll as a publisher, accept the Fab Distribution Agreement, create the publisher profile/Creator Code, and complete trader, tax, and payout setup. Fab currently advertises an 88% publisher revenue share.

One thing to plan for now: engine versions. Fab requires code-plugin publishers to submit an updated plugin/project for each supported Unreal Engine version rather than simply assuming forward compatibility. I would launch Mixtormat narrowly:

UE 5.8

first.

Do not promise 5.6/5.7 until you actually build and test them. The shader/RDG/editor integration is complicated enough that supporting old engines immediately would create unnecessary maintenance.

Pricing is worth thinking about early. Fab’s Standard license has both Personal and Professional pricing tiers, and publishers must provide both; they can be the same price or different. The rights themselves are the same, while the buyer tier is determined by revenue threshold.

For Mixtormat I would probably position it as a serious technical-art tool rather than a cheap utility. Very roughly, something in this territory would make sense to investigate against competitors at launch:

Personal      $49–79
Professional  $99–149

I wouldn’t lock that yet. Once the tool is feature-complete, we should compare current Fab material-authoring/compositing plugins and decide from actual competition.

The listing itself matters a lot. Fab requires a thumbnail and at least one additional media item such as an image, video, or 3D preview before review. For this product, I would make the showcase primarily visual:

1. Hero image:
   raw material → Mixtormat → finished wall/floor

2. 30–60 sec video:
   add material
   add pattern IDs
   wear edges
   add mask
   change material
   final result

3. UI screenshot:
   clearly show Layers / Inspector / Material library / viewport

4. Before/after comparisons:
   brick
   plaster
   metal/rust
   stone

5. One “everything is procedural” image:
   BC / Normal / RAMH / Height / IDs / final

The strongest selling message is not “it has 50 parameters.” It’s:

Build, layer, weather, mask, and vary production-ready Unreal materials directly inside Unreal without leaving the editor.

I would also get documentation into decent shape before Fab review. Code/tool products benefit from having clear setup and usage docs, and Fab explicitly expects documentation for functional assets in several categories. For Mixtormat I’d have:

Quick Start
Installing
Creating a Mixtormat material
Layers
Masks
Pattern IDs
Effects
Drivers / References
Importing your own materials
Export / Bake
Performance / 4K notes
Troubleshooting

And before submitting, we should do one specific pass I’d call the Fab audit:

remove development junk
check plugin folder structure
check hardcoded /MaterialLab/ paths
verify fresh-project installation
verify no developer-only assets
verify all assets save/load
verify no missing references
check packaged ZIP
check UE 5.8 clean build
check shader compilation from cold cache
check uninstall/reinstall
check documentation links
check licenses for any third-party code/assets

That /MaterialLab/ naming debt is something I would definitely resolve before shipping. Fab reviewers and buyers should not see an old internal product name leaking through the final Mixtormat package.

So the sequence I’d use is:

Finish core features
      ↓
Import / feature masks / UX cleanup
      ↓
Create 12–20 shipping-quality materials
      ↓
Fab audit + rename/path cleanup
      ↓
Documentation
      ↓
Trailer/screenshots
      ↓
Pricing/competitor research
      ↓
Submit UE 5.8 version to Fab

When you’re approaching that point, I’d do the full Fab submission audit against the current requirements rather than relying on what they are today, because Fab’s technical requirements are explicitly subject to change.