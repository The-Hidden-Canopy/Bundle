// Seed-jar motif. Growth reflects progress toward protected savings goals only;
// spending never changes it, and it never wilts. Every jar carries text.
"use strict";

(function () {
  const STAGES = ["seeds", "shoot", "sprout"];

  function stageFor(fraction) {
    if (!(fraction > 0)) return 0;
    if (fraction < 1 / 3) return 0;
    if (fraction < 2 / 3) return 1;
    return 2;
  }

  // A simple, bold outline so the jar stays legible at 16px.
  function jarSvg(stage, { size = 48, label = "", decorative = false } = {}) {
    const shoot = stage >= 1
      ? '<path class="jar-stem" d="M32 39 V27"/>' +
        '<path class="jar-leaf" d="M32 31 C27 30 24 27 24 23 C28 23 31 26 32 31 Z"/>'
      : "";
    const sprout = stage >= 2
      ? '<path class="jar-stem" d="M32 27 V16"/>' +
        '<path class="jar-leaf" d="M32 22 C37 21 41 17 41 12 C36 12 33 16 32 22 Z"/>' +
        '<path class="jar-leaf" d="M32 18 C29 16 27 13 28 9 C31 10 33 13 32 18 Z"/>'
      : "";
    const a11y = decorative
      ? 'aria-hidden="true" focusable="false"'
      : `role="img" aria-label="${label.replace(/"/g, "&quot;")}"`;
    return `<svg class="jar jar-stage-${STAGES[stage]}" viewBox="0 0 64 64" width="${size}" height="${size}" ${a11y}>` +
      '<rect class="jar-lid" x="20" y="6" width="24" height="7" rx="2"/>' +
      '<path class="jar-glass" d="M22 13 V17 C16 19 13 24 13 30 V50 C13 55 17 58 22 58 H42 C47 58 51 55 51 50 V30 C51 24 48 19 42 17 V13 Z"/>' +
      sprout + shoot +
      '<g class="jar-seeds"><ellipse cx="25" cy="50" rx="4.5" ry="3.2"/><ellipse cx="34" cy="51" rx="4.5" ry="3.2"/>' +
      '<ellipse cx="42" cy="49" rx="4" ry="3"/><ellipse cx="30" cy="45" rx="4" ry="3"/><ellipse cx="38" cy="44" rx="3.6" ry="2.7"/></g>' +
      '</svg>';
  }

  window.BundleJar = { STAGES, stageFor, jarSvg };
})();
