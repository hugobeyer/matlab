/* Shared gradient falloff. Authored controls stay in tokens.css as plain numbers; the sampled
   stops they produce are derived here and written back as gradient strings on :root.
   opacity(t) = top + (bottom - top) * pow(t, exponent), t in [0, 1].
   For a decreasing fade: exponent < 1 fades earlier, 1 is linear, > 1 holds the top longer. */
'use strict';
(() => {
  const root = document.documentElement;
  const SAMPLES = 6;
  const read = name => parseFloat(getComputedStyle(root).getPropertyValue(name)) || 0;

  // Samples the power curve into gradient stops for one additive source color.
  function ramp(colorToken, top, bottom, exponent) {
    const power = Math.max(exponent, .01);
    const stops = [];
    for (let index = 0; index < SAMPLES; index++) {
      const t = index / (SAMPLES - 1);
      const opacity = top + (bottom - top) * Math.pow(t, power);
      stops.push(`rgb(var(${colorToken}) / ${opacity.toFixed(4)}) ${(t * 100).toFixed(1)}%`);
    }
    return `linear-gradient(${stops.join(', ')})`;
  }

  // Rebuilds every derived falloff gradient from the currently authored tokens.
  function refreshFalloff() {
    const foldPower = read('--foldout-falloff-power');
    const tintTop = read('--header-tint-opacity');
    const hoverTop = read('--header-hover-opacity');
    // The body stop is fixed at zero contribution: the header dissolves into the body color.
    root.style.setProperty('--foldout-lift-gradient', ramp('--header-tint-rgb', tintTop, 0, foldPower));
    root.style.setProperty('--foldout-lift-gradient-hover', ramp('--header-hover-rgb', hoverTop, 0, foldPower));
        // Saturated accent, multiplied: fades to transparent so it contributes nothing at the seam,
        // leaving the additive lift as the only thing the header hands to the body.
        root.style.setProperty('--foldout-accent-multiply-gradient', ramp('--accent-rgb', read('--foldout-accent-multiply-opacity'), 0, foldPower));
        root.style.setProperty('--foldout-accent-hover-multiply-gradient', ramp('--accent-rgb', read('--foldout-accent-hover-multiply-opacity'), 0, foldPower));

    const fillPower = read('--fill-falloff-power');
    root.style.setProperty('--fill-body-gradient', ramp('--accent-rgb', read('--fill-body-top'), read('--fill-body-bottom'), fillPower));
    root.style.setProperty('--fill-body-gradient-hover', ramp('--accent-rgb', read('--fill-body-hover-top'), read('--fill-body-hover-bottom'), fillPower));
    root.style.setProperty('--fill-body-gradient-active', ramp('--accent-rgb', read('--fill-body-active-top'), read('--fill-body-active-bottom'), fillPower));

    // Continue the header fade into the body and mirror its tail at the bottom.
    // Cap each tail at half the body height so short cards cannot overlap the stops.
    const cardPower = read('--card-falloff-power');
    const cardTop = read('--card-header-opacity');
    const cardBottom = read('--card-body-opacity');
    const reach = read('--card-gradient-reach');
    const headerHeight = Math.max(read('--card-header-height'), 1);
    const seam = headerHeight / (headerHeight + reach);
    const opacityAt = t => cardTop + (cardBottom - cardTop) * Math.pow(t, Math.max(cardPower, .01));
    const headerStops = [];
    const bodyStops = [];
    const bottomStops = [];
    for (let index = 0; index < SAMPLES; index++) {
      const t = index / (SAMPLES - 1);
      headerStops.push(`rgb(var(--ground-rgb) / ${opacityAt(t * seam).toFixed(4)}) ${(t * 100).toFixed(1)}%`);
      const color = `rgb(var(--ground-rgb) / ${opacityAt(seam + t * (1 - seam)).toFixed(4)})`;
      const position = `min(${(t * reach).toFixed(2)}px, ${(t * 50).toFixed(1)}%)`;
      bodyStops.push(`${color} ${position}`);
      bottomStops.unshift(`${color} calc(100% - ${position})`);
    }
    root.style.setProperty('--card-header-gradient', `linear-gradient(${headerStops.join(', ')})`);
    root.style.setProperty('--card-body-gradient', reach > 0 ? `linear-gradient(${[...bodyStops, ...bottomStops].join(', ')})` : `linear-gradient(rgb(var(--ground-rgb) / ${cardBottom}), rgb(var(--ground-rgb) / ${cardBottom}))`);
  }

  const ui = window.MixtormatPrototype;
  ui.refreshFalloff = refreshFalloff;
  refreshFalloff();
})();