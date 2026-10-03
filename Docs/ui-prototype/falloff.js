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

    // Card header: same curve, its own exponent. Lifts off the card's own body opacity rather
    // than fading to zero, so the seam into the body below it stays continuous.
    const cardPower = read('--card-falloff-power');
    root.style.setProperty('--card-header-gradient', ramp('--ground-rgb', read('--card-header-opacity'), read('--card-body-opacity'), cardPower));
  }

  const ui = window.MixtormatPrototype;
  ui.refreshFalloff = refreshFalloff;
  refreshFalloff();
})();