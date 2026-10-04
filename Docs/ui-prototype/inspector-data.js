/* Representative inspector schemas, not a dump of every Unreal parameter.
   Structure maps to existing builders; values are demo values, not engine defaults.
   Only foldout() creates a collapsible section. card() always owns its controls. */
'use strict';
window.MixtormatPrototypeData = (() => {
  const number = (label, value = 0, min = 0, max = 1, extra = {}) =>
    ({ type: 'number', label, value, min, max, ...extra });
  const dropdown = (label, options, selected = 0) => ({ type: 'dropdown', label, options, selected });
  const toggle = (label, value = false, extra = {}) => ({ type: 'toggle', label, value, ...extra });
  const pair = (...rows) => ({ type: 'pair', rows });
  const curve = () => ({ type: 'curve', label: 'Curve Bias' });
  const card = (title, rows, options = {}) => ({ type: 'card', title, rows, ...options });
  const foldout = (title, rows, open = true) => ({ type: 'foldout', title, rows, open });
  const shaping = () => card('Shaping', [toggle('Normalize Input'), pair(number('Input Min', 0), number('Input Max', 1)), curve(), pair(number('Balance', .5), number('Contrast', 1, 0, 4)), number('Offset', 0, -1, 1), toggle('Invert')]);
  const composition = () => foldout('Composition', [
    card('Height Blend', [pair({ ...dropdown('Blend', ['Height Blend', 'Normal', 'Add', 'Multiply']), bare: true }, number('Softness', .1)), number('Amount', 1, 0, 4)], { debug: true, layout: 'blend-controls' }),
    card('Blending / Opacity', [{ type: 'segments', options: ['COMBINE', 'OVERRIDE', 'COAT', 'DETAIL'] }, pair({ ...dropdown('Blend', ['Normal', 'Multiply', 'Add', 'Screen']), bare: true }, number('Amount', 1)), number('Opacity', 1)], { layout: 'blend-controls' }),
    card('Color', [number('Hue Shift', 0, -1, 1), pair(number('Saturation', 1, 0, 2), number('Value', 1, 0, 2))], { eye: 'right', debug: true })
  ]);
  const surface = [
    foldout('Transform', [card('Transform', [number('Tiling', 1, .1, 8), pair(number('Scale X', 1, .1, 8), number('Scale Y', 1, .1, 8)), pair(number('Offset X', 0, -1, 1), number('Offset Y', 0, -1, 1)), number('Rotate', 0, 0, 270, { integer: true, step: 90, unit: '°' }), pair(toggle('Flip U'), toggle('Flip V'))], { layout: 'transform' })]),
    foldout('Height Blend', [number('Blend Strength', 1, 0, 4), number('Threshold', .5), number('Edge Softness', .1), number('Base Bias', 0, -1, 1), number('Blend Bias', 0, -1, 1), number('Smooth Radius', 0, 0, 32, { integer: true }), number('Smooth Amount', 1), { type: 'rows', rows: [card('Contact AO', [number('Amount', .5), number('Width', .05)], { eye: 'left', debug: true }), card('Border Normal', [number('Amount', .3), number('Width', .1)], { eye: 'right', debug: true }), number('Shared Smoothing', 2, 1, 4, { integer: true })] }, toggle('Invert Base Height')]),
    composition(),
    foldout('Surface Adjustments', [card('Roughness', [number('Bias', .5), number('Contrast', 0, -1, 1), number('Offset', 0, -1, 1)], { eye: 'left' }), card('Relief', [number('Height Booster', 1, 0, 4), number('Height Offset', 0, -1, 1), number('Height Smooth', 0)], { debug: true }), card('Featured Masks', [number('Normal Influence', 0), pair(number('Radius', 2, 1, 16, { integer: true }), number('Smoothing', 2, 1, 4, { integer: true }))], { eye: 'right' })], false)
  ];
  const schemas = {
    surface: { title: 'Weathered Alloy', kind: 'Material layer', badge: 'COMBINE', groups: surface },
    mask: { title: 'Directional Wear', kind: 'Generated mask', badge: 'MASK', groups: [foldout('Mask', [card('Directional Wear · asset-name heading', [dropdown('Asset', ['Directional Wear', 'Clouds', 'Scratches']), dropdown('Channel', ['Red', 'Green', 'Blue', 'Alpha']), dropdown('Blend', ['Multiply', 'Add', 'Subtract']), number('Amount', 1)], { eye: 'right', debug: true }), shaping(), card('Generated Shaping', [toggle('Normalize Weights', true), pair(number('Broadness', 2, 1, 32, { integer: true }), number('Smoothing', 2, 1, 4, { integer: true })), number('Bias', .5), pair(number('Warp', 0, 0, .05), number('Radius', 1, 1, 16, { integer: true }))])])] },
    effect: { title: 'Procedural Peel', kind: 'Layer effect', badge: 'EFFECT', groups: [foldout('Procedural Peel', [card('Source', [dropdown('Seed Mask', ['Child / Auto', 'Layer Mask', 'Custom Asset']), number('Amount', .7)], { eye: 'right', debug: true }), card('Peeling', [number('Scale', 1, .1, 8), number('Spread', .25), number('Edge Width', .1)]), card('Relief', [number('Height', .4), number('Edge Detail', .2)], { debug: true })]), foldout('Stain', [card('Source', [dropdown('Liquid Mask', ['Child / Auto', 'Clouds']), number('Tiling', 1, .1, 8), number('Liquid Amount', 1)]), card('Dirt / Minerals', [number('Amount', .3), number('Roughness', .8)], { eye: 'left' })], false), foldout('Erosion', [card('Edges', [number('Amount', .25), number('Scale', 1, .1, 8), number('Bias', 0, -1, 1)])], false)] },
    generator: { title: 'Generator Flow', kind: 'Generator layer', badge: 'GENERATE', groups: [foldout('Generator Layer', [card('Source', [dropdown('Source', ['Normal', 'Height', 'Mask']), number('Strength', .6)], { eye: 'right', debug: true }), card('Flow', [number('Amount', .4), number('Scale', 2, .1, 8), number('Offset', 0, -1, 1)]), card('Composition', [number('Height Blend Amount', 1), dropdown('Blend', ['Normal', 'Add', 'Multiply'])])]), foldout('Outputs', [card('Channels', [toggle('Base Color', true), toggle('Normal', true), toggle('Roughness', true)])], false)] },
    ids: { title: 'Pattern IDs', kind: 'ID module', badge: 'IDS', groups: [foldout('Pattern IDs', [card('Pattern', [dropdown('Pattern', ['Cells', 'Bricks', 'Tiles']), number('Scale', 3, .1, 12), number('Seed', 12, 0, 100, { integer: true })], { eye: 'right', debug: true }), card('Variation', [number('Amount', .5), pair(number('Hue', .2), number('Value', .1))]), card('Legacy Treatment', [card('UV Variation', [number('Rotation', .2), number('Offset', .1)]), card('Relief', [number('Height', .1)]), card('Edges', [number('Width', .05)])]), card('Outputs', [toggle('Color', true), toggle('Relief', true), toggle('Edges')])])] },
    outputs: { title: 'ID Group Outputs', kind: 'Output reference', badge: 'OUTPUT', groups: [foldout('ID Group', [card('Outputs', [dropdown('Source', ['Pattern IDs', 'Generator Flow']), toggle('Base Color', true), toggle('Height', true), number('Amount', 1)], { debug: true })])] },
    states: { title: 'Component States', kind: 'Visual test bench', badge: 'STATES', groups: [foldout('Shared Controls', [card('Draggers', [number('Normal', .5), number('Modified', .8, 0, 1, { defaultValue: .5 }), number('Disabled', .5, 0, 1, { disabled: true }), number('Signed / Zero Divider', 0, -1, 1), pair(number('Paired X', 1, 0, 2), number('Paired Y', 1, 0, 2)), number('Long parameter title clipped without changing geometry', .5)], { eye: 'right', debug: true }), card('A deliberately long group title that must truncate instead of wrapping', [dropdown('Blend', ['Normal', 'Multiply', 'A very long selected option']), pair(dropdown('Primary', ['Red', 'Green']), dropdown('Secondary', ['Blue', 'Alpha'])), toggle('Enabled', true), toggle('Disabled sample', false, { disabled: true })], { eye: 'left', compactToggles: true }), card('Nested card demonstration', [card('Subgroup', [number('Value', .3)])]), shaping()])] }
  };
  const layers = [
    { title: 'Weathered Alloy', group: true, id: 'weathered-alloy' },
    { title: 'Brushed Metal', groupId: 'weathered-alloy', schema: 'surface', source: 'METAL', thumb: 'metal', heightBadge: 'REP' },
    { title: 'Directional Wear', groupId: 'weathered-alloy', schema: 'mask', child: true, source: 'MASK', blendBadge: 'MULT' },
    { title: 'Procedural Peel', groupId: 'weathered-alloy', schema: 'effect', child: true, source: 'EFFECT', blendBadge: 'PEEL' },
    { title: 'Generator Flow', groupId: 'weathered-alloy', schema: 'generator', child: true, source: 'GEN', blendBadge: 'ADD' },
    { title: 'Pattern IDs', groupId: 'weathered-alloy', schema: 'ids', child: true, source: 'IDS' },
    { title: 'ID Group Outputs', groupId: 'weathered-alloy', schema: 'outputs', child: true, source: 'OUT' },
    { title: 'Oxidized Finish', groupId: 'weathered-alloy', schema: 'effect', source: 'METAL', thumb: 'metal', colorBadge: 'MULT', heightBadge: 'ADD' },
    { title: 'Component States', schema: 'states', source: 'DEMO' }
  ];
  const materials = [
    ['Brushed Alloy', 'Metal', 'metal'], ['Oxidized Steel', 'Metal', 'metal'], ['Limestone', 'Stone', 'stone'], ['Granite', 'Stone', 'stone'], ['Oak Grain', 'Organic', 'organic'], ['Bark', 'Organic', 'organic'], ['Worn Copper', 'Metal', 'metal'], ['Sandstone', 'Stone', 'stone']
  ];
  const masks = [['Directional', 'All', 'mask'], ['Clouds', 'All', 'stone'], ['Scratches', 'All', 'mask'], ['Grain', 'All', 'organic'], ['Cells', 'All', 'stone'], ['Edges', 'All', 'mask']];
  return { schemas, layers, materials, masks };
})();
