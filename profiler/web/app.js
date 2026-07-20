const state = { tabs: [], trace: null, nodes: new Map(), matches: [], match: -1, searchVersion: 0 };
const $ = (selector) => document.querySelector(selector);

function ms(value) {
  if (value === null || value === undefined) return "--";
  return value >= 1000 ? `${(value / 1000).toFixed(2)}s` : `${value}ms`;
}

function durationClass(value) {
  if (value === null || value === undefined) return "";
  if (value < 10) return "time-fast";
  if (value < 100) return "time-warm";
  if (value < 1000) return "time-slow";
  return "time-hot";
}

function buildSources() {
  const nav = $("#source-list");
  nav.replaceChildren();
  const grouped = new Map();
  for (const tab of state.tabs) {
    if (!grouped.has(tab.hostname)) grouped.set(tab.hostname, new Map());
    const kinds = grouped.get(tab.hostname);
    if (!kinds.has(tab.kind)) kinds.set(tab.kind, []);
    kinds.get(tab.kind).push(tab);
  }
  for (const [host, kinds] of grouped) {
    const hostLabel = document.createElement("div"); hostLabel.className = "source-host"; hostLabel.textContent = host; nav.append(hostLabel);
    for (const [kind, tabs] of kinds) {
      const kindLabel = document.createElement("div"); kindLabel.className = "source-kind"; kindLabel.textContent = kind; nav.append(kindLabel);
      for (const tab of tabs) {
        const button = document.createElement("button"); button.className = "source-item btn"; button.dataset.id = tab.id;
        const title = document.createElement("span"); title.textContent = `thread ${tab.tid}`;
        const meta = document.createElement("small"); meta.textContent = `${tab.nodes.toLocaleString()} nodes`;
        button.append(title, meta); button.addEventListener("click", () => loadTrace(tab.id)); nav.append(button);
      }
    }
  }
  $("#source-count").textContent = state.tabs.length;
}

function metric(label, value) {
  const item = document.createElement("div"); item.className = "metric";
  const small = document.createElement("small"); small.textContent = label;
  const strong = document.createElement("strong"); strong.textContent = value;
  item.append(small, strong); return item;
}

function registerNodes(nodes, parent = null) {
  for (const node of nodes) {
    node.parent = parent;
    node.children = null;
    state.nodes.set(node.id, node);
  }
}

async function loadTrace(id) {
  state.searchVersion += 1;
  document.querySelectorAll(".source-item").forEach(item => item.classList.toggle("active", Number(item.dataset.id) === id));
  $("#trace-title").textContent = "Loading trace...";
  const response = await fetch(`/api/trace?id=${id}`); state.trace = await response.json();
  const summary = state.trace.summary;
  state.nodes.clear(); registerNodes(state.trace.roots);
  $("#breadcrumb").textContent = `${summary.hostname} / ${summary.kind}`;
  $("#trace-title").textContent = `Thread ${summary.tid}`;
  $("#metrics").replaceChildren(metric("Trace nodes", summary.nodes.toLocaleString()), metric("Scopes", summary.scopes.toLocaleString()), metric("Events", summary.events.toLocaleString()), metric("Time window", ms(summary.window_ms)));
  $("#search").value = ""; state.matches = []; state.match = -1; $("#match-count").textContent = "";
  renderTree(); renderStats(); showEmptyInspector();
  $(".sidebar").classList.remove("open");
  $("#sidebar-backdrop").classList.remove("open");
}

function createNode(node) {
  const wrapper = document.createElement("div"); wrapper.className = `tree-node ${node.is_event ? "event" : `depth-${node.depth % 5}`}`; wrapper.dataset.nodeId = node.id;
  const row = document.createElement("div"); row.className = "node-row"; row.style.paddingLeft = `${12 + node.depth * 19}px`; row.setAttribute("role", "treeitem");
  const toggle = document.createElement(node.child_count ? "button" : "span");
  toggle.className = node.child_count ? "toggle" : "toggle-spacer";
  if (node.child_count) {
    toggle.type = "button";
    toggle.textContent = "+";
    toggle.setAttribute("aria-label", `Expand ${node.invoker}`);
    toggle.setAttribute("aria-expanded", "false");
  }
  const name = document.createElement("span"); name.className = "node-name"; name.textContent = node.invoker;
  const time = document.createElement("span"); time.className = `node-time ${durationClass(node.duration_ms)}`; time.textContent = node.is_event ? `@${ms(node.timestamp_ms)}` : ms(node.duration_ms);
  const args = document.createElement("span"); args.className = "node-args"; args.textContent = node.args ? `| ${node.args}` : "";
  row.append(toggle, name, time, args); wrapper.append(row);
  row.addEventListener("click", () => selectNode(node, row));
  if (node.child_count) {
    toggle.addEventListener("click", event => { event.stopPropagation(); toggleNode(wrapper, node); });
  }
  return wrapper;
}

async function expandNode(wrapper, node) {
  let children = wrapper.querySelector(":scope > .children");
  const toggle = wrapper.querySelector(":scope > .node-row > .toggle");
  if (node.children === null) {
    if (node.loading) return;
    node.loading = true;
    toggle.textContent = "...";
    toggle.classList.add("loading");
    try {
      const traceId = state.trace.summary.id;
      const response = await fetch(`/api/children?trace=${traceId}&node=${encodeURIComponent(node.id)}`);
      if (!response.ok) throw new Error("Unable to load child traces");
      const payload = await response.json();
      if (!state.trace || state.trace.summary.id !== traceId) return;
      node.children = payload.nodes;
      registerNodes(node.children, node);
    } catch (error) {
      toggle.textContent = "+";
      console.error(error);
      return;
    } finally {
      node.loading = false;
      toggle.classList.remove("loading");
    }
  }
  if (!children) {
    children = document.createElement("div"); children.className = "children";
    node.children.forEach(child => children.append(createNode(child)));
    wrapper.append(children);
  }
  children.hidden = false;
  toggle.textContent = "-"; toggle.setAttribute("aria-expanded", "true");
}
function collapseNode(wrapper) { const children = wrapper.querySelector(":scope > .children"); if (!children) return; children.hidden = true; const toggle = wrapper.querySelector(":scope > .node-row > .toggle"); toggle.textContent = "+"; toggle.setAttribute("aria-expanded", "false"); }
async function toggleNode(wrapper, node) { const children = wrapper.querySelector(":scope > .children"); if (!children || children.hidden) await expandNode(wrapper, node); else collapseNode(wrapper); }

function renderTree() { const tree = $("#tree"); tree.replaceChildren(); state.trace.roots.forEach(root => tree.append(createNode(root))); $("#empty").hidden = true; }

function selectNode(node, row) {
  document.querySelectorAll(".node-row.selected").forEach(item => item.classList.remove("selected")); row.classList.add("selected");
  const inspector = $("#inspector"); inspector.replaceChildren();
  const label = document.createElement("div"); label.className = "panel-label"; label.textContent = "INSPECTOR";
  const body = document.createElement("div"); body.className = "inspect-body";
  body.innerHTML = `<div class="inspect-kind"></div><h2></h2><div class="inspect-grid"></div><div class="inspect-args"></div>`;
  body.querySelector(".inspect-kind").textContent = node.is_event ? "EVENT" : `SCOPE / DEPTH ${node.depth}`; body.querySelector("h2").textContent = node.invoker;
  const values = [["Source", `${node.file}:${node.line}`], ["Duration", ms(node.duration_ms)], ["Timestamp", ms(node.timestamp_ms)], ["Enter", ms(node.enter_ms)], ["Exit", ms(node.exit_ms)], ["Children", node.child_count]];
  const grid = body.querySelector(".inspect-grid"); values.forEach(([key, value]) => { const cell = document.createElement("div"); const small = document.createElement("small"); small.textContent = key; const span = document.createElement("span"); span.textContent = value; cell.append(small, span); grid.append(cell); });
  body.querySelector(".inspect-args").textContent = node.args || "No arguments"; inspector.append(label, body);
}
function showEmptyInspector() { $("#inspector").innerHTML = '<div class="panel-label">INSPECTOR</div><div class="inspector-empty">Select a trace call to inspect its timing and source.</div>'; }

async function revealMatch() {
  if (!state.matches.length) return;
  const match = state.matches[state.match];
  const parts = match.id.split(".");
  for (let length = 1; length < parts.length; length += 1) {
    const ancestorId = parts.slice(0, length).join(".");
    const ancestor = state.nodes.get(ancestorId);
    const wrapper = document.querySelector(`[data-node-id="${ancestorId}"]`);
    if (!ancestor || !wrapper) return;
    await expandNode(wrapper, ancestor);
  }
  const node = state.nodes.get(match.id) || match;
  const wrapper = document.querySelector(`[data-node-id="${node.id}"]`); if (!wrapper) return;
  const row = wrapper.querySelector(":scope > .node-row"); selectNode(node, row); row.scrollIntoView({ block: "center", behavior: "smooth" });
  $("#match-count").textContent = `${state.match + 1}/${state.matches.length}`;
}
async function search() {
  const query = $("#search").value.trim();
  const version = ++state.searchVersion;
  if (!query) {
    state.matches = []; state.match = -1; $("#match-count").textContent = ""; $("#empty").hidden = true; return;
  }
  $("#match-count").textContent = "...";
  await new Promise(resolve => setTimeout(resolve, 180));
  if (version !== state.searchVersion) return;
  const traceId = state.trace.summary.id;
  const response = await fetch(`/api/search?trace=${traceId}&q=${encodeURIComponent(query)}`);
  if (version !== state.searchVersion || state.trace.summary.id !== traceId) return;
  const payload = await response.json();
  state.matches = payload.matches; state.match = state.matches.length ? 0 : -1;
  $("#match-count").textContent = query ? `0/${state.matches.length}${payload.limited ? "+" : ""}` : "";
  $("#empty").hidden = state.matches.length > 0;
  if (state.matches.length) await revealMatch();
}
async function stepMatch(direction) { if (!state.matches.length) return; state.match = (state.match + direction + state.matches.length) % state.matches.length; await revealMatch(); }

function renderStats() { const body = $("#stats-body"); body.replaceChildren(); for (const row of state.trace.stats) { const tr = document.createElement("tr"); [row.invoker, row.count, ms(row.total_ms), ms(row.mean_ms.toFixed(1)), ms(row.median_ms.toFixed(1)), ms(row.max_ms), ms(row.min_ms), ms(row.std_ms.toFixed(1))].forEach(value => { const td = document.createElement("td"); td.textContent = value; tr.append(td); }); body.append(tr); } }

$("#search").addEventListener("input", search); $("#search").addEventListener("keydown", event => { if (event.key === "Enter") stepMatch(event.shiftKey ? -1 : 1); });
$("#next-match").addEventListener("click", () => stepMatch(1)); $("#previous-match").addEventListener("click", () => stepMatch(-1));
$("#expand-all").addEventListener("click", async () => {
  const currentLevel = [...state.nodes.values()];
  await Promise.all(currentLevel.map(node => {
    const wrapper = document.querySelector(`[data-node-id="${node.id}"]`);
    return wrapper && node.child_count ? expandNode(wrapper, node) : null;
  }));
});
$("#collapse-all").addEventListener("click", () => document.querySelectorAll(".tree-node").forEach(collapseNode));
$("#show-stats").addEventListener("click", () => $("#stats-panel").hidden = false); $("#close-stats").addEventListener("click", () => $("#stats-panel").hidden = true);
function toggleSources(force) {
  const open = force ?? !$(".sidebar").classList.contains("open");
  $(".sidebar").classList.toggle("open", open);
  $("#sidebar-backdrop").classList.toggle("open", open);
}
$("#mobile-sources").addEventListener("click", () => toggleSources());
$("#sidebar-backdrop").addEventListener("click", () => toggleSources(false));
document.addEventListener("keydown", event => { if (event.key === "/" && document.activeElement !== $("#search")) { event.preventDefault(); $("#search").focus(); } if (event.key === "Escape") $("#stats-panel").hidden = true; });

fetch("/api/overview").then(response => response.json()).then(data => { state.tabs = data.tabs; buildSources(); if (state.tabs.length) loadTrace(state.tabs[0].id); });
