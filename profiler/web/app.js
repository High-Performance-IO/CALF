const workspace = { tabs: [], columns: [], nextColumnId: 1 };
const $ = (selector, root = document) => root.querySelector(selector);
const columnThemes = [
  ["#315e57", "#e7f0ee"], ["#35679a", "#e8f0f8"], ["#76539a", "#f0eaf6"],
  ["#a56324", "#f8eee3"], ["#a64458", "#f8e9ed"], ["#36775a", "#e7f3ec"],
];
let activeConnection = null;

function correlationOverlay() {
  let overlay = $("#correlation-overlay");
  if (overlay) return overlay;
  overlay = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  overlay.id = "correlation-overlay";
  overlay.innerHTML = '<defs><linearGradient id="correlation-gradient" gradientUnits="userSpaceOnUse"><stop offset="0%"></stop><stop offset="100%"></stop></linearGradient><marker id="correlation-arrowhead" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 Z"></path></marker></defs><path class="correlation-line"></path><path class="correlation-hit"></path>';
  const dismiss = event => {
    event.preventDefault(); event.stopPropagation(); hideCorrelationArrow();
  };
  $(".correlation-line", overlay).addEventListener("click", dismiss);
  $(".correlation-hit", overlay).addEventListener("click", dismiss);
  document.body.append(overlay);
  return overlay;
}

function hideCorrelationArrow() {
  activeConnection?.observer?.disconnect();
  activeConnection?.hiddenButtons?.forEach(button => button.hidden = false);
  document.querySelectorAll(".connection-close").forEach(button => button.remove());
  document.querySelectorAll(".correlation-jump:not(.connection-close)").forEach(button => button.hidden = false);
  activeConnection = null;
  const overlay = $("#correlation-overlay");
  if (overlay) overlay.remove();
}

function drawCorrelationArrow() {
  if (!activeConnection) return;
  const { sourceRow, targetRow } = activeConnection;
  if (!rowIsVisible(sourceRow) || !rowIsVisible(targetRow)) {
    hideCorrelationArrow(); return;
  }
  const source = sourceRow.getBoundingClientRect();
  const target = targetRow.getBoundingClientRect();
  const leftToRight = source.left < target.left;
  const x1 = leftToRight ? source.right : source.left;
  const x2 = leftToRight ? target.left : target.right;
  const y1 = source.top + source.height / 2;
  const y2 = target.top + target.height / 2;
  const bend = Math.max(50, Math.abs(x2 - x1) * 0.45);
  const direction = leftToRight ? 1 : -1;
  const path = `M ${x1} ${y1} C ${x1 + bend * direction} ${y1}, ${x2 - bend * direction} ${y2}, ${x2} ${y2}`;
  const overlay = correlationOverlay();
  const gradient = $("#correlation-gradient", overlay);
  gradient.setAttribute("x1", x1); gradient.setAttribute("y1", y1);
  gradient.setAttribute("x2", x2); gradient.setAttribute("y2", y2);
  const sourceColor = getComputedStyle(sourceRow.closest(".trace-column")).getPropertyValue("--accent").trim();
  const targetColor = getComputedStyle(targetRow.closest(".trace-column")).getPropertyValue("--accent").trim();
  $("stop:first-child", gradient).setAttribute("stop-color", sourceColor);
  $("stop:last-child", gradient).setAttribute("stop-color", targetColor);
  $("#correlation-arrowhead path", overlay).setAttribute("fill", targetColor);
  $(".correlation-line", overlay).setAttribute("d", path);
  $(".correlation-hit", overlay).setAttribute("d", path);
  overlay.hidden = false;
}

function rowIsVisible(row) {
  if (!row.isConnected || !row.offsetParent) return false;
  const rect = row.getBoundingClientRect();
  if (rect.bottom <= 0 || rect.top >= window.innerHeight || rect.right <= 0 || rect.left >= window.innerWidth) return false;
  const scroller = row.closest(".tree, .event-table-wrap");
  if (!scroller) return true;
  const clip = scroller.getBoundingClientRect();
  return rect.bottom > clip.top && rect.top < clip.bottom && rect.right > clip.left && rect.left < clip.right;
}

function addConnectionClose(row) {
  const host = row.matches("tr") ? row.lastElementChild : row;
  const button = document.createElement("button");
  button.className = "correlation-jump connection-close";
  button.textContent = "Close connection";
  button.addEventListener("click", event => {
    event.preventDefault(); event.stopPropagation(); hideCorrelationArrow();
  });
  host.append(button);
}

function showCorrelationArrow(sourceRow, targetRow, source, target, sourceTrace, targetTrace) {
  const observer = new IntersectionObserver(entries => {
    if (entries.some(entry => !entry.isIntersecting)) hideCorrelationArrow();
  }, { threshold: 0.01 });
  observer.observe(sourceRow); observer.observe(targetRow);
  const hiddenButtons = [
    ...source.root.querySelectorAll(`.correlation-jump[data-trace-id="${targetTrace}"]`),
    ...target.root.querySelectorAll(`.correlation-jump[data-trace-id="${sourceTrace}"]`),
  ];
  hiddenButtons.forEach(button => button.hidden = true);
  activeConnection = { sourceRow, targetRow, source, target, sourceTrace, targetTrace, observer, hiddenButtons };
  addConnectionClose(sourceRow); addConnectionClose(targetRow);
  requestAnimationFrame(() => requestAnimationFrame(drawCorrelationArrow));
}

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

function metric(label, value) {
  const item = document.createElement("div"); item.className = "metric";
  const small = document.createElement("small"); small.textContent = label;
  const strong = document.createElement("strong"); strong.textContent = value;
  item.append(small, strong); return item;
}

function createColumn(kind, initialTraceId = null, pendingNode = null) {
  const column = {
    id: workspace.nextColumnId++, kind, trace: null, nodes: new Map(), matches: [],
    match: -1, searchVersion: 0, loadVersion: 0, view: "tree", eventOffset: 0,
    eventTotal: 0, correlationVersion: 0, selectedNode: null, pendingNode,
    requestLinks: new Set(), root: document.createElement("section"),
  };
  column.root.className = "trace-column";
  const theme = columnThemes[(column.id - 1) % columnThemes.length];
  column.root.style.setProperty("--accent", theme[0]);
  column.root.style.setProperty("--accent-soft", theme[1]);
  column.root.innerHTML = `
    <header class="column-head">
      <label><span>Directory</span><select class="directory-select"></select></label>
      <button class="remove-column" title="Remove column" aria-label="Remove column">Remove</button>
    </header>
    <nav class="thread-tabs" aria-label="Thread logs"></nav>
    <div class="trace-summary"><div><div class="eyebrow trace-path">No trace selected</div><h1>Loading...</h1></div><div class="metrics"></div></div>
    <div class="toolbar">
      <div class="view-switch" aria-label="Trace view"><button class="show-tree active" aria-pressed="true">Tree</button><button class="show-events" aria-pressed="false">Events</button></div>
      <label class="search"><span>Search</span><input type="search" placeholder="Invoker, arguments, or source"><kbd class="match-count"></kbd></label>
      <button class="previous-match" title="Previous match">Prev</button><button class="next-match" title="Next match">Next</button>
      <div class="tree-actions"><button class="expand-all">Expand</button><button class="collapse-all">Collapse</button></div>
      <button class="show-insights">Analysis</button>
      <details class="export-menu"><summary>Export</summary><div><button data-format="json">JSON</button><button data-format="csv">CSV</button><button data-format="html">HTML report</button><button class="copy-link">Copy link</button></div></details>
    </div>
    <div class="content-grid">
      <div class="tree-panel"><div class="panel-label"><span>Call tree</span><span class="legend"><i class="event-dot"></i>event <i class="fast-dot"></i>fast <i class="hot-dot"></i>hot</span></div><div class="tree" role="tree"></div><div class="tree-empty empty" hidden>No trace nodes match this search.</div></div>
      <div class="events-panel" hidden><div class="panel-label"><span>Event log</span><span class="event-count">0 events</span></div><div class="event-table-wrap"><table class="event-table"><thead><tr><th>Time</th><th>Event</th><th>Details</th><th>Context</th><th>Source</th></tr></thead><tbody></tbody></table><div class="events-empty empty" hidden>No events match this search.</div></div><button class="load-events" hidden>Load more</button></div>
      <aside class="inspector"></aside>
    </div>`;

  const directories = [...new Set(workspace.tabs.map(tab => tab.kind))];
  const select = $(".directory-select", column.root);
  directories.forEach(directory => {
    const option = document.createElement("option"); option.value = directory; option.textContent = directory; select.append(option);
  });
  select.value = kind;
  select.addEventListener("change", () => setDirectory(column, select.value));
  $(".remove-column", column.root).addEventListener("click", () => removeColumn(column));
  $(".show-tree", column.root).addEventListener("click", () => setView(column, "tree"));
  $(".show-events", column.root).addEventListener("click", () => setView(column, "events"));
  $(".load-events", column.root).addEventListener("click", () => loadEvents(column));
  $(".previous-match", column.root).addEventListener("click", () => stepMatch(column, -1));
  $(".next-match", column.root).addEventListener("click", () => stepMatch(column, 1));
  $(".expand-all", column.root).addEventListener("click", () => expandLoadedLevel(column));
  $(".collapse-all", column.root).addEventListener("click", () => column.root.querySelectorAll(".tree-node").forEach(collapseNode));
  $(".show-insights", column.root).addEventListener("click", () => showInsights(column));
  column.root.querySelectorAll(".export-menu [data-format]").forEach(button => button.addEventListener("click", () => exportEvents(column, button.dataset.format)));
  $(".copy-link", column.root).addEventListener("click", event => copyLink(column, event.currentTarget));
  const searchInput = $(".search input", column.root);
  searchInput.addEventListener("input", () => search(column));
  searchInput.addEventListener("keydown", event => {
    if (event.key === "Enter" && column.view === "tree") stepMatch(column, event.shiftKey ? -1 : 1);
  });

  workspace.columns.push(column);
  $("#columns").append(column.root);
  updateRemoveButtons();
  column.ready = setDirectory(column, kind, initialTraceId);
  return column;
}

function removeColumn(column) {
  if (workspace.columns.length === 1) return;
  hideCorrelationArrow();
  workspace.columns = workspace.columns.filter(item => item !== column);
  column.root.remove();
  updateRemoveButtons();
}

function updateRemoveButtons() {
  const disabled = workspace.columns.length === 1;
  workspace.columns.forEach(column => $(".remove-column", column.root).hidden = disabled);
}

function setDirectory(column, kind, initialTraceId = null) {
  hideCorrelationArrow();
  column.kind = kind;
  $(".directory-select", column.root).value = kind;
  const tabs = workspace.tabs.filter(tab => tab.kind === kind);
  const nav = $(".thread-tabs", column.root); nav.replaceChildren();
  tabs.forEach(tab => {
    const button = document.createElement("button"); button.dataset.id = tab.id;
    button.textContent = `${tab.hostname} / ${tab.tid}`;
    button.title = tab.file;
    button.addEventListener("click", () => loadTrace(column, tab.id));
    nav.append(button);
  });
  const selected = tabs.find(tab => tab.id === initialTraceId) || tabs[0];
  return selected ? loadTrace(column, selected.id) : Promise.resolve();
}

function registerNodes(column, nodes, parent = null) {
  for (const node of nodes) {
    node.parent = parent; node.children = null; column.nodes.set(node.id, node);
  }
}

async function loadTrace(column, id) {
  hideCorrelationArrow();
  const version = ++column.loadVersion;
  column.root.querySelectorAll(".thread-tabs button").forEach(item => item.classList.toggle("active", Number(item.dataset.id) === id));
  $(".trace-summary h1", column.root).textContent = "Loading...";
  const response = await fetch(`/api/trace?id=${id}`);
  const trace = await response.json();
  if (version !== column.loadVersion) return;
  column.trace = trace; column.nodes.clear(); registerNodes(column, trace.roots);
  column.requestLinks.clear();
  const summary = trace.summary;
  $(".trace-path", column.root).textContent = `${summary.hostname} / ${summary.kind}`;
  $(".trace-summary h1", column.root).textContent = `Thread ${summary.tid}`;
  $(".metrics", column.root).replaceChildren(
    metric("Nodes", summary.nodes.toLocaleString()), metric("Scopes", summary.scopes.toLocaleString()),
    metric("Events", summary.events.toLocaleString()), metric("Window", ms(summary.window_ms)),
  );
  const input = $(".search input", column.root); input.value = "";
  column.matches = []; column.match = -1; column.eventOffset = 0; column.eventTotal = 0;
  $(".match-count", column.root).textContent = "";
  renderTree(column); showEmptyInspector(column);
  if (column.view === "events") loadEvents(column, true);
  if (column.pendingNode) {
    const nodeId = column.pendingNode; column.pendingNode = null;
    await revealNodeId(column, nodeId);
  }
}

function createNode(column, node) {
  const wrapper = document.createElement("div"); wrapper.className = `tree-node ${node.is_event ? "event" : "scope"}`; wrapper.dataset.nodeId = node.id;
  const row = document.createElement("div"); row.className = "node-row"; row.style.paddingLeft = `${8 + node.depth * 16}px`; row.setAttribute("role", "treeitem");
  const toggle = document.createElement(node.child_count ? "button" : "span"); toggle.className = node.child_count ? "toggle" : "toggle-spacer";
  if (node.child_count) { toggle.type = "button"; toggle.textContent = "+"; toggle.setAttribute("aria-label", `Expand ${node.invoker}`); toggle.setAttribute("aria-expanded", "false"); }
  const name = document.createElement("span"); name.className = "node-name"; name.textContent = node.invoker;
  const time = document.createElement("span"); time.className = `node-time ${durationClass(node.duration_ms)}`; time.textContent = node.is_event ? `@${ms(node.timestamp_ms)}` : ms(node.duration_ms);
  const args = document.createElement("span"); args.className = "node-args"; args.textContent = node.args ? `| ${node.args}` : "";
  row.dataset.invoker = node.invoker; row.dataset.nodeId = node.id;
  row.append(toggle, name, time, args); wrapper.append(row);
  row.addEventListener("click", () => selectNode(column, node, row));
  if (node.child_count) toggle.addEventListener("click", event => { event.stopPropagation(); toggleNode(column, wrapper, node); });
  attachRequestJumps(column, node, row);
  (node.linked_requests || []).filter(request => request.id !== node.id).forEach(request => addSyncJump(row, column, request));
  return wrapper;
}

async function expandNode(column, wrapper, node) {
  let children = $(":scope > .children", wrapper);
  const toggle = $(":scope > .node-row > .toggle", wrapper);
  if (node.children === null) {
    if (node.loading) return;
    node.loading = true; toggle.textContent = "...";
    try {
      const traceId = column.trace.summary.id;
      const response = await fetch(`/api/children?trace=${traceId}&node=${encodeURIComponent(node.id)}`);
      if (!response.ok) throw new Error("Unable to load child traces");
      const payload = await response.json();
      if (!column.trace || column.trace.summary.id !== traceId) return;
      node.children = payload.nodes; registerNodes(column, node.children, node);
    } catch (error) { toggle.textContent = "+"; console.error(error); return; }
    finally { node.loading = false; }
  }
  if (!children) {
    children = document.createElement("div"); children.className = "children";
    node.children.forEach(child => children.append(createNode(column, child))); wrapper.append(children);
  }
  children.hidden = false; toggle.textContent = "-"; toggle.setAttribute("aria-expanded", "true");
}

function collapseNode(wrapper) {
  hideCorrelationArrow();
  const children = $(":scope > .children", wrapper); if (!children) return;
  children.hidden = true; const toggle = $(":scope > .node-row > .toggle", wrapper);
  toggle.textContent = "+"; toggle.setAttribute("aria-expanded", "false");
}

async function toggleNode(column, wrapper, node) {
  const children = $(":scope > .children", wrapper);
  if (!children || children.hidden) await expandNode(column, wrapper, node); else collapseNode(wrapper);
}

function renderTree(column) {
  const tree = $(".tree", column.root); tree.replaceChildren();
  column.trace.roots.forEach(root => tree.append(createNode(column, root)));
  $(".tree-empty", column.root).hidden = true;
}

function selectNode(column, node, row) {
  column.root.querySelectorAll(".node-row.selected, .event-table tr.selected").forEach(item => item.classList.remove("selected"));
  row.classList.add("selected");
  column.selectedNode = node;
  const inspector = $(".inspector", column.root); inspector.replaceChildren();
  const label = document.createElement("div"); label.className = "panel-label"; label.textContent = "Inspector";
  const body = document.createElement("div"); body.className = "inspect-body";
  body.innerHTML = `<div class="inspect-kind"></div><h2></h2><div class="inspect-grid"></div><div class="inspect-args"></div>`;
  $(".inspect-kind", body).textContent = node.is_event ? "Event" : `Scope / depth ${node.depth}`;
  $("h2", body).textContent = node.invoker;
  const values = [["Source", `${node.file}:${node.line}`], ["Duration", ms(node.duration_ms)], ["Timestamp", ms(node.timestamp_ms)], ["Enter", ms(node.enter_ms)], ["Exit", ms(node.exit_ms)], ["Children", node.child_count]];
  const grid = $(".inspect-grid", body);
  values.forEach(([key, value]) => { const cell = document.createElement("div"); const small = document.createElement("small"); small.textContent = key; const span = document.createElement("span"); span.textContent = value; cell.append(small, span); grid.append(cell); });
  $(".inspect-args", body).textContent = node.args || "No arguments"; inspector.append(label, body);
  correlateSelection(column, node, inspector);
}

function showEmptyInspector(column) {
  $(".inspector", column.root).innerHTML = '<div class="panel-label">Inspector</div><div class="inspector-empty">Select a trace row to inspect its timing and source.</div>';
}

function createEventRow(column, event) {
  const row = document.createElement("tr");
  row.dataset.invoker = event.invoker;
  row.dataset.nodeId = event.id;
  [ms(event.timestamp_ms), event.invoker, event.args || "--", event.context, `${event.file}:${event.line}`].forEach((value, index) => {
    const cell = document.createElement("td"); cell.textContent = value;
    if (index === 0) cell.className = "event-time"; if (index === 1) cell.className = "event-name"; row.append(cell);
  });
  row.addEventListener("click", () => selectNode(column, event, row));
  attachRequestJumps(column, event, row);
  return row;
}

async function loadEvents(column, reset = false) {
  if (!column.trace) return;
  if (reset) { column.eventOffset = 0; $(".event-table tbody", column.root).replaceChildren(); }
  const version = ++column.searchVersion; const traceId = column.trace.summary.id;
  const query = $(".search input", column.root).value.trim();
  $(".event-count", column.root).textContent = "Loading...";
  const response = await fetch(`/api/events?trace=${traceId}&offset=${column.eventOffset}&limit=250&q=${encodeURIComponent(query)}`);
  if (version !== column.searchVersion || !column.trace || column.trace.summary.id !== traceId) return;
  const payload = await response.json(); const body = $(".event-table tbody", column.root);
  payload.events.forEach(event => body.append(createEventRow(column, event)));
  column.eventOffset += payload.events.length; column.eventTotal = payload.total;
  $(".event-count", column.root).textContent = `${column.eventOffset.toLocaleString()} of ${column.eventTotal.toLocaleString()}`;
  $(".events-empty", column.root).hidden = column.eventTotal > 0;
  $(".load-events", column.root).hidden = column.eventOffset >= column.eventTotal;
  $(".match-count", column.root).textContent = query ? `${column.eventTotal.toLocaleString()} found` : "";
}

function setView(column, view) {
  hideCorrelationArrow();
  column.view = view; const events = view === "events";
  $(".tree-panel", column.root).hidden = events; $(".events-panel", column.root).hidden = !events;
  $(".tree-actions", column.root).hidden = events; $(".previous-match", column.root).hidden = events; $(".next-match", column.root).hidden = events;
  $(".show-tree", column.root).classList.toggle("active", !events); $(".show-tree", column.root).setAttribute("aria-pressed", String(!events));
  $(".show-events", column.root).classList.toggle("active", events); $(".show-events", column.root).setAttribute("aria-pressed", String(events));
  $(".search input", column.root).placeholder = events ? "Filter events, details, or source" : "Invoker, arguments, or source";
  $(".match-count", column.root).textContent = "";
  if (events) loadEvents(column, true); else search(column);
}

async function revealNodeId(column, nodeId, behavior = "smooth") {
  setView(column, "tree");
  const parts = nodeId.split(".");
  for (let length = 1; length < parts.length; length += 1) {
    const ancestorId = parts.slice(0, length).join(".");
    const ancestor = column.nodes.get(ancestorId);
    const wrapper = column.root.querySelector(`[data-node-id="${ancestorId}"]`);
    if (!ancestor || !wrapper) return null;
    await expandNode(column, wrapper, ancestor);
  }
  const node = column.nodes.get(nodeId);
  const wrapper = column.root.querySelector(`[data-node-id="${nodeId}"]`);
  if (!node || !wrapper) return null;
  const row = $(":scope > .node-row", wrapper);
  selectNode(column, node, row);
  row.scrollIntoView({ block: "center", behavior });
  return row;
}

async function revealMatch(column) {
  if (!column.matches.length) return;
  const match = column.matches[column.match]; const parts = match.id.split(".");
  for (let length = 1; length < parts.length; length += 1) {
    const ancestorId = parts.slice(0, length).join("."); const ancestor = column.nodes.get(ancestorId);
    const wrapper = column.root.querySelector(`[data-node-id="${ancestorId}"]`);
    if (!ancestor || !wrapper) return; await expandNode(column, wrapper, ancestor);
  }
  const node = column.nodes.get(match.id) || match;
  const wrapper = column.root.querySelector(`[data-node-id="${node.id}"]`); if (!wrapper) return;
  const row = $(":scope > .node-row", wrapper); selectNode(column, node, row); row.scrollIntoView({ block: "center", behavior: "smooth" });
  $(".match-count", column.root).textContent = `${column.match + 1}/${column.matches.length}`;
}

async function search(column) {
  if (!column.trace) return;
  if (column.view === "events") {
    const version = ++column.searchVersion; await new Promise(resolve => setTimeout(resolve, 180));
    if (version === column.searchVersion) loadEvents(column, true); return;
  }
  const query = $(".search input", column.root).value.trim(); const version = ++column.searchVersion;
  if (!query) { column.matches = []; column.match = -1; $(".match-count", column.root).textContent = ""; $(".tree-empty", column.root).hidden = true; return; }
  $(".match-count", column.root).textContent = "..."; await new Promise(resolve => setTimeout(resolve, 180));
  if (version !== column.searchVersion) return;
  const traceId = column.trace.summary.id; const response = await fetch(`/api/search?trace=${traceId}&q=${encodeURIComponent(query)}`);
  if (version !== column.searchVersion || column.trace.summary.id !== traceId) return;
  const payload = await response.json(); column.matches = payload.matches; column.match = column.matches.length ? 0 : -1;
  $(".match-count", column.root).textContent = `0/${column.matches.length}${payload.limited ? "+" : ""}`;
  $(".tree-empty", column.root).hidden = column.matches.length > 0;
  if (column.matches.length) await revealMatch(column);
}

async function stepMatch(column, direction) {
  if (!column.matches.length) return;
  column.match = (column.match + direction + column.matches.length) % column.matches.length; await revealMatch(column);
}

async function expandLoadedLevel(column) {
  const currentLevel = [...column.nodes.values()];
  await Promise.all(currentLevel.map(node => { const wrapper = column.root.querySelector(`[data-node-id="${node.id}"]`); return wrapper && node.child_count ? expandNode(column, wrapper, node) : null; }));
}

function appendTableRow(body, values, onClick = null) {
  const row = document.createElement("tr");
  values.forEach(value => { const cell = document.createElement("td"); cell.textContent = value; row.append(cell); });
  if (onClick) { row.classList.add("clickable"); row.addEventListener("click", onClick); }
  body.append(row);
}

function showInsights(column) {
  const analysis = column.trace.analysis;
  $("#insights-title").textContent = `${column.kind} / Thread ${column.trace.summary.tid}`;
  const percentiles = $("#percentiles"); percentiles.replaceChildren();
  Object.entries(analysis.percentiles).forEach(([label, value]) => percentiles.append(metric(label, ms(value))));
  const slowest = $("#slowest-body"); slowest.replaceChildren();
  analysis.slowest.forEach(item => appendTableRow(slowest, [item.invoker, item.path, ms(item.duration_ms)], () => { $("#insights-panel").hidden = true; revealNodeId(column, item.id); }));
  const frequent = $("#frequent-body"); frequent.replaceChildren();
  analysis.frequent.forEach(item => appendTableRow(frequent, [item.invoker, item.kind, item.count.toLocaleString()]));
  const paths = $("#paths-body"); paths.replaceChildren();
  analysis.call_paths.forEach(item => appendTableRow(paths, [item.path, item.count.toLocaleString(), ms(item.total_ms)]));
  const stats = $("#stats-body"); stats.replaceChildren();
  column.trace.stats.forEach(item => appendTableRow(stats, [item.invoker, item.count, ms(item.total_ms), ms(item.mean_ms.toFixed(1)), ms(item.median_ms.toFixed(1)), ms(item.max_ms), ms(item.min_ms), ms(item.std_ms.toFixed(1))]));
  $("#insights-panel").hidden = false;
}

function exportEvents(column, format) {
  const query = $(".search input", column.root).value.trim();
  window.location.href = `/api/export?trace=${column.trace.summary.id}&format=${format}&q=${encodeURIComponent(query)}`;
  $(".export-menu", column.root).open = false;
}

async function copyLink(column, button) {
  const url = new URL(window.location.href);
  url.search = "";
  url.searchParams.set("trace", column.trace.summary.id);
  if (column.selectedNode) url.searchParams.set("node", column.selectedNode.id);
  try {
    await navigator.clipboard.writeText(url.toString());
    const original = button.textContent; button.textContent = "Copied";
    setTimeout(() => { button.textContent = original; }, 1200);
  } catch (error) {
    window.prompt("Copy trace link", url.toString());
  }
}

async function openCorrelatedNode(target, traceId, nodeId) {
  if (!target.trace || target.trace.summary.id !== traceId) await loadTrace(target, traceId);
  return revealNodeId(target, nodeId, "auto");
}

function correlationTarget(source, traceId) {
  const tab = workspace.tabs.find(item => item.id === traceId);
  return workspace.columns.find(item => item !== source && item.kind === tab?.kind) || null;
}

async function openCorrelationTarget(source, traceId, nodeId) {
  const tab = workspace.tabs.find(item => item.id === traceId);
  if (!tab) return null;
  let target = correlationTarget(source, traceId);
  if (!target) {
    target = createColumn(tab.kind, traceId);
    await target.ready;
  }
  const row = await openCorrelatedNode(target, traceId, nodeId);
  return { target, row };
}

async function connectCorrelatedNodes(source, sourceNodeId, traceId, targetNodeId, sourceRow = null) {
  hideCorrelationArrow();
  const destinationPromise = openCorrelationTarget(source, traceId, targetNodeId);
  const sourcePromise = sourceRow?.isConnected
    ? Promise.resolve(sourceRow)
    : revealNodeId(source, sourceNodeId, "auto");
  const [destinationResult, sourceResult] = await Promise.allSettled([destinationPromise, sourcePromise]);
  if (destinationResult.status === "rejected") throw destinationResult.reason;
  const destination = destinationResult.value;
  const resolvedSourceRow = sourceResult.status === "fulfilled" ? sourceResult.value : null;
  if (resolvedSourceRow && destination?.row) {
    showCorrelationArrow(resolvedSourceRow, destination.row, source, destination.target, source.trace.summary.id, traceId);
  }
}

function addCorrelationJump(row, source, sourceNodeId, traceId, node, persistent = false) {
  if (!row) return;
  const host = row.matches("tr") ? row.lastElementChild : row;
  if (host.querySelector(`.correlation-jump[data-trace-id="${traceId}"]`)) return;
  const tab = workspace.tabs.find(item => item.id === traceId);
  const button = document.createElement("button");
  button.className = "correlation-jump";
  button.classList.toggle("persistent", persistent);
  button.dataset.traceId = traceId;
  button.textContent = `Open ${tab?.kind || "match"}`;
  button.title = `Open the associated request in ${tab?.kind || "another component"} / thread ${tab?.tid || "?"}`;
  button.addEventListener("click", async event => {
    event.stopPropagation();
    const label = button.textContent;
    button.textContent = "Opening..."; button.disabled = true;
    try {
      await connectCorrelatedNodes(source, sourceNodeId, traceId, node.id, row);
    } catch (error) {
      console.error(error);
      button.textContent = `Retry ${tab?.kind || "match"}`;
      button.disabled = false;
      return;
    }
    button.textContent = label; button.disabled = false;
  });
  host.append(button);
  if (activeConnection && (
    (source === activeConnection.source && traceId === activeConnection.targetTrace)
    || (source === activeConnection.target && traceId === activeConnection.sourceTrace)
  )) {
    button.hidden = true;
    activeConnection.hiddenButtons.push(button);
  }
}

function addSyncJump(row, column, request) {
  if (row.querySelector(`.sync-jump[data-node-id="${request.id}"]`)) return;
  const button = document.createElement("button");
  button.className = "sync-jump";
  button.dataset.nodeId = request.id;
  button.textContent = "Go to sync event";
  button.title = `Expand to request ${request.request}`;
  button.addEventListener("click", event => {
    event.preventDefault(); event.stopPropagation();
    revealNodeId(column, request.id);
  });
  row.append(button);
}

async function attachRequestJumps(column, node, row) {
  if (!node.request) return;
  const key = `${column.trace.summary.id}:${node.id}`;
  if (column.requestLinks.has(key)) return;
  column.requestLinks.add(key);
  const traceIds = workspace.tabs.map(tab => tab.id);
  const response = await fetch(`/api/correlate?source=${column.trace.summary.id}&node=${encodeURIComponent(node.id)}&traces=${traceIds.join(",")}`);
  const payload = await response.json();
  if (!row.isConnected || !column.trace || column.trace.summary.id !== Number(row.closest(".trace-column")?.querySelector(".thread-tabs .active")?.dataset.id)) return;
  payload.matches.forEach(match => addCorrelationJump(row, column, node.id, match.trace, match.node, true));
}

async function correlateSelection(column, node, inspector) {
  const version = ++column.correlationVersion;
  workspace.columns.forEach(item => {
    item.root.querySelectorAll(".correlation-jump:not(.persistent)").forEach(button => button.remove());
    item.root.querySelectorAll("[data-invoker]").forEach(row => {
      row.classList.toggle("correlated", item !== column && row.dataset.invoker === node.invoker);
    });
  });
  const traceIds = workspace.tabs.map(tab => tab.id);
  const response = await fetch(`/api/correlate?source=${column.trace.summary.id}&node=${encodeURIComponent(node.id)}&traces=${traceIds.join(",")}`);
  if (version !== column.correlationVersion || column.selectedNode !== node) return;
  const payload = await response.json();
  const matches = payload.matches;
  if (!matches.length) return;
  const section = document.createElement("div"); section.className = "correlations";
  const title = document.createElement("small");
  title.textContent = payload.mode === "request" ? `Request interaction: ${payload.request || payload.terms.join(" / ")}` : "Cross-column matches";
  section.append(title);
  const sourceRow = [...column.root.querySelectorAll(".node-row, .event-table tr")].find(row => row.dataset.nodeId === node.id);
  matches.forEach(match => {
    const targetTab = workspace.tabs.find(tab => tab.id === match.trace);
    const target = correlationTarget(column, match.trace);
    const loadedRow = target?.trace?.summary.id === match.trace
      ? [...target.root.querySelectorAll(".node-row, .event-table tr")].find(row => row.dataset.nodeId === match.node.id)
      : null;
    if (loadedRow) {
      loadedRow.classList.add("correlated");
      addCorrelationJump(loadedRow, target, match.node.id, column.trace.summary.id, node, true);
    }
    addCorrelationJump(sourceRow, column, node.id, match.trace, match.node, true);
    const button = document.createElement("button");
    const relation = payload.mode === "request" ? `${Math.round(match.score * 100)}% parameter match` : `${match.delta_ms >= 0 ? "+" : ""}${ms(match.delta_ms)}`;
    button.textContent = `${targetTab.kind} / thread ${targetTab.tid}  ${relation}`;
    button.addEventListener("click", () => connectCorrelatedNodes(column, node.id, match.trace, match.node.id, sourceRow)); section.append(button);
  });
  inspector.append(section);
}

function addColumn() {
  const directories = [...new Set(workspace.tabs.map(tab => tab.kind))];
  const used = new Set(workspace.columns.map(column => column.kind));
  createColumn(directories.find(directory => !used.has(directory)) || directories[0]);
}

$("#add-column").addEventListener("click", addColumn);
$("#close-insights").addEventListener("click", () => $("#insights-panel").hidden = true);
let globalSearchTimer;
$("#global-search").addEventListener("input", event => {
  clearTimeout(globalSearchTimer);
  globalSearchTimer = setTimeout(() => {
    workspace.columns.forEach(column => { $(".search input", column.root).value = event.target.value; search(column); });
  }, 120);
});
document.addEventListener("keydown", event => {
  if (event.key === "/" && !event.target.matches("input, select")) { event.preventDefault(); $("#global-search").focus(); }
  if (event.key === "Escape") $("#insights-panel").hidden = true;
});
window.addEventListener("resize", drawCorrelationArrow);
document.addEventListener("scroll", () => {
  if (activeConnection) requestAnimationFrame(drawCorrelationArrow);
}, true);

fetch("/api/overview").then(response => response.json()).then(data => {
  workspace.tabs = data.tabs; $("#columns").replaceChildren();
  const params = new URLSearchParams(window.location.search);
  const linkedTrace = Number(params.get("trace"));
  const linkedTab = params.has("trace") ? workspace.tabs.find(tab => tab.id === linkedTrace) : null;
  if (linkedTab) createColumn(linkedTab.kind, linkedTab.id, params.get("node"));
  else if (workspace.tabs.length) createColumn(workspace.tabs[0].kind);
  else $("#columns").innerHTML = '<div class="loading">No trace logs found.</div>';
});
