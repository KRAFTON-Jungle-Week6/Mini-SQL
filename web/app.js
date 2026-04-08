const presets = [
  {
    label: "Select All",
    sql: "SELECT * FROM users;",
  },
  {
    label: "Select With Filter",
    sql: "SELECT name, age FROM users WHERE id = 2;",
  },
  {
    label: "Classic Insert",
    sql: "INSERT INTO users VALUES (3, 'Carol', 30);",
  },
  {
    label: "Flexible Insert",
    sql: "INSERT INTO users (name, id, age) VALUES ('Dave', 4, 28);",
  },
];

const HISTORY_LIMIT = 1;

const stages = [
  { id: "tokenizer", title: "tokenize_sql()", summary: (trace) => `${trace.stages.tokenizer.tokens.length} tokens` },
  {
    id: "parser",
    title: "parse_statement()",
    summary: (trace) => trace.stages.parser.statement?.type ?? "no statement",
  },
  {
    id: "executor",
    title: "execute_statement()",
    summary: (trace) => {
      const lines = (trace.stages.executor.output || "").trim().split("\n").filter(Boolean);
      return `${lines.length} output lines`;
    },
  },
];

const state = {
  snapshot: null,
  trace: null,
  selectedStage: "tokenizer",
  history: [],
};

const elements = {
  schemaTree: document.getElementById("schema-tree"),
  presetList: document.getElementById("preset-list"),
  sqlInput: document.getElementById("sql-input"),
  runButton: document.getElementById("run-sql"),
  resetButton: document.getElementById("reset-demo"),
  consoleHistory: document.getElementById("console-history"),
  stageCards: document.getElementById("stage-cards"),
  detailTitle: document.getElementById("detail-title"),
  detailContent: document.getElementById("detail-content"),
  tableSnapshots: document.getElementById("table-snapshots"),
};

function createElement(tag, className, text) {
  const node = document.createElement(tag);
  if (className) {
    node.className = className;
  }
  if (text !== undefined) {
    node.textContent = text;
  }
  return node;
}

async function fetchJson(url, options = {}) {
  const response = await fetch(url, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });
  return response.json();
}

function renderSchema() {
  elements.schemaTree.innerHTML = "";

  if (!state.snapshot?.tables?.length) {
    elements.schemaTree.append(createElement("div", "schema-card", "No tables loaded"));
    return;
  }

  for (const table of state.snapshot.tables) {
    const card = createElement("article", "schema-card");
    card.append(createElement("h3", "", table.name));

    const chips = createElement("div", "column-chip-list");
    for (const column of table.columns) {
      const chip = createElement("span", "chip", column);
      chips.append(chip);
    }

    card.append(chips);
    elements.schemaTree.append(card);
  }
}

function renderPresets() {
  elements.presetList.innerHTML = "";

  presets.forEach((preset) => {
    const button = createElement("button", "preset-button");
    button.type = "button";
    button.addEventListener("click", () => {
      elements.sqlInput.value = preset.sql;
      runSql(preset.sql);
    });

    button.append(createElement("span", "preset-title", preset.label));
    button.append(createElement("span", "preset-sql", preset.sql));
    elements.presetList.append(button);
  });
}

function renderHistory() {
  elements.consoleHistory.innerHTML = "";

  if (!state.history.length) {
    const empty = createElement("div", "console-entry result", "아직 실행된 명령이 없습니다.");
    elements.consoleHistory.append(empty);
    return;
  }

  for (const entry of state.history) {
    const commandBox = createElement("div", "console-entry command");
    commandBox.append(createElement("div", "command-sql", `mini_sql> ${entry.sql}`));
    elements.consoleHistory.append(commandBox);

    const outputBox = createElement("div", `console-entry ${entry.ok ? "result" : "error"}`);
    outputBox.textContent = entry.output;
    elements.consoleHistory.append(outputBox);
  }
}

function buildStageCard(stage) {
  const trace = state.trace;
  const stageData = trace?.stages?.[stage.id];
  const card = createElement("button", "stage-card");
  card.type = "button";
  card.classList.toggle("active", state.selectedStage === stage.id);
  card.classList.toggle("fail", trace?.error?.stage === stage.id);
  card.addEventListener("click", () => {
    state.selectedStage = stage.id;
    renderInspector();
  });

  card.append(createElement("h3", "", stage.title));
  return card;
}

function renderStageCards() {
  elements.stageCards.innerHTML = "";
  stages.forEach((stage) => elements.stageCards.append(buildStageCard(stage)));
}

function renderTokenTable(tokens) {
  const table = createElement("table", "tokens-table");
  table.innerHTML = `
    <thead>
      <tr>
        <th>#</th>
        <th>Type</th>
        <th>Lexeme</th>
        <th>Position</th>
      </tr>
    </thead>
  `;

  const body = document.createElement("tbody");
  for (const token of tokens) {
    const row = document.createElement("tr");

    const indexCell = createElement("td", "", String(token.index));
    const typeCell = document.createElement("td");
    typeCell.append(createElement("span", "token-type", token.type));
    const lexemeCell = createElement("td", "", token.lexeme);
    const positionCell = createElement("td", "", String(token.position));

    row.append(indexCell, typeCell, lexemeCell, positionCell);
    body.append(row);
  }

  table.append(body);
  return table;
}

function renderObjectTree(value, key = "root") {
  if (value === null || typeof value !== "object") {
    const leaf = createElement("div", "tree-leaf");
    leaf.append(createElement("span", "tree-key", key));
    leaf.append(createElement("span", "leaf-value", JSON.stringify(value)));
    return leaf;
  }

  const node = createElement("div", "tree-node");
  const header = createElement("div", "tree-node-header");
  const type = Array.isArray(value) ? `array(${value.length})` : "object";
  header.append(createElement("span", "tree-key", key));
  header.append(createElement("span", "tree-type", type));
  node.append(header);

  const children = createElement("div", "tree-children");
  const entries = Array.isArray(value)
    ? value.map((item, index) => [String(index), item])
    : Object.entries(value);

  for (const [childKey, childValue] of entries) {
    children.append(renderObjectTree(childValue, childKey));
  }

  node.append(children);
  return node;
}

function renderJsonBlock(value) {
  const pre = createElement("pre", "code-block");
  pre.textContent = JSON.stringify(value, null, 2);
  return pre;
}

function renderStageDetail() {
  const trace = state.trace;

  if (!trace) {
    elements.detailTitle.textContent = "Trace Details";
    elements.detailContent.className = "detail-content empty-state";
    elements.detailContent.textContent = "쿼리를 실행하면 여기서 tokens, statement, executor output 을 볼 수 있습니다.";
    return;
  }

  const stage = state.selectedStage;
  const stageData = trace.stages[stage];
  elements.detailContent.className = "detail-content";
  elements.detailContent.innerHTML = "";

  if (stage === "tokenizer") {
    elements.detailTitle.textContent = "Token Stream";
    elements.detailContent.append(renderTokenTable(stageData.tokens));
    elements.detailContent.append(renderJsonBlock(stageData.tokens));
    return;
  }

  if (stage === "parser") {
    const statement = stageData.statement;
    elements.detailTitle.textContent = "Parser Statement";

    if (!statement) {
      elements.detailContent.append(createElement("div", "console-entry error", trace.error?.message || "stage failed"));
      return;
    }

    const tree = createElement("div", "tree-view");
    tree.append(renderObjectTree(statement, "statement"));
    elements.detailContent.append(tree);
    elements.detailContent.append(renderJsonBlock(statement));
    return;
  }

  elements.detailTitle.textContent = "Executor Output";

  if (!trace.ok && trace.error?.stage === "executor") {
    elements.detailContent.append(createElement("div", "console-entry error", trace.error.message));
  }

  elements.detailContent.append(renderJsonBlock({ output: stageData.output || "" }));
}

function renderInspector() {
  renderStageCards();
  renderStageDetail();
}

function renderSnapshotTables() {
  elements.tableSnapshots.innerHTML = "";

  if (!state.snapshot?.tables?.length) {
    elements.tableSnapshots.append(createElement("div", "schema-card", "No table data available."));
    return;
  }

  for (const table of state.snapshot.tables) {
    const card = createElement("article", "table-card");
    card.append(createElement("h3", "", table.name));

    const shell = createElement("div", "table-shell");
    const tableNode = createElement("table", "snapshot-table");

    const thead = document.createElement("thead");
    const headRow = document.createElement("tr");
    for (const column of table.columns) {
      headRow.append(createElement("th", "", column));
    }
    thead.append(headRow);
    tableNode.append(thead);

    const tbody = document.createElement("tbody");
    for (const row of table.rows) {
      const rowNode = document.createElement("tr");
      row.forEach((cell) => rowNode.append(createElement("td", "", cell)));
      tbody.append(rowNode);
    }
    tableNode.append(tbody);
    shell.append(tableNode);
    card.append(shell);
    elements.tableSnapshots.append(card);
  }
}

function setBusy(isBusy) {
  elements.runButton.disabled = isBusy;
  elements.resetButton.disabled = isBusy;
}

async function runSql(sqlOverride) {
  const sql = (sqlOverride ?? elements.sqlInput.value).trim();
  if (!sql) {
    return;
  }

  setBusy(true);

  try {
    const payload = await fetchJson("/api/run", {
      method: "POST",
      body: JSON.stringify({ sql }),
    });

    state.trace = payload.trace;
    state.snapshot = payload.snapshot;
    state.selectedStage = payload.trace.ok ? "executor" : payload.trace.error.stage;
    state.history.unshift({
      kind: payload.trace.ok ? "success" : "failure",
      sql,
      ok: payload.trace.ok,
      output: payload.trace.ok
        ? payload.trace.stages.executor.output || "(no output)"
        : `${payload.trace.error.stage}: ${payload.trace.error.message}`,
    });
    state.history = state.history.slice(0, HISTORY_LIMIT);

    renderHistory();
    renderInspector();
    renderSchema();
    renderSnapshotTables();
    setBusy(false);
  } catch (error) {
    setBusy(false);
    state.history.unshift({
      kind: "failure",
      sql,
      ok: false,
      output: String(error),
    });
    renderHistory();
  }
}

async function resetWorkspace() {
  setBusy(true);
  const payload = await fetchJson("/api/reset", {
    method: "POST",
    body: JSON.stringify({}),
  });
  state.snapshot = payload.snapshot;
  state.trace = null;
  state.selectedStage = "tokenizer";
  state.history.unshift({
    kind: "system",
    sql: "RESET DEMO WORKSPACE",
    ok: true,
    output: "demo data restored to initial state",
  });
  state.history = state.history.slice(0, HISTORY_LIMIT);
  renderSchema();
  renderHistory();
  renderInspector();
  renderSnapshotTables();
  setBusy(false);
}

async function loadInitialState() {
  setBusy(true);
  const payload = await fetchJson("/api/state");
  state.snapshot = payload.snapshot;
  renderSchema();
  renderPresets();
  renderHistory();
  renderInspector();
  renderSnapshotTables();
  elements.sqlInput.value = presets[0].sql;
  setBusy(false);
}

elements.runButton.addEventListener("click", () => runSql());
elements.resetButton.addEventListener("click", () => resetWorkspace());
elements.sqlInput.addEventListener("keydown", (event) => {
  if ((event.metaKey || event.ctrlKey) && event.key === "Enter") {
    event.preventDefault();
    runSql();
  }
});

loadInitialState();
