// Bundle by The Hidden Canopy -- local interface.
// Every figure on screen comes from the local Bundle backend (/api/*), which runs
// the native `bundle` engine. Nothing here invents balances or calculations.
"use strict";

(function () {
  const TOKEN = document.querySelector('meta[name="bundle-session"]').content;
  const { jarSvg, stageFor, STAGES } = window.BundleJar;
  const MODES = {
    confirmed: { label: "Confirmed", text: "Only commitments with verified confidence. Goals and anything less certain are left out." },
    expected: { label: "Expected", text: "Verified, supported and inferred commitments and protected goals. Estimated and unknown items are left out." },
    stress: { label: "Stress", text: "Everything except unknown or stale items, using the maximum of any range." },
  };
  const HORIZONS = [7, 14, 30, 60, 90];
  const EXPONENT = { JPY: 0, KWD: 3 };

  const state = { workspace: null, snapshot: null, savedFloor: null, calc: null, calcError: null, preview: null, lastAction: null };

  // -- utilities ------------------------------------------------------------------------
  const esc = (value) => String(value ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  function money(minor, currency = "USD", { sign = false } = {}) {
    if (minor === null || minor === undefined) return "—";
    const exp = EXPONENT[currency] ?? 2;
    const text = new Intl.NumberFormat(undefined, { style: "currency", currency, minimumFractionDigits: exp, maximumFractionDigits: exp }).format(Math.abs(minor) / 10 ** exp);
    if (minor < 0) return "−" + text;
    return sign && minor > 0 ? "+" + text : text;
  }
  const toMinor = (text, currency = "USD") => {
    const cleaned = String(text).replace(/[^0-9.]/g, "");
    if (!cleaned) return null;
    const exp = EXPONENT[currency] ?? 2;
    const [whole, frac = ""] = cleaned.split(".");
    return parseInt(whole || "0", 10) * 10 ** exp + parseInt((frac + "000").slice(0, exp) || "0", 10);
  };
  const fmtDate = (iso, withYear = false) => iso ? new Date(iso).toLocaleDateString(undefined, { month: "short", day: "numeric", ...(withYear ? { year: "numeric" } : {}) }) : "—";
  const fmtDateTime = (iso) => iso ? new Date(iso).toLocaleString(undefined, { month: "short", day: "numeric", year: "numeric", hour: "numeric", minute: "2-digit" }) : "—";
  const daysAgo = (iso) => (Date.now() - new Date(iso).getTime()) / 86400000;
  const ago = (iso) => { const d = daysAgo(iso); return d < 1 ? "today" : d < 2 ? "1 day ago" : `${Math.floor(d)} days ago`; };
  const title = (text) => String(text || "").replace(/_/g, " ").replace(/^./, (c) => c.toUpperCase());
  const currencyOf = () => state.snapshot?.accounts?.[0]?.currency || "USD";
  const storeKey = (name) => `bundle:${state.workspace?.database || "none"}:${name}`;
  const remember = (name, value) => { try { localStorage.setItem(storeKey(name), JSON.stringify(value)); } catch (_) { /* storage optional */ } };
  const recall = (name, fallback) => { try { const v = localStorage.getItem(storeKey(name)); return v === null ? fallback : JSON.parse(v); } catch (_) { return fallback; } };

  const ICONS = {
    home: '<path d="M3 11 12 4l9 7v9a1 1 0 0 1-1 1h-5v-6H9v6H4a1 1 0 0 1-1-1z"/>',
    list: '<path d="M8 6h13M8 12h13M8 18h13M3.5 6h.01M3.5 12h.01M3.5 18h.01"/>',
    card: '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M3 10h18"/>',
    chart: '<path d="M5 20V12M12 20V5M19 20v-9"/>',
    doc: '<path d="M7 3h7l5 5v13H7z"/><path d="M14 3v5h5M10 13h6M10 17h6"/>',
    book: '<path d="M4 5a2 2 0 0 1 2-2h13v16H6a2 2 0 0 0-2 2z"/><path d="M4 19V5"/>',
    help: '<circle cx="12" cy="12" r="9"/><path d="M9.5 9a2.5 2.5 0 1 1 3.5 2.3c-.6.3-1 .9-1 1.6V14M12 17h.01"/>',
    gear: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.6 1.6 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.6 1.6 0 0 0-2.7 1.1V21a2 2 0 1 1-4 0v-.1a1.6 1.6 0 0 0-2.7-1.1l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1A1.6 1.6 0 0 0 3.6 15H3a2 2 0 1 1 0-4h.1a1.6 1.6 0 0 0 1.1-2.7l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1A1.6 1.6 0 0 0 9.7 4.4V4a2 2 0 1 1 4 0v.1a1.6 1.6 0 0 0 2.7 1.1l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.6 1.6 0 0 0-.3 1.8V10c.3.6 1 1 1.6 1H21a2 2 0 1 1 0 4h-.1c-.7 0-1.3.4-1.5 1z"/>',
    cart: '<path d="M3 4h2l2.4 11h10.2L20 8H6.2"/><circle cx="9" cy="19.5" r="1.3"/><circle cx="17" cy="19.5" r="1.3"/>',
    swap: '<path d="M4 8h14l-3-3M20 16H6l3 3"/>',
    undo: '<path d="M9 7 4 12l5 5"/><path d="M4 12h11a5 5 0 0 1 0 10h-2"/>',
    income: '<path d="M12 19V5M6 11l6-6 6 6"/>',
    house: '<path d="M3 11 12 4l9 7"/><path d="M5 10v10h14V10"/>',
    bolt: '<path d="M13 2 4 14h7l-1 8 9-12h-7z"/>',
    bank: '<path d="M3 10 12 4l9 6M5 10v8M9.5 10v8M14.5 10v8M19 10v8M3 20h18"/>',
    upload: '<path d="M12 16V4M7 9l5-5 5 5M4 20h16"/>',
  };
  const icon = (name, label = "") => `<svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" ${label ? `role="img" aria-label="${esc(label)}"` : 'aria-hidden="true" focusable="false"'}>${ICONS[name] || ""}</svg>`;

  async function api(path, { method = "GET", body } = {}) {
    let response;
    try {
      response = await fetch(path, { method, headers: { "X-Bundle-Token": TOKEN, "Content-Type": "application/json" }, body: body === undefined ? undefined : JSON.stringify(body) });
    } catch (_) {
      throw Object.assign(new Error("Bundle's local service is not answering."), { recovery: "Start Bundle again (python -m bundle_ui --db …) and reload this page. Your data is safe in the database file." });
    }
    let data;
    try { data = await response.json(); } catch (_) { data = { error: `Unexpected response (${response.status}).` }; }
    if (!response.ok) throw Object.assign(new Error(data.error || "Request failed."), { recovery: data.recovery || "" });
    return data;
  }

  function errorNotice(error, retryLabel = "", retryId = "") {
    return `<div class="notice error" role="alert"><h3>${esc(error.message || error)}</h3>` +
      (error.recovery ? `<p>${esc(error.recovery)}</p>` : "") +
      (retryLabel ? `<button type="button" class="button small" id="${esc(retryId)}">${esc(retryLabel)}</button>` : "") + "</div>";
  }

  // -- data loading ---------------------------------------------------------------------
  function settings() {
    return { mode: recall("mode", "expected"), horizon: recall("horizon", 14) };
  }
  async function loadState() {
    const data = await api("/api/state");
    state.workspace = data.workspace;
    state.snapshot = data.snapshot;
    state.savedFloor = data.saved_floor;
  }
  async function loadCalc() {
    state.calc = null;
    state.calcError = null;
    if (!state.snapshot.accounts.length) return;
    const { mode, horizon } = settings();
    const floor = state.savedFloor ? state.savedFloor.floor_minor : 0;
    try {
      state.calc = await api(`/api/calculate?mode=${mode}&horizon_days=${horizon}&floor_minor=${floor}`);
    } catch (error) {
      state.calcError = error;
    }
  }
  async function refresh() {
    await loadState();
    await loadCalc();
  }

  // -- shared pieces --------------------------------------------------------------------
  function protectedProgress() {
    const goals = state.snapshot.goals.filter((g) => g.protected === 1 && g.status === "active");
    const reserved = goals.reduce((sum, g) => sum + g.current_reserved_minor, 0);
    const target = goals.reduce((sum, g) => sum + g.target_minor, 0);
    const fraction = target > 0 ? reserved / target : 0;
    return { goals, reserved, target, fraction, stage: stageFor(fraction) };
  }
  function jarStages(current) {
    const names = { seeds: "Seeds", shoot: "Shoot", sprout: "Sprout" };
    return `<ol class="jar-stages" aria-label="Savings stages">${STAGES.map((st, i) => `<li${i === current ? ' aria-current="step" class="current"' : ""}>${jarSvg(i, { size: 36, decorative: true })}<span>${names[st]}${i === current ? '<span class="visually-hidden"> (current)</span>' : ""}</span></li>`).join("")}</ol>`;
  }
  function jarBlock(reserved, target, name, { size = 72, stages = false } = {}) {
    const fraction = target > 0 ? Math.min(reserved / target, 1) : 0;
    const stage = stageFor(fraction);
    const pct = Math.round(fraction * 100);
    const words = target > 0
      ? `${money(reserved, currencyOf())} of ${money(target, currencyOf())} reserved (${pct}%)`
      : "No protected savings goal yet";
    const label = `${name}: ${words}. Jar shows ${STAGES[stage]}.`;
    return `<div class="jar-card">${jarSvg(stage, { size, label })}<div>` +
      `<div class="row-title">${esc(name)}</div><div>${esc(words)}</div>` +
      (target > 0 ? `<progress class="progress" max="100" value="${pct}" aria-label="${esc(name)} progress"></progress>` : "") +
      (stages ? jarStages(stage) : "") +
      `<div class="small muted">Stage: ${STAGES[stage]} (seeds below one third, a shoot from one third, a sprout from two thirds). Only savings progress changes the jar.</div></div></div>`;
  }
  function confidencePill(confidence) {
    const text = { verified: "Verified", supported: "Supported", inferred: "Inferred", estimated: "Estimated", unknown: "Unknown", stale: "Stale" }[confidence] || title(confidence);
    return `<span class="pill ${esc(confidence)}">${esc(text)}</span>`;
  }
  function classificationPill(tx) {
    switch (tx.classification) {
      case "transfer_out": case "transfer_in": return '<span class="pill transfer">Transfer</span>';
      case "refund": return '<span class="pill refund">Refund</span>';
      case "refunded_purchase": return '<span class="pill refund">Refunded</span>';
      case "income": return '<span class="pill ok">Money in</span>';
      default: return '<span class="pill info">Spending</span>';
    }
  }
  function txAmount(tx) {
    if (tx.classification === "transfer_out" || tx.classification === "transfer_in") {
      return `<span class="amount neutral" title="Moved between your own accounts; not spending">${tx.classification === "transfer_out" ? "→ " : "← "}${money(tx.amount_minor, tx.currency)}</span>`;
    }
    const minor = tx.direction === "credit" ? tx.amount_minor : -tx.amount_minor;
    return `<span class="amount ${minor > 0 ? "credit" : ""}">${money(minor, tx.currency, { sign: true })}</span>`;
  }
  function txIcon(tx) {
    if (tx.classification.startsWith("transfer")) return icon("swap");
    if (tx.classification === "refund") return icon("undo");
    if (tx.direction === "credit") return icon("income");
    return icon("cart");
  }
  function stateText(tx) {
    return tx.transaction_state === "posted" ? "" : ` <span class="pill pending">${esc(title(tx.transaction_state))}</span>`;
  }
  function accountName(id) {
    const account = state.snapshot.accounts.find((a) => a.account_id === id);
    return account ? account.display_name : id;
  }
  function accountWarnings(account) {
    const warnings = [];
    if (!account.has_observed_balance) warnings.push("No balance has been observed for this account yet. Safe-to-spend counts it as zero.");
    if (account.balance_freshness === "stale") warnings.push("The source marked this balance as stale.");
    if (account.stale_by_age) warnings.push(`Balance observed ${ago(account.balance_observed_at)} — older than ${state.snapshot.stale_after_days} days, so it may be out of date.`);
    return warnings;
  }

  // Forecast chart from the engine's projected points (closing balance per day).
  function chartSvg(points, currency) {
    if (!points || points.length < 2) return '<p class="muted">Not enough projected points to draw an outlook.</p>';
    const W = 640, H = 230, L = 64, R = 16, T = 12, B = 30;
    const values = points.map((p) => p.closing_minor).concat(points.map((p) => p.floor_minor));
    let lo = Math.min(0, ...values), hi = Math.max(...values);
    if (hi === lo) hi = lo + 100;
    // Round the axis to a readable step (1, 2 or 5 x 10^n minor units).
    const raw = (hi - lo) / 4, mag = 10 ** Math.floor(Math.log10(raw));
    const step = [1, 2, 5, 10].map((k) => k * mag).find((v) => v >= raw);
    lo = Math.floor(lo / step) * step; hi = Math.ceil((hi * 1.04) / step) * step;
    const x = (i) => L + (i / (points.length - 1)) * (W - L - R);
    const y = (v) => T + (1 - (v - lo) / (hi - lo)) * (H - T - B);
    const line = points.map((p, i) => `${i ? "L" : "M"}${x(i).toFixed(1)},${y(p.closing_minor).toFixed(1)}`).join(" ");
    const area = `${line} L${x(points.length - 1).toFixed(1)},${y(lo).toFixed(1)} L${x(0).toFixed(1)},${y(lo).toFixed(1)} Z`;
    const ticks = []; for (let v = lo; v <= hi + 1e-9; v += step) ticks.push(v);
    let minIndex = 0;
    points.forEach((p, i) => { if (p.closing_minor < points[minIndex].closing_minor) minIndex = i; });
    const floor = points[0].floor_minor;
    const label = `Projected balance from ${fmtDate(points[0].date)} to ${fmtDate(points[points.length - 1].date)}. Lowest point ${money(points[minIndex].closing_minor, currency)} on ${fmtDate(points[minIndex].date)}. Protected floor ${money(floor, currency)}.`;
    const xTicks = [0, Math.floor((points.length - 1) / 2), points.length - 1];
    return `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="${esc(label)}">` +
      ticks.map((v) => `<line class="grid-line" x1="${L}" x2="${W - R}" y1="${y(v)}" y2="${y(v)}"/><text class="axis" x="${L - 8}" y="${y(v) + 4}" text-anchor="end">${esc(money(v, currency).replace(/\.00$/, ""))}</text>`).join("") +
      `<path class="area" d="${area}"/><path class="line" d="${line}"/>` +
      (floor > 0 ? `<line class="floor" x1="${L}" x2="${W - R}" y1="${y(floor)}" y2="${y(floor)}"/><text class="floor-label" x="${W - R}" y="${y(floor) - 6}" text-anchor="end">Protected floor (${esc(money(floor, currency))})</text>` : "") +
      `<circle class="min-dot" cx="${x(minIndex)}" cy="${y(points[minIndex].closing_minor)}" r="5"/>` +
      xTicks.map((i) => `<text class="axis" x="${x(i)}" y="${H - 8}" text-anchor="${i === 0 ? "start" : i === points.length - 1 ? "end" : "middle"}">${esc(fmtDate(points[i].date))}</text>`).join("") +
      "</svg>";
  }
  function chartBlock(points, currency) {
    const changes = (points || []).filter((p, i) => i === 0 || p.inflows_minor || p.outflows_minor);
    return `<div class="legend"><span><span class="swatch"></span>Projected balance (estimate)</span><span><span class="swatch floor"></span>Protected floor</span></div>` +
      `<div class="chart">${chartSvg(points, currency)}</div>` +
      `<details><summary>Show the projection as a table</summary><div class="table-wrap"><table class="data"><thead><tr><th>Date</th><th class="amount">In</th><th class="amount">Out</th><th class="amount">Closing balance</th></tr></thead><tbody>` +
      changes.map((p) => `<tr><td>${esc(fmtDate(p.date, true))}</td><td class="amount">${money(p.inflows_minor, currency)}</td><td class="amount">${money(p.outflows_minor, currency)}</td><td class="amount">${money(p.closing_minor, currency)}</td></tr>`).join("") +
      "</tbody></table></div></details>";
  }

  // -- pages ----------------------------------------------------------------------------
  function pageHead(name, extra = "") {
    const demo = state.workspace?.demo ? '<span class="pill demo"><span class="dot"></span>Demo data — synthetic</span>' : "";
    return `<div class="page-head"><h1>${esc(name)}</h1>${demo}${extra}<button type="button" class="help-button" data-help aria-label="Help for ${esc(name)}">?</button></div>`;
  }

  function firstRun() {
    return `<section class="card empty">${jarSvg(0, { size: 96, label: "An empty seed jar: no data yet" })}` +
      "<h2>Welcome to Bundle</h2><p class=\"muted\">Bundle keeps your finances in a local file on this computer. Start by importing a statement, or explore a clearly labelled synthetic demo first.</p>" +
      '<div class="u-center u-mt-l actions"><a class="button primary" href="#/import">' + icon("upload") + 'Import a statement</a>' +
      '<button type="button" class="button demo" data-action="start-demo">Try the synthetic demo</button>' +
      '<a class="button" href="#/guide">Read the 5-step guide</a></div></section>';
  }

  function renderOverview() {
    const s = state.snapshot;
    if (!s.accounts.length) return pageHead("Overview") + firstRun();
    const { mode, horizon } = settings();
    const currency = currencyOf();
    let sts = "";
    if (state.calcError) {
      sts = errorNotice(state.calcError, "Try again", "retry-calc");
    } else if (state.calc) {
      const o = state.calc.receipt.outputs;
      const consistent = o.minimum_projected_balance_minor - o.protected_floor_minor === o.safe_to_spend_minor;
      const included = s.accounts.filter((a) => a.include_in_safe_to_spend === 1 && a.status === "active");
      sts = `<section class="card sts" aria-labelledby="sts-title">` +
        `<div><div class="sts-headline">${jarSvg(protectedProgress().stage, { size: 64, decorative: true })}<div>` +
        `<h2 id="sts-title">Safe to spend through ${esc(fmtDate(state.calc.horizon_end))}</h2>` +
        `<div class="sts-amount ${o.safe_to_spend_minor < 0 ? "below" : ""}">${money(o.safe_to_spend_minor, currency)}</div>` +
        `<p class="muted">After commitments, protected savings and your ${money(o.protected_floor_minor, currency)} cash floor, in the ${MODES[mode].label} view.</p></div></div>` +
        `<div class="controls"><fieldset class="segmented" aria-label="Calculation view"><legend class="visually-hidden">Calculation view</legend>` +
        Object.entries(MODES).map(([key, m]) => `<input type="radio" name="mode" id="mode-${key}" value="${key}" ${key === mode ? "checked" : ""}><label for="mode-${key}">${m.label}</label>`).join("") +
        `</fieldset><label class="field">Period<select id="horizon">${HORIZONS.map((d) => `<option value="${d}" ${d === horizon ? "selected" : ""}>Next ${d} days</option>`).join("")}</select></label></div>` +
        `<p class="u-mt-m small muted">${esc(MODES[mode].text)}</p>` +
        (o.warnings || []).map((w) => `<div class="notice warn small">${esc(w)}. This is information, not a judgement: adjust the floor or plan if it does not look right.</div>`).join("") +
        `</div><div><table class="breakdown" aria-label="How safe-to-spend was calculated"><tbody>` +
        `<tr><th>Available now<span class="note">Observed balances of ${included.length} included account${included.length === 1 ? "" : "s"}</span></th><td>${money(o.current_liquid_minor, currency)}</td></tr>` +
        `<tr><th>Commitments in period<span class="note">Counted in the ${MODES[mode].label} view</span></th><td>−${money(o.commitment_total_minor, currency)}</td></tr>` +
        `<tr><th>Protected savings<span class="note">Planned goal contributions due in period</span></th><td>−${money(o.protected_goal_total_minor, currency)}</td></tr>` +
        (o.expected_income_total_minor ? `<tr><th>Expected income</th><td>+${money(o.expected_income_total_minor, currency)}</td></tr>` : "") +
        `<tr><th>Lowest projected balance<span class="note">on ${esc(fmtDate(o.minimum_projected_at, true))}</span></th><td>${money(o.minimum_projected_balance_minor, currency)}</td></tr>` +
        `<tr><th>Protected cash floor</th><td>−${money(o.protected_floor_minor, currency)}</td></tr>` +
        `<tr class="total"><th>= Safe to spend</th><td>${money(o.safe_to_spend_minor, currency)}</td></tr></tbody></table>` +
        `<p class="small muted">Bundle subtracts the floor from the <em>lowest</em> point of the projection, so money needed later in the period is not counted as spendable now.` +
        (consistent ? "" : " <strong>Check failed: the figures above do not add up; please report this.</strong>") + `</p>` +
        `<div class="actions"><button type="button" class="link-button" data-help-topic="calculation">How it's calculated</button>` +
        `<button type="button" class="button small" data-action="record-calc">Save this calculation</button></div>` +
        `<div id="record-result" aria-live="polite"></div></div></section>`;
    } else {
      sts = '<section class="card"><span class="spinner" aria-hidden="true"></span> Calculating…</section>';
    }

    const asOf = state.calc ? new Date(state.calc.as_of) : new Date();
    const horizonEnd = state.calc ? new Date(state.calc.horizon_end) : new Date(Date.now() + horizon * 86400000);
    const counted = new Set();
    (state.calc?.forecast || []).forEach((p) => p.event_ids.forEach((id) => counted.add(id.split(":").slice(0, 2).join(":"))));
    const upcoming = s.commitments.filter((c) => c.status !== "cancelled" && c.status !== "ended");
    const commitmentRows = upcoming.map((c) => {
      const due = new Date(c.next_expected_at);
      const inPeriod = due >= asOf && due <= horizonEnd;
      const isCounted = counted.has(`commitment:${c.commitment_id}`);
      const model = c.amount_model || {};
      const amount = model.primary ? money(model.primary.minor_units, model.primary.currency) : "—";
      const range = model.kind === "range" && model.minimum && model.maximum ? ` · range ${money(model.minimum.minor_units, currency)}–${money(model.maximum.minor_units, currency)}` : "";
      const note = due < asOf ? "Date has passed — not projected; update the plan" : !inPeriod ? "After this period" : isCounted ? "Counted" : `Not counted (${MODES[mode].label})`;
      return `<li><span class="row-icon">${icon(/rent|housing/i.test(c.commitment_type + c.name) ? "house" : "bolt")}</span><div><div class="row-title">${esc(c.name)}</div><div class="row-sub">${esc(fmtDate(c.next_expected_at))} · ${esc(note)}${esc(range)}</div></div>${confidencePill(c.confidence)}<div class="amount">${amount}</div></li>`;
    }).join("");
    const inPeriodTotal = state.calc ? state.calc.receipt.outputs.commitment_total_minor : null;

    const recent = s.transactions.slice(0, 5).map((tx) => `<li><span class="row-icon">${txIcon(tx)}</span><div><div class="row-title">${esc(tx.description || tx.merchant_raw)}${stateText(tx)}</div><div class="row-sub">${esc(accountName(tx.account_id))} · ${esc(fmtDate(tx.posted_at || tx.authorized_at || tx.observed_at))}</div></div>${classificationPill(tx)}${txAmount(tx)}</li>`).join("");
    const accountsTotal = s.accounts.reduce((sum, a) => sum + a.available_minor, 0);
    const accountRows = s.accounts.map((a) => {
      const warnings = accountWarnings(a);
      return `<li><span class="row-icon">${icon("bank")}</span><div><div class="row-title">${esc(a.display_name)}</div><div class="row-sub">${a.last_four ? "•••• " + esc(a.last_four) + " · " : ""}observed ${esc(ago(a.balance_observed_at))}</div><div class="row-sub">${warnings.length ? '<span class="pill warn"><span class="dot"></span>Needs review</span>' : '<span class="pill observed">Observed</span>'}</div></div><span></span><div class="amount">${money(a.available_minor, a.currency)}</div></li>`;
    }).join("");
    const progress = protectedProgress();

    return pageHead("Overview", '<a class="button primary" href="#/import">' + icon("upload") + "Import statement</a>") +
      `<div class="grid grid-overview"><div class="stack">${sts}` +
      `<section class="card" aria-labelledby="outlook-title"><div class="card-head"><h2 id="outlook-title">Cash outlook</h2><span class="small muted">Projection from your plan — an estimate, not observed data</span></div>${state.calc ? chartBlock(state.calc.forecast, currency) : '<p class="muted">The outlook appears once safe-to-spend can be calculated.</p>'}</section>` +
      `<section class="card" aria-labelledby="recent-title"><div class="card-head"><h2 id="recent-title">Recent activity</h2><a href="#/activity">See all</a></div>${recent ? `<ul class="rows">${recent}</ul>` : '<p class="muted">No transactions yet. Import a statement to see them here.</p>'}</section></div>` +
      `<div class="stack"><section class="card" aria-labelledby="commit-title"><div class="card-head"><h2 id="commit-title">Upcoming commitments</h2>${inPeriodTotal !== null ? `<span class="small muted">Counted: <strong>${money(inPeriodTotal, currency)}</strong></span>` : ""}</div>${commitmentRows ? `<ul class="rows">${commitmentRows}</ul>` : '<p class="muted">No commitments in your plan. Commitments arrive with a bundle-finance-1 plan or backup (see Help).</p>'}</section>` +
      `<section class="card" aria-labelledby="acct-title"><div class="card-head"><h2 id="acct-title">Accounts</h2><span class="small muted">Total available <strong>${money(accountsTotal, currency)}</strong></span></div><ul class="rows">${accountRows}</ul></section>` +
      `<section class="card card-soft" aria-labelledby="jar-title"><h2 class="u-mb-m" id="jar-title">Protected savings</h2>${jarBlock(progress.reserved, progress.target, "Protected goals", { stages: progress.target > 0 })}</section>` +
      `<section class="card card-soft"><h2>Make Bundle yours</h2>${guideSteps(true)}<p class="u-mt-m"><a href="#/guide">Open the getting-started guide →</a></p></section></div></div>`;
  }

  function renderActivity() {
    const s = state.snapshot;
    if (!s.transactions.length) return pageHead("Activity") + `<section class="card empty">${jarSvg(0, { size: 72, decorative: true })}<h2>No transactions yet</h2><p class="muted">Import a statement and its transactions appear here with their source and state.</p><a class="button primary" href="#/import">Import a statement</a></section>`;
    remember("reviewed", true);
    const f = recall("activity-filter", { account: "", kind: "", q: "" });
    const rows = s.transactions.filter((tx) => {
      if (f.account && tx.account_id !== f.account) return false;
      if (f.kind === "spending" && !(tx.classification === "spending" || tx.classification === "refunded_purchase")) return false;
      if (f.kind === "income" && tx.classification !== "income") return false;
      if (f.kind === "transfer" && !tx.classification.startsWith("transfer")) return false;
      if (f.kind === "refund" && !tx.classification.startsWith("refund")) return false;
      const text = `${tx.description} ${tx.merchant_raw} ${tx.category_id || ""}`.toLowerCase();
      return !f.q || text.includes(f.q.toLowerCase());
    });
    const unmatchable = s.transactions.filter((tx) => !tx.matchable).length;
    const spending = rows.filter((tx) => tx.classification === "spending" || tx.classification === "refunded_purchase").reduce((sum, tx) => sum + tx.amount_minor, 0);
    const refunds = rows.filter((tx) => tx.classification === "refund").reduce((sum, tx) => sum + tx.amount_minor, 0);
    const transfers = rows.filter((tx) => tx.classification === "transfer_out").reduce((sum, tx) => sum + tx.amount_minor, 0);
    const currency = currencyOf();
    return pageHead("Activity") +
      `<section class="card"><form class="filters" id="activity-filters" role="search" aria-label="Filter activity">` +
      `<label class="field">Account<select name="account"><option value="">All accounts</option>${s.accounts.map((a) => `<option value="${esc(a.account_id)}" ${f.account === a.account_id ? "selected" : ""}>${esc(a.display_name)}</option>`).join("")}</select></label>` +
      `<label class="field">Type<select name="kind">${[["", "Everything"], ["spending", "Spending"], ["income", "Money in"], ["transfer", "Transfers"], ["refund", "Refunds"]].map(([v, t]) => `<option value="${v}" ${f.kind === v ? "selected" : ""}>${t}</option>`).join("")}</select></label>` +
      `<label class="field">Search<input type="text" name="q" value="${esc(f.q)}" placeholder="Merchant, description or category"></label></form>` +
      `<p class="small muted">${rows.length} shown · Spending ${money(spending, currency)} · Refunds back ${money(refunds, currency)} · Moved between accounts ${money(transfers, currency)} (not spending)</p>` +
      `<div class="table-wrap"><table class="data"><caption class="visually-hidden">Transactions</caption><thead><tr><th>Date</th><th>Description</th><th class="col-opt">Category</th><th>Account</th><th class="col-opt">Source</th><th>Type</th><th class="amount">Amount</th></tr></thead><tbody>` +
      rows.map((tx) => `<tr><td class="mono">${esc(fmtDate(tx.posted_at || tx.authorized_at || tx.observed_at, true))}</td><td><div class="row-title">${esc(tx.description || "—")}${stateText(tx)}</div><div class="row-sub">${esc(tx.merchant_raw)}${tx.revision_count > 1 ? ` · revision ${tx.revision} of ${tx.revision_count}` : ""}</div></td><td class="col-opt">${esc(title(tx.category_id) || "—")}</td><td>${esc(accountName(tx.account_id))}</td><td class="col-opt small">${esc((tx.source_kind || "").toUpperCase())}<div class="row-sub">imported ${esc(fmtDate(tx.source_imported_at, true))}</div></td><td>${classificationPill(tx)}${tx.related_transaction_id ? `<div class="row-sub">with ${esc(tx.related_transaction_id)}</div>` : ""}</td><td class="amount">${txAmount(tx)}</td></tr>`).join("") +
      `</tbody></table></div>` +
      `<p class="u-mt-m small muted">Transfers and refunds are matched by Bundle's reconciliation engine: posted transactions with a posted or authorized time, equal amounts and matching text (transfers within ${s.classification_rules.transfer_window_hours} hours, refunds within ${s.classification_rules.refund_window_days} days). ${unmatchable ? `${unmatchable} transaction${unmatchable === 1 ? " has" : "s have"} no posted time (CSV and OFX statements only carry an observed date) or are not posted, so they cannot be matched and are shown as plain spending or money in.` : ""}</p></section>`;
  }

  function renderAccounts() {
    const s = state.snapshot;
    if (!s.accounts.length) return pageHead("Accounts") + `<section class="card empty">${jarSvg(0, { size: 72, decorative: true })}<h2>No accounts yet</h2><p class="muted">Accounts are created by the statements and batches you import. Bundle never connects to a bank.</p><a class="button primary" href="#/import">Import a statement</a></section>`;
    const cards = s.accounts.map((a) => {
      const warnings = accountWarnings(a);
      const observations = s.balance_observations.filter((b) => b.account_id === a.account_id).slice(0, 4);
      return `<section class="card" aria-labelledby="acct-${esc(a.account_id)}"><div class="card-head"><h2 id="acct-${esc(a.account_id)}">${esc(a.display_name)}</h2><span class="pill ${a.balance_freshness === "fresh" ? "observed" : "warn"}">${esc(title(a.balance_freshness))} at observation</span></div>` +
        warnings.map((w) => `<div class="notice warn small">${esc(w)} <a href="#/import">Import a newer statement</a>.</div>`).join("") +
        `<dl class="kv"><dt>Available</dt><dd class="u-left amount">${money(a.available_minor, a.currency)}</dd><dt>Current</dt><dd>${money(a.current_minor, a.currency)}</dd>` +
        `<dt>Observed</dt><dd>${esc(fmtDateTime(a.balance_observed_at))} (${esc(ago(a.balance_observed_at))})</dd>` +
        `<dt>Institution</dt><dd>${esc(a.institution_name || "—")}${a.last_four ? " · •••• " + esc(a.last_four) : ""}</dd><dt>Type</dt><dd>${esc(title(a.account_type))}</dd>` +
        `<dt>Source</dt><dd>${esc(a.source_id)} v${esc(a.source_version)} (${esc((a.source_kind || "").toUpperCase())}), imported ${esc(fmtDateTime(a.source_imported_at))}</dd>` +
        `<dt>Safe-to-spend</dt><dd>${a.include_in_safe_to_spend === 1 ? "Included" : "Not included"}</dd></dl>` +
        (observations.length ? `<h3 class="u-mt-l">Observed balances</h3><div class="table-wrap"><table class="data"><thead><tr><th>Observed</th><th>Status</th><th class="amount">Available</th></tr></thead><tbody>${observations.map((b) => `<tr><td>${esc(fmtDateTime(b.observed_at))}</td><td>${esc(title(b.status))}</td><td class="amount">${money(b.available_minor, a.currency)}</td></tr>`).join("")}</tbody></table></div>` : '<p class="u-mt-m muted small">No balance observations yet: this account came from a statement without balances.</p>') +
        "</section>";
    }).join("");
    const imports = s.import_batches.map((b) => `<tr><td>${esc(fmtDateTime(b.completed_at))}</td><td>${esc(b.connector_id)}</td><td class="small mono">${esc(b.batch_id.slice(0, 30))}…</td></tr>`).join("");
    return pageHead("Accounts") + `<div class="u-grid-320 grid">${cards}</div>` +
      `<section class="u-mt-xl card"><h2>Import history</h2><p class="small muted">Balances are observations from your files, not live bank data. A balance older than ${s.stale_after_days} days is flagged by this interface; the status in each card is what the source reported.</p><div class="table-wrap"><table class="data"><thead><tr><th>Imported</th><th>Format</th><th>Batch</th></tr></thead><tbody>${imports}</tbody></table></div></section>`;
  }

  function renderPlan() {
    const s = state.snapshot;
    const currency = currencyOf();
    const protectedGoals = s.goals.filter((g) => g.protected === 1);
    const otherGoals = s.goals.filter((g) => g.protected !== 1);
    const goalCards = protectedGoals.map((g) => `<div class="card card-soft">${jarBlock(g.current_reserved_minor, g.target_minor, g.name, { size: 64 })}<p class="u-mt-s small muted">Planned contribution ${money(g.planned_contribution_minor, g.currency)} by ${esc(fmtDate(g.target_date, true))} — subtracted from safe-to-spend when due in the period.</p></div>`).join("");
    const budgets = s.budgets.map((b) => `<h3 class="u-mt-s">${esc(b.name)} <span class="small muted">(${esc(title(b.mode))}, from ${esc(fmtDate(b.starts_at, true))})</span></h3><div class="table-wrap"><table class="data"><thead><tr><th>Category</th><th class="amount">Limit</th><th class="amount">Spent</th><th class="amount">Pending</th><th class="amount">Remaining</th></tr></thead><tbody>${b.allocations.map((a) => `<tr><td>${esc(title(a.category_id))}</td><td class="amount">${money(a.limit_minor, currency)}</td><td class="amount">${money(a.spent_minor, currency)}</td><td class="amount">${money(a.pending_minor, currency)}</td><td class="amount">${money(a.remaining_minor, currency)}</td></tr>`).join("")}</tbody></table></div>`).join("");
    const commitments = s.commitments.map((c) => { const m = c.amount_model || {}; return `<tr><td>${esc(c.name)}</td><td>${esc(title(c.cadence))}</td><td>${esc(fmtDate(c.next_expected_at, true))}</td><td>${confidencePill(c.confidence)}</td><td class="amount">${m.primary ? money(m.primary.minor_units, m.primary.currency) : "—"}${m.kind === "range" && m.minimum ? `<div class="row-sub">${money(m.minimum.minor_units, currency)}–${money(m.maximum.minor_units, currency)}</div>` : ""}</td></tr>`; }).join("");
    const whatIfDefault = new Date(Date.now() + 7 * 86400000).toISOString().slice(0, 10);
    return pageHead("Plan") +
      '<div class="notice small">Plans are read-only in this version: Bundle\'s backend has no editing API for budgets, goals or commitments yet. They arrive with a bundle-finance-1 plan or backup. Your protected cash floor is set on the Overview.</div>' +
      `<section class="card"><h2 class="u-mb-m">Protected savings goals</h2>${goalCards ? `<div class="u-grid-280 grid">${goalCards}</div>` : `<p class="muted">No protected goals yet. Protected goals are kept separate from spendable money.</p>`}` +
      (otherGoals.length ? `<h3 class="u-mt-l">Other goals (not protected, not subtracted)</h3><ul class="rows">${otherGoals.map((g) => `<li><span class="row-icon">${jarSvg(stageFor(g.target_minor ? g.current_reserved_minor / g.target_minor : 0), { size: 32, decorative: true })}</span><div><div class="row-title">${esc(g.name)}</div><div class="row-sub">${money(g.current_reserved_minor, g.currency)} of ${money(g.target_minor, g.currency)}</div></div><span></span><span></span></li>`).join("")}</ul>` : "") + "</section>" +
      `<section class="u-mt-xl card" aria-labelledby="whatif-title"><h2 id="whatif-title">What if I spend…</h2><p class="small muted">Tests a one-off purchase against your plan in the ${MODES[settings().mode].label} view. Nothing is saved.</p>` +
      `<form id="whatif-form" class="filters"><label class="field">Amount<input type="text" name="amount" inputmode="decimal" required placeholder="250.00"></label><label class="field">Date<input type="date" name="date" value="${whatIfDefault}" required></label><button class="button primary" type="submit">Check</button></form><div id="whatif-result" aria-live="polite"></div></section>` +
      `<section class="u-mt-xl card"><div class="card-head"><h2>Forecast</h2><span class="small muted">Next ${settings().horizon} days · ${MODES[settings().mode].label} view</span></div>${state.calc ? chartBlock(state.calc.forecast, currency) : state.calcError ? errorNotice(state.calcError) : '<p class="muted">Import account data to see a forecast.</p>'}</section>` +
      `<section class="u-mt-xl card"><h2>Commitments</h2>${commitments ? `<p class="small muted">Verified and supported amounts are observed or confirmed; inferred and estimated ones are Bundle's guesses from patterns or ranges.</p><div class="table-wrap"><table class="data"><thead><tr><th>Name</th><th>Cadence</th><th>Next</th><th>Confidence</th><th class="amount">Amount</th></tr></thead><tbody>${commitments}</tbody></table></div>` : '<p class="muted">No commitments yet.</p>'}</section>` +
      `<section class="u-mt-xl card"><h2>Budgets</h2>${budgets ? `<p class="small muted">Shown as recorded in the plan (not recalculated from activity here).</p>${budgets}` : '<p class="muted">No budgets yet.</p>'}</section>`;
  }

  function renderImport() {
    const s = state.snapshot;
    const w = state.workspace;
    const p = state.preview;
    let preview = "";
    if (p && p.error) {
      preview = errorNotice(p.error, "Try again", "retry-preview");
    } else if (p) {
      const status = p.already_imported
        ? '<div class="notice ok"><h3>Already imported</h3><p>This exact statement is already in Bundle. Importing it again changes nothing, so nothing will be counted twice.</p></div>'
        : `<div class="notice ok"><h3>Ready to import</h3><p>Checked by a full trial import that was rolled back: ${p.new_events} new record${p.new_events === 1 ? "" : "s"} would be added.</p></div>`;
      const overlap = p.skipped_known ? `<p class="small muted">${p.skipped_known} row${p.skipped_known === 1 ? " is" : "s are"} already in Bundle unchanged and will be left out (statements often overlap).</p>` : "";
      preview = `<section class="u-mt-xl card" aria-labelledby="preview-title"><div class="card-head"><h2 id="preview-title">Preview: ${esc(p.filename)}</h2><span class="pill info">${esc(p.format.toUpperCase())}</span></div>${status}${overlap}` +
        `<dl class="kv"><dt>Accounts</dt><dd>${p.accounts.length ? p.accounts.map((a) => esc(`${a.display_name || a.account_id} (${a.currency})`)).join(", ") : "Existing accounts"}</dd><dt>Transactions</dt><dd>${p.transaction_count}${p.first_date ? ` from ${esc(fmtDate(p.first_date, true))} to ${esc(fmtDate(p.last_date, true))}` : ""}</dd><dt>Balances</dt><dd>${p.balance_count}</dd><dt>Money out / in</dt><dd>${money(p.debits_minor)} / ${money(p.credits_minor)}</dd><dt>Source</dt><dd class="mono">${esc(p.source_id)}${p.source_version ? ` · version ${p.source_version}` : ""}</dd></dl>` +
        (p.sample.length ? `<div class="u-mt-m table-wrap"><table class="data"><thead><tr><th>Date</th><th>Description</th><th>State</th><th class="amount">Amount</th></tr></thead><tbody>${p.sample.map((t) => `<tr><td>${esc(fmtDate(t.date, true))}</td><td>${esc(t.description)}</td><td>${esc(title(t.state))}</td><td class="amount">${money(t.direction === "credit" ? t.amount_minor : -t.amount_minor, t.currency, { sign: true })}</td></tr>`).join("")}</tbody></table></div>` : "") +
        `<div class="u-mt-l actions"><button type="button" class="button primary" data-action="commit-import" ${p.committed && !p.retry ? "" : ""}>${p.already_imported ? "Import anyway (no change)" : "Import these records"}</button><button type="button" class="button" data-action="cancel-preview">Choose another file</button></div><div id="commit-result" aria-live="polite">${p.result || ""}</div></section>`;
    }
    const backups = (w.backups || []).map((b) => `<tr><td>${esc(b.name)}</td><td>${esc(fmtDateTime(b.created_at))}</td><td><div class="actions"><button type="button" class="button small" data-verify="${esc(b.path)}">Verify</button><button type="button" class="button small" data-restore="${esc(b.path)}">Restore</button></div></td></tr>`).join("");
    const blockers = s.export_blockers.length ? `<div class="notice warn small">Export is blocked because these tables hold records the backup format cannot carry yet: ${esc(s.export_blockers.join(", "))}.</div>` : "";
    return pageHead("Import & backup") +
      `<section class="card" aria-labelledby="import-title"><h2 id="import-title">Import a statement</h2><p class="small muted">Supported: Bundle CSV (.csv), OFX (.ofx), QFX (.qfx) and bundle-connector-1 batches (.json). Bundle previews every file with a trial import first; nothing is saved until you confirm. <a href="/sample/statement.csv" download>Download a synthetic sample CSV</a>.</p>` +
      `<form id="import-form" class="filters"><label class="field">Statement file<input type="file" name="file" accept=".csv,.ofx,.qfx,.json" required></label>` +
      `<label class="field">Source ID <span class="hint">Optional. New accounts default to a fingerprint of the file; later statements continue their account's source.</span><input type="text" name="source_id"></label>` +
      `<label class="field">Statement time zone <span class="hint">OFX/QFX only, if times have no offset (e.g. -05:00)</span><input type="text" name="utc_offset" placeholder="-05:00"></label>` +
      `<button class="button primary" type="submit">Preview</button></form><div id="import-busy" aria-live="polite"></div></section>${preview}` +
      `<section class="u-mt-xl card" aria-labelledby="backup-title"><h2 id="backup-title">Backups</h2><p class="small muted">A backup is a bundle-finance-1 folder with a manifest of SHA-256 digests. Restoring is all-or-nothing and safe to repeat; a damaged or conflicting backup is refused without changing anything. Backups are not encrypted.</p>${blockers}` +
      `<div class="actions"><button type="button" class="button primary" data-action="export" ${s.export_blockers.length || !s.accounts.length ? "disabled" : ""}>Export a backup now</button>${!s.accounts.length ? '<span class="small muted">Import a statement first; there is nothing to back up yet.</span>' : ""}</div><div id="backup-result" aria-live="polite">${state.lastAction?.backup || ""}</div>` +
      `<h3 class="u-mt-l">Saved backups <span class="small muted">in ${esc(w.backups_directory)}</span></h3>${backups ? `<div class="table-wrap"><table class="data"><thead><tr><th>Folder</th><th>Created</th><th>Actions</th></tr></thead><tbody>${backups}</tbody></table></div>` : '<p class="muted">No backups yet.</p>'}` +
      `<form id="restore-form" class="u-mt-l filters"><label class="field">Restore from another folder<input type="text" name="path" class="u-wide" placeholder="Path to a bundle-finance-1 folder" required></label><button class="button" type="submit">Check and restore</button></form></section>`;
  }

  function guideSteps(compact = false) {
    const s = state.snapshot, w = state.workspace;
    const steps = [
      ["Import a statement", "Bring in a CSV, OFX/QFX or batch file.", s.import_batches.length > 0, "#/import"],
      ["Review your records", "Check transactions, sources and states in Activity.", recall("reviewed", false) && s.transactions.length > 0, "#/activity"],
      ["Set your protected cash floor", "The amount Bundle never counts as spendable.", !!state.savedFloor, "#/overview"],
      ["Understand safe-to-spend", "See how each figure is calculated.", recall("explained", false), "#/help"],
      ["Export a backup", "Keep a verified copy of your data.", (w.backups || []).length > 0, "#/import"],
    ];
    const list = (compact ? steps.slice(0, 3) : steps);
    return `<ol class="steps">${list.map(([t, d, done, href], i) => `<li class="${done ? "done" : ""}"><span class="step-num" aria-hidden="true">${done ? "✓" : i + 1}</span><div><a href="${href}" class="row-title">${esc(t)}</a><div class="row-sub">${esc(d)}</div></div><span class="small ${done ? "" : "muted"}">${done ? "Done" : "To do"}</span></li>`).join("")}</ol>`;
  }

  function renderGuide() {
    return pageHead("Getting started") + `<section class="card"><p>Five steps to a working local setup. Progress is read from your actual data.</p>${guideSteps()}` +
      `<h3 class="u-mt-xl">Reopening your data</h3><p>Everything lives in <span class="mono">${esc(state.workspace.user_database)}</span>. Stop Bundle with Ctrl+C and start it again with the same <span class="mono">--db</span> path; your records, plan and saved floor are still there.</p>` +
      `<div class="u-mt-l actions">${state.workspace.demo ? '<button type="button" class="button" data-action="exit-demo">Leave the demo</button>' : '<button type="button" class="button demo" data-action="start-demo">Try the synthetic demo</button>'}</div></section>`;
  }

  const HELP = {
    overview: "<p><strong>Safe to spend</strong> is the lowest balance Bundle projects for the period, minus your protected cash floor. Bundle starts from observed balances, subtracts commitments and protected-goal contributions that fall in the period, adds expected income, and finds the lowest point.</p><h3>Views</h3><p><strong>Confirmed</strong> counts only verified commitments. <strong>Expected</strong> also counts supported and inferred ones and protected goals. <strong>Stress</strong> counts everything except unknown or stale items and uses the top of any range.</p><h3>Saving a calculation</h3><p>“Save this calculation” records an append-only receipt. Your floor is saved the same way and travels with backups.</p>",
    calculation: "<p>Safe to spend = lowest projected balance in the period − protected cash floor.</p><p>The projection is a day-by-day running balance: it starts at the available balance of included accounts (observed in your imports) and applies each commitment and protected-goal contribution on its date. Using the lowest point means money needed later in the period is never offered as spendable today.</p><p>Observed figures come from your files. Commitments marked inferred or estimated are Bundle's guesses; the view you choose decides which are counted. The forecast is an estimate, not a promise.</p>",
    activity: "<p>Every transaction shows its source file type, import date and state (posted, pending, reversed, removed). <strong>Transfers</strong> between your accounts and <strong>refunds</strong> are matched by Bundle's reconciliation engine and are not counted as spending. Matching needs posted or authorized times, which CSV and OFX statements do not carry.</p>",
    accounts: "<p>Balances are observations from the files you import, with the time they were observed. Bundle never connects to a bank. A balance older than the stale limit, or marked stale by its source, is flagged for review: import a newer statement to refresh it.</p>",
    plan: "<p>Protected savings goals are kept apart from spendable money: their planned contributions are subtracted from safe-to-spend. The seed jar shows progress toward protected goals only; spending never changes it.</p><p>What-if checks a one-off purchase against the plan without saving anything.</p>",
    import: "<p><strong>CSV</strong> needs the columns account_id, account_name, account_type, currency, transaction_id, amount_minor, direction, state, description, observed_at (with a time-zone offset). Amounts are whole minor units (cents).</p><p>Every file is previewed with a trial import that is rolled back. Importing the same file twice changes nothing. A later statement for the same accounts continues their source as a new version; rows already stored unchanged are left out. Bundle cannot yet apply corrections (for example a pending purchase that later posted with a different amount) from a statement file: such a file is refused. If a file is refused, nothing is written; fix the file and preview again.</p><p><strong>Backups</strong> contain your accounts, transactions, balances, plan and calculation receipts, with SHA-256 digests. Restoring checks the digests first and is all-or-nothing.</p>",
    guide: "<p>Follow the steps in order. The demo is synthetic and kept in a separate database; leaving it returns you to your own data unchanged.</p>",
    help: "<p>Use the ? button on any page for help about that page.</p>",
    settings: "<p>Bundle runs only on this computer. The database is a plain SQLite file and is not encrypted, so protect it like any other private file.</p>",
  };

  function renderHelp() {
    remember("explained", true);
    return pageHead("Help") + `<section class="card">` + Object.entries({ overview: "Safe to spend", calculation: "How it's calculated", activity: "Activity", accounts: "Accounts and freshness", plan: "Plan and savings jar", import: "Import and backup", settings: "Privacy and storage" }).map(([k, t]) => `<h2 class="u-subhead">${esc(t)}</h2>${HELP[k]}`).join("") +
      `<h2 class="u-subhead">What Bundle does not do</h2><p>No bank linking, live synchronization, payments, or encryption. Balances are only as current as your last import.</p></section>`;
  }

  function renderSettings() {
    const w = state.workspace;
    return pageHead("Settings") + `<section class="card"><h2>Workspace</h2><dl class="u-mt-m kv"><dt>Active database</dt><dd class="mono">${esc(w.database)}</dd><dt>Your database</dt><dd class="mono">${esc(w.user_database)}</dd><dt>Backups folder</dt><dd class="mono">${esc(w.backups_directory)}</dd><dt>Engine</dt><dd class="mono">${esc(w.native_cli)}</dd><dt>Showing</dt><dd>${w.demo ? "Synthetic demo data" : "Your data"}</dd></dl>` +
      `<div class="u-mt-l actions">${w.demo ? '<button type="button" class="button primary" data-action="exit-demo">Back to my data</button><button type="button" class="button demo" data-action="start-demo">Reset the demo</button>' : '<button type="button" class="button demo" data-action="start-demo">Open the synthetic demo</button>'}</div>` +
      `<h2 class="u-mt-xl">Protected cash floor</h2>${floorForm()}<h2 class="u-mt-xl">Privacy</h2><p>Bundle runs on this computer only and answers only on its local address. The database is not encrypted.</p></section>`;
  }

  function floorForm() {
    const current = state.savedFloor;
    const currency = currencyOf();
    const disabled = !state.snapshot.accounts.length;
    return `<p class="small muted">The amount Bundle never counts as spendable. Saved as part of a calculation receipt, so it is kept in backups.${current ? ` Current: <strong>${money(current.floor_minor, currency)}</strong>, saved ${esc(fmtDateTime(current.calculated_at))}.` : " Not set yet (treated as zero)."}</p>` +
      `<form id="floor-form" class="filters"><label class="field">Floor<input type="text" name="floor" inputmode="decimal" value="${current ? (current.floor_minor / 10 ** (EXPONENT[currency] ?? 2)).toFixed(EXPONENT[currency] ?? 2) : ""}" placeholder="500.00" ${disabled ? "disabled" : ""}></label><button class="button primary" type="submit" ${disabled ? "disabled" : ""}>Save floor</button></form>${disabled ? '<p class="small muted">Import an account first.</p>' : ""}<div id="floor-result" aria-live="polite"></div>`;
  }

  function renderStatus() {
    const s = state.snapshot, w = state.workspace;
    const last = s.import_batches[0];
    const oldest = s.accounts.reduce((acc, a) => (!acc || a.balance_observed_at < acc ? a.balance_observed_at : acc), null);
    document.getElementById("statusbar").innerHTML =
      `<span>${icon("card")} Local workspace · ${esc(w.database.split(/[\\/]/).pop())}</span>` +
      `<span>Last import: ${last ? esc(fmtDateTime(last.completed_at)) : "none yet"}</span>` +
      (oldest ? `<span>Oldest balance observed ${esc(ago(oldest))}</span><a href="#/accounts">Review data freshness</a>` : "") +
      `<span class="spacer"></span>${w.demo ? '<span class="pill demo">Demo data — amounts are fictional</span>' : ""}`;
  }

  // -- routing and events ---------------------------------------------------------------
  const PAGES = { overview: renderOverview, activity: renderActivity, accounts: renderAccounts, plan: renderPlan, import: renderImport, guide: renderGuide, help: renderHelp, settings: renderSettings };
  const route = () => (location.hash.replace(/^#\//, "") || "overview").split("?")[0];

  function render() {
    const page = PAGES[route()] ? route() : "overview";
    document.querySelectorAll("[data-route]").forEach((a) => { if (a.dataset.route === page) a.setAttribute("aria-current", "page"); else a.removeAttribute("aria-current"); });
    const main = document.getElementById("main");
    main.innerHTML = PAGES[page]();
    if (page === "overview" && state.snapshot.accounts.length) {
      const floorCard = document.createElement("section");
      floorCard.className = "card";
      floorCard.setAttribute("aria-labelledby", "floor-title");
      floorCard.innerHTML = `<h2 id="floor-title">Protected cash floor</h2>${floorForm()}`;
      const jarCard = main.querySelector('[aria-labelledby="jar-title"]');
      if (jarCard) jarCard.before(floorCard);
    }
    renderStatus();
    document.title = `${main.querySelector("h1")?.textContent || "Bundle"} — Bundle by The Hidden Canopy`;
  }

  async function reloadAndRender(focusMain = false) {
    try {
      await refresh();
      document.getElementById("banner").innerHTML = "";
    } catch (error) {
      document.getElementById("banner").innerHTML = errorNotice(error, "Try again", "retry-all");
      return;
    }
    render();
    if (focusMain) document.getElementById("main").focus();
  }

  function confirmDialog(titleText, body, ok = "Continue") {
    const dialog = document.getElementById("confirm-dialog");
    document.getElementById("confirm-title").textContent = titleText;
    document.getElementById("confirm-body").textContent = body;
    document.getElementById("confirm-ok").textContent = ok;
    return new Promise((resolve) => {
      const done = (value) => { dialog.close(); resolve(value); };
      document.getElementById("confirm-ok").onclick = () => done(true);
      document.getElementById("confirm-cancel").onclick = () => done(false);
      dialog.oncancel = () => resolve(false);
      dialog.showModal();
    });
  }

  function busy(target, text) {
    const el = document.getElementById(target);
    if (el) el.innerHTML = `<p class="small"><span class="spinner" aria-hidden="true"></span> ${esc(text)}</p>`;
  }

  async function act(button, fn) {
    if (button) button.disabled = true;
    try { await fn(); } finally { if (button && document.body.contains(button)) button.disabled = false; }
  }

  document.addEventListener("click", async (event) => {
    const target = event.target.closest("button, a");
    if (!target) return;
    if (target.matches("[data-help]") || target.matches("[data-help-topic]")) {
      const topic = target.dataset.helpTopic || route();
      if (topic === "calculation") remember("explained", true);
      document.getElementById("help-title").textContent = topic === "calculation" ? "How safe-to-spend is calculated" : "Help";
      document.getElementById("help-body").innerHTML = HELP[topic] || HELP.help;
      document.getElementById("help-dialog").showModal();
      return;
    }
    if (target.matches("[data-close-dialog]")) { target.closest("dialog").close(); return; }
    if (target.id === "retry-all") { reloadAndRender(); return; }
    if (target.id === "retry-calc") { await loadCalc(); render(); return; }
    if (target.id === "retry-preview") { state.preview = null; render(); document.querySelector("#import-form input[type=file]")?.focus(); return; }
    const action = target.dataset.action;
    if (action === "start-demo") {
      await act(target, async () => {
        busy("banner", "Building the synthetic demo…");
        try { await api("/api/demo/start", { method: "POST", body: {} }); location.hash = "#/overview"; await reloadAndRender(true); }
        catch (error) { document.getElementById("banner").innerHTML = errorNotice(error); }
      });
    } else if (action === "exit-demo") {
      await act(target, async () => { await api("/api/demo/exit", { method: "POST", body: {} }); location.hash = "#/overview"; await reloadAndRender(true); });
    } else if (action === "record-calc") {
      await act(target, async () => {
        const { mode, horizon } = settings();
        try {
          const result = await api("/api/calculate", { method: "POST", body: { mode, horizon_days: horizon, floor_minor: state.savedFloor ? state.savedFloor.floor_minor : 0, record: true } });
          await loadState();
          const el = document.getElementById("record-result");
          if (el) el.innerHTML = `<div class="notice ok small">${result.recorded ? "Saved" : "Already saved earlier"} as receipt <span class="mono">${esc(result.receipt.receipt_id)}</span>. It is included in backups.</div>`;
        } catch (error) { const el = document.getElementById("record-result"); if (el) el.innerHTML = errorNotice(error); }
      });
    } else if (action === "commit-import") {
      await act(target, async () => {
        busy("commit-result", "Importing…");
        try {
          const result = await api("/api/import/commit", { method: "POST", body: { preview_id: state.preview.preview_id } });
          state.preview.result = `<div class="notice ok"><h3>${result.changed ? "Imported" : "Nothing changed"}</h3><p>${result.changed ? "The records are saved." : "This statement was already imported, so nothing was counted twice."} <a href="#/activity">Review them in Activity</a>.</p></div>`;
          await refresh();
          state.preview.already_imported = true;
          render();
          document.getElementById("commit-result")?.focus();
        } catch (error) {
          const el = document.getElementById("commit-result");
          if (el) el.innerHTML = errorNotice(error, "Retry import", "retry-commit");
        }
      });
    } else if (target.id === "retry-commit") {
      document.querySelector('[data-action="commit-import"]')?.click();
    } else if (action === "cancel-preview") {
      state.preview = null; render();
    } else if (action === "export") {
      await act(target, async () => {
        busy("backup-result", "Writing and verifying the backup…");
        try {
          const result = await api("/api/backup/export", { method: "POST", body: {} });
          state.lastAction = { backup: `<div class="notice ok"><h3>Backup saved and verified</h3><p class="mono small">${esc(result.path)}</p><p class="small">${esc(result.verification.message)}</p></div>` };
          await loadState(); render();
        } catch (error) { state.lastAction = { backup: errorNotice(error) }; render(); }
      });
    } else if (target.dataset.verify) {
      await act(target, async () => {
        busy("backup-result", "Verifying…");
        try { const r = await api("/api/backup/verify", { method: "POST", body: { path: target.dataset.verify } }); document.getElementById("backup-result").innerHTML = `<div class="notice ok small">${esc(r.message)}</div>`; }
        catch (error) { document.getElementById("backup-result").innerHTML = errorNotice(error); }
      });
    } else if (target.dataset.restore) {
      restore(target.dataset.restore, target);
    }
  });

  async function restore(path, button) {
    const ok = await confirmDialog("Restore this backup?", `Bundle will check ${path} and then add its records to the current workspace. Records already present must match exactly; if anything conflicts, nothing is changed.`, "Restore");
    if (!ok) return;
    await act(button, async () => {
      busy("backup-result", "Checking and restoring…");
      try {
        const result = await api("/api/backup/restore", { method: "POST", body: { path } });
        await refresh();
        state.lastAction = { backup: `<div class="notice ok"><h3>Restored</h3><p class="small">${esc(result.message)}</p></div>` };
        render();
      } catch (error) { state.lastAction = { backup: errorNotice(error) }; render(); }
    });
  }

  document.addEventListener("change", async (event) => {
    const t = event.target;
    if (t.name === "mode") { remember("mode", t.value); await loadCalc(); render(); document.getElementById(`mode-${t.value}`)?.focus(); }
    else if (t.id === "horizon") { remember("horizon", parseInt(t.value, 10)); await loadCalc(); render(); document.getElementById("horizon")?.focus(); }
    else if (t.closest("#activity-filters") && t.name !== "q") { saveActivityFilter(); }
  });
  document.addEventListener("input", (event) => {
    if (event.target.closest("#activity-filters") && event.target.name === "q") {
      clearTimeout(window.__bundleSearch);
      window.__bundleSearch = setTimeout(() => { saveActivityFilter(); const q = document.querySelector('#activity-filters [name="q"]'); if (q) { q.focus(); q.setSelectionRange(q.value.length, q.value.length); } }, 250);
    }
  });
  function saveActivityFilter() {
    const form = document.getElementById("activity-filters");
    remember("activity-filter", { account: form.account.value, kind: form.kind.value, q: form.q.value });
    render();
  }

  document.addEventListener("submit", async (event) => {
    const form = event.target;
    event.preventDefault();
    const button = form.querySelector('button[type="submit"]');
    if (form.id === "import-form") {
      const file = form.file.files[0];
      if (!file) return;
      await act(button, async () => {
        busy("import-busy", `Checking ${file.name} with a trial import…`);
        try {
          const content = await file.text();
          state.preview = await api("/api/import/preview", { method: "POST", body: { filename: file.name, content, source_id: form.source_id.value, utc_offset: form.utc_offset.value } });
        } catch (error) {
          state.preview = { error };
        }
        render();
        document.getElementById("preview-title")?.focus();
      });
    } else if (form.id === "floor-form") {
      const minor = toMinor(form.floor.value, currencyOf());
      const out = document.getElementById("floor-result");
      if (minor === null) { out.innerHTML = '<div class="notice error small" role="alert">Enter an amount such as 500.00.</div>'; return; }
      await act(button, async () => {
        const { mode, horizon } = settings();
        try {
          await api("/api/calculate", { method: "POST", body: { mode, horizon_days: horizon, floor_minor: minor, record: true } });
          await refresh(); render();
          const el = document.getElementById("floor-result");
          if (el) el.innerHTML = `<div class="notice ok small">Floor saved: ${money(minor, currencyOf())}.</div>`;
        } catch (error) { out.innerHTML = errorNotice(error); }
      });
    } else if (form.id === "whatif-form") {
      const out = document.getElementById("whatif-result");
      const minor = toMinor(form.amount.value, currencyOf());
      if (!minor) { out.innerHTML = '<div class="notice error small" role="alert">Enter an amount greater than zero.</div>'; return; }
      await act(button, async () => {
        const { mode, horizon } = settings();
        try {
          const r = await api("/api/calculate", { method: "POST", body: { mode, horizon_days: horizon, floor_minor: state.savedFloor ? state.savedFloor.floor_minor : 0, what_if: { amount_minor: minor, date: `${form.date.value}T12:00:00Z` } } });
          const w = r.what_if, c = currencyOf();
          out.innerHTML = `<table class="u-narrow u-mt-s breakdown"><tbody><tr><th>Safe to spend now</th><td>${money(w.safe_to_spend_before_minor, c)}</td></tr><tr><th>After this purchase</th><td>${money(w.safe_to_spend_after_minor, c)}</td></tr><tr><th>Lowest balance before / after</th><td>${money(w.minimum_balance_before_minor, c)} / ${money(w.minimum_balance_after_minor, c)}</td></tr></tbody></table>` +
            (w.safe_to_spend_after_minor < 0 ? '<p class="notice warn small">This would dip below your protected floor during the period. You can still decide; this is information, not a rule.</p>' : '<p class="small muted">Your plan still covers commitments and the protected floor.</p>') +
            (w.warnings || []).map((x) => `<p class="small">${esc(x)}</p>`).join("");
        } catch (error) { out.innerHTML = errorNotice(error); }
      });
    } else if (form.id === "restore-form") {
      restore(form.path.value.trim(), button);
    }
  });

  window.addEventListener("hashchange", () => { render(); document.getElementById("main").focus(); });
  document.querySelector("[data-jar=logo]").innerHTML = jarSvg(0, { size: 46, decorative: true });
  document.querySelectorAll("[data-icon]").forEach((el) => { el.outerHTML = icon(el.dataset.icon); });
  reloadAndRender();
})();
