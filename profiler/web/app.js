const workspace = { tabs: [], columns: [], nextColumnId: 1 };
const $ = (selector, root = document) => root.querySelector(selector);

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

function createColumn(kind) {
  const column = {
    id: workspace.nextColumnId++, kind, trace: null, nodes: new Map(), matches: [],
    match: -1, searchVersion: 0, loadVersion: 0, view: "tree", eventOffset: 0,
    eventTotal: 0, root: document.createElement("section"),
  };
  column.root.className = "trace-column";
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
      <button class="show-stats">Stats</button>
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
  $(".show-stats", column.root).addEventListener("click", () => showStats(column));
  const searchInput = $(".search input", column.root);
  searchInput.addEventListener("input", () => search(column));
  searchInput.addEventListener("keydown", event => {
    if (event.key === "Enter" && column.view === "tree") stepMatch(column, event.shiftKey ? -1 : 1);
  });

  workspace.columns.push(column);
  $("#columns").append(column.root);
  updateRemoveButtons();
  setDirectory(column, kind);
}

function removeColumn(column) {
  if (workspace.columns.length === 1) return;
  workspace.columns = workspace.columns.filter(item => item !== column);
  column.root.remove();
  updateRemoveButtons();
}

function updateRemoveButtons() {
  const disabled = workspace.columns.length === 1;
  workspace.columns.forEach(column => $(".remove-column", column.root).hidden = disabled);
}

function setDirectory(column, kind) {
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
  if (tabs.length) loadTrace(column, tabs[0].id);
}

function registerNodes(column, nodes, parent = null) {
  for (const node of nodes) {
    node.parent = parent; node.children = null; column.nodes.set(node.id, node);
  }
}

async function loadTrace(column, id) {
  const version = ++column.loadVersion;
  column.root.querySelectorAll(".thread-tabs button").forEach(item => item.classList.toggle("active", Number(item.dataset.id) === id));
  $(".trace-summary h1", column.root).textContent = "Loading...";
  const response = await fetch(`/api/trace?id=${id}`);
  const trace = await response.json();
  if (version !== column.loadVersion) return;
  column.trace = trace; column.nodes.clear(); registerNodes(column, trace.roots);
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
}

function createNode(column, node) {
  const wrapper = document.createElement("div"); wrapper.className = `tree-node ${node.is_event ? "event" : "scope"}`; wrapper.dataset.nodeId = node.id;
  const row = document.createElement("div"); row.className = "node-row"; row.style.paddingLeft = `${8 + node.depth * 16}px`; row.setAttribute("role", "treeitem");
  const toggle = document.createElement(node.child_count ? "button" : "span"); toggle.className = node.child_count ? "toggle" : "toggle-spacer";
  if (node.child_count) { toggle.type = "button"; toggle.textContent = "+"; toggle.setAttribute("aria-label", `Expand ${node.invoker}`); toggle.setAttribute("aria-expanded", "false"); }
  const name = document.createElement("span"); name.className = "node-name"; name.textContent = node.invoker;
  const time = document.createElement("span"); time.className = `node-time ${durationClass(node.duration_ms)}`; time.textContent = node.is_event ? `@${ms(node.timestamp_ms)}` : ms(node.duration_ms);
  const args = document.createElement("span"); args.className = "node-args"; args.textContent = node.args ? `| ${node.args}` : "";
  row.append(toggle, name, time, args); wrapper.append(row);
  row.addEventListener("click", () => selectNode(column, node, row));
  if (node.child_count) toggle.addEventListener("click", event => { event.stopPropagation(); toggleNode(column, wrapper, node); });
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
}

function showEmptyInspector(column) {
  $(".inspector", column.root).innerHTML = '<div class="panel-label">Inspector</div><div class="inspector-empty">Select a trace row to inspect its timing and source.</div>';
}

function createEventRow(column, event) {
  const row = document.createElement("tr");
  [ms(event.timestamp_ms), event.invoker, event.args || "--", event.context, `${event.file}:${event.line}`].forEach((value, index) => {
    const cell = document.createElement("td"); cell.textContent = value;
    if (index === 0) cell.className = "event-time"; if (index === 1) cell.className = "event-name"; row.append(cell);
  });
  row.addEventListener("click", () => selectNode(column, event, row)); return row;
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
  column.view = view; const events = view === "events";
  $(".tree-panel", column.root).hidden = events; $(".events-panel", column.root).hidden = !events;
  $(".tree-actions", column.root).hidden = events; $(".previous-match", column.root).hidden = events; $(".next-match", column.root).hidden = events;
  $(".show-tree", column.root).classList.toggle("active", !events); $(".show-tree", column.root).setAttribute("aria-pressed", String(!events));
  $(".show-events", column.root).classList.toggle("active", events); $(".show-events", column.root).setAttribute("aria-pressed", String(events));
  $(".search input", column.root).placeholder = events ? "Filter events, details, or source" : "Invoker, arguments, or source";
  $(".match-count", column.root).textContent = "";
  if (events) loadEvents(column, true); else search(column);
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

function showStats(column) {
  const body = $("#stats-body"); body.replaceChildren();
  column.trace.stats.forEach(stat => {
    const row = document.createElement("tr");
    [stat.invoker, stat.count, ms(stat.total_ms), ms(stat.mean_ms.toFixed(1)), ms(stat.median_ms.toFixed(1)), ms(stat.max_ms), ms(stat.min_ms), ms(stat.std_ms.toFixed(1))].forEach(value => { const cell = document.createElement("td"); cell.textContent = value; row.append(cell); });
    body.append(row);
  });
  $("#stats-title").textContent = `${column.kind} / Thread ${column.trace.summary.tid}`;
  $("#stats-panel").hidden = false;
}

function addColumn() {
  const directories = [...new Set(workspace.tabs.map(tab => tab.kind))];
  const used = new Set(workspace.columns.map(column => column.kind));
  createColumn(directories.find(directory => !used.has(directory)) || directories[0]);
}

$("#add-column").addEventListener("click", addColumn);
$("#close-stats").addEventListener("click", () => $("#stats-panel").hidden = true);
document.addEventListener("keydown", event => {
  if (event.key === "/" && !event.target.matches("input, select")) { event.preventDefault(); $(".trace-column .search input")?.focus(); }
  if (event.key === "Escape") $("#stats-panel").hidden = true;
});

fetch("/api/overview").then(response => response.json()).then(data => {
  workspace.tabs = data.tabs; $("#columns").replaceChildren();
  if (workspace.tabs.length) createColumn(workspace.tabs[0].kind);
  else $("#columns").innerHTML = '<div class="loading">No trace logs found.</div>';
});
