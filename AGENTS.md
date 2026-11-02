# AGENTS.md

# GraphXPort

## Mission

GraphXPort is a standalone Unreal Engine Editor plugin for exporting Blueprint
graphs into deterministic, portable representations.

It must work without:

- UnrealMCP
- an MCP client
- an AI service
- Python, Node.js, or an external process
- network access

Users access GraphXPort directly from Blueprint Editor context menus.

## Supported Engine

- Unreal Engine 5.4.4 is the current build and test target.
- Keep engine-specific editor integrations isolated where practical.

## Modules

The plugin ships three editor modules:

- `GraphXPort` - export, formatting, and Beautify. Has no git dependency and works
  entirely offline, as described below.
- `GraphXPortConflictToolset` - Epic MCP conflict workflows; depends on ToolsetRegistry and UnrealMCP in UE 5.8.
- `GraphXPortMerge` - the **GraphXPort Merge** window: git status, side-by-side
  asset revision comparison, and manual conflict resolution. It shells out to
  `git`, which is the one place the plugin invokes an external process.

`GraphXPortMerge` may depend on `GraphXPort`; never the reverse. Nothing in
`GraphXPort` may require git to be installed.

## Current Scope

GraphXPort currently converts selected nodes or the entire active graph into an
internal document and copies views of it as:

- structured in-memory graph document for the formatters
- AI-oriented Markdown
- ASCII flow text
- Mermaid flowchart text

Selection views must include:

- Blueprint and graph identity
- nodes and pins
- pin defaults
- function, event, variable, member, and macro references when available
- connections inside the selection
- connections crossing the selection boundary

The export path is read-only. Two deliberate, separately reviewed exceptions
mutate state: the manual **Beautify Selection** command, and the
`GraphXPortMerge` conflict resolution flow. Both are described under Safety.

## Architecture

Keep responsibilities separated:

- `GraphXPortMenu` owns Unreal Editor menu integration, active selection, and
  applying Beautify's computed positions inside an undo transaction.
- `GraphXPortSerializer` converts Unreal graph objects into the canonical model.
- `GraphXPortDocument` defines the canonical in-memory representation.
- `GraphXPortFormatter` renders Markdown, ASCII, and Mermaid.
- `GraphXPortBeautifier` computes a Beautify layout (node -> target position)
  from live `UEdGraphNode`/`UEdGraphPin` data. It only reads graph state and
  returns positions; it never mutates a node itself.
- Tests validate deterministic export behavior, graph boundaries, and Beautify
  layout correctness (linear/branch ordering, cycles, disconnected islands).

`GraphXPortMerge` keeps the same separation:

- `GraphXPortGitProcess` is the only place a git process is launched. It runs
  `git` directly - never through `cmd.exe` - reads stdout as raw bytes so binary
  `.uasset` payloads survive, and launches detached so Windows gives the child no
  console. Launching merely "hidden" is not enough: `bLaunchReallyHidden` only sets
  `SW_HIDE`, which Windows Terminal ignores, popping a visible window per call.
- `GraphXPortGitStatus` parses `git status --porcelain=v2` into unstaged (Y column)
  and staged (X column) entries, and stages files. Keep parsing in
  `ParseStatusOutput`, separate from process execution, so it stays testable.
- `GraphXPortGitHistory` pages through `git log --name-status` for `.uasset`/`.umap`
  files, with the same parse/execute split (`ParseLogOutput`). Commit fields are
  delimited by `\x1e`/`\x1f`, which cannot occur in commit messages. It is read-only.
- Every file entry records its `Source` (unstaged, staged, commit) and, for a
  rename, `OriginalPath`. The "before" revision is always read at `BeforePath()`,
  because HEAD or the parent still has the file under its old name.
- `GraphXPortGitBlob` extracts a revision's bytes for a path. The refspec is
  anything git accepts before `:path` - a commit, `HEAD`, `<hash>^`, a conflict
  stage (`:2`), or empty for the index.
- `GraphXPortTempPackage` loads a revision using the engine's own
  `DiffUtils::LoadPackageForDiff`. Use that helper rather than a hand-rolled
  `LoadPackage`: it loads from a loose file with
  `LOAD_ForDiff | LOAD_DisableCompileOnLoad | LOAD_DisableEngineVersionChecks`,
  so an old revision never compiles on load, and it needs no mount point, so
  revisions never appear in the Content Browser. It also classifies what a
  package holds, and for a `.umap` it reports the world as the asset and the
  level script Blueprint as the graph source (never creating one on a historical
  revision).
- `GraphXPortAssetDiff` handles assets that have no graphs - user-defined enums,
  structs, and data tables - by projecting them into a list of keyed entries with
  columns, so one comparison and one grid renderer cover all three.
- `SGraphXPortMergeWindow` owns the file list; `SGraphXPortDiffPanel` owns the
  revision panes, the differences tree, and resolution.

Do not put serialization logic in menu callbacks or UI code.
Do not make formatters inspect live Unreal graph objects.
Do not add a second place that launches git.

Two traps in `SGraphXPortDiffPanel` that cost real debugging time:

- **Keep each pane's `FUICommandList` alive in a member.** `SGraphEditor::Construct`
  appends it but stores only a `TWeakPtr`, so a command list built as a local
  variable dies the moment the function returns and every binding silently stops
  working - copy, paste and delete all at once.
- **Focus a node on the next tick, not in the same frame as `RebuildPanes()`.**
  A freshly constructed `SGraphEditor` has no geometry yet, so `JumpToNode`
  centres against nothing and appears to do nothing. `RequestJumpToNode` defers
  through an active timer.

## Internal Graph Document

The in-memory graph document is the source of truth for the copy formatters.
Markdown, ASCII, and Mermaid are views of that document, not independent graph
parsers.

The internal document must be:

- deterministic for the same graph state
- structured and machine-readable
- stable enough for source control and structural comparison
- explicit about internal edges and external boundary links
- independent of visual node placement unless placement is intentionally added
  as optional metadata

Sort collections with stable keys before serialization. Never rely on pointer
order, UObject iteration order, selection order, or hash-map order.

Do not silently change canonical field meanings. When compatibility handling is
eventually required, document it and add tests before changing the format.

## Editor Integration

All user actions belong under one `GraphXPort` submenu in Blueprint graph
context menus.

Support graph-node context menus first. Direct My Blueprint tree integration
must use a supported public extension point; do not patch engine source or rely
on private `SMyBlueprint` implementation details.

User-facing operations must report clear failures for missing Blueprint
editors, graphs, selections, clipboard access, or file writes.

## Safety

### Agent workflow for requested Blueprint conflicts

Use GraphXPort's base/current/incoming evidence to review both sides, then official/custom Unreal MCP tools to integrate the intended changes. Never take incoming or outgoing/current wholesale or invoke Start from Incoming/Mine without explicit user confirmation for that asset and side; a general conflict-resolution request does not authorize discarding either side. Preserve existing unsaved work and user-confirmed contracts. Check relevant variables, interfaces, components and defaults as well as graphs. Search the shared MCP tool index and both live catalogs before declaring a tool unavailable. Verify edited nodes/links/layout, compile the target, save, and verify clean package state before staging only the reviewed asset and confirming no unmerged Git stages remain. Do not claim runtime verification from compilation, and do not commit unless requested. Host-project restrictions on Blueprint access and computer use still apply.

Export operations must not modify Blueprint assets, compile Blueprints, save
packages, or run PIE.

**Beautify Selection is the one explicit, intentional exception.** It is a
manual, user-initiated command that repositions the nodes the user has
selected (`NodePosX`/`NodePosY` only) to improve readability. It must stay
within these bounds:

- Position-only: never touch pins, connections, node properties, or logic.
  (A fan-out reroute-insertion feature was implemented, live-tested, and
  deliberately rejected/reverted - do not reintroduce node/connection mutation
  in Beautify without this section being updated first and explicit sign-off.)
- Selection-only: never move a node the user did not select.
- Transactional and undoable: wrap changes in `FScopedTransaction`, call
  `Node->Modify()` before each change, and mark the Blueprint package dirty -
  follow normal Unreal editor edit conventions here, not the export path's
  read-only guarantee.
- Deterministic and safe on malformed input: cyclic or disconnected selections
  must still terminate and produce a valid layout (see `GraphXPortBeautifier`'s
  visited-set and depth-cap guards, mirroring the ASCII formatter's).
- No new mutation surface without updating this section first. If a future
  change needs to touch anything beyond node position, treat it as a new,
  separately reviewed exception - do not fold it into Beautify silently.

**GraphXPortMerge conflict resolution is the second explicit exception.** The
merge window edits and saves the real working-tree asset and stages it with git.
It must stay within these bounds:

- The window shows three panes: a left reference that toggles between **Base** and
  **Mine**, an editable **Result**, and **Theirs**. Only Result is editable, and it
  is always the real, live `/Game` asset - the only thing ever saved. Base, Mine
  and Theirs are diff-only revisions and must stay read-only.
  Read-only is enforced three ways, and all three are required: `IsEditable(false)`,
  `DisplayAsReadOnly(true)`, and clearing the graph's own `UEdGraph::bEditable`.
  The widget attribute alone does not gate pin and connection editing.
- Do not conflate "Mine" with "the thing being edited". Mine is git stage 2 and is
  read-only; Result is the working tree. Outside a conflict there are no git stages
  at all, so comparisons must go through `LocalChangeSide()` - stage 2 in a
  conflict, the working tree otherwise. Reading `MineSide` directly silently
  reports zero changes for an ordinary modified file.
- Clearing `bEditable` is only ever done on diff-only revisions, which are never
  saved. Never clear it on the real asset's graph.
- Diff-only revisions are never force-unloaded. Hold them alive from an
  `FGCObject` while displayed and let GC reclaim them; unloading a package whose
  graph a live `SGraphEditor` still points at crashes the editor. Reference them
  as `TObjectPtr`, not raw pointers - the raw-pointer `AddReferencedObject`
  overload is documented to crash under incremental GC.
- **Start from Mine / Start from Incoming** is the one place the working-tree file
  is overwritten with a revision's bytes (re-added with explicit sign-off on
  2026-09-11, after an earlier version was removed). It lets the user build Result
  on whichever side changed more and copy the smaller side's changes into it. Bounds:
  - Only in a conflict, only from stage 2 or 3, and only after a confirmation dialog
    that says when saved or unsaved merge edits will be lost. It never stages;
    **Save & Mark Resolved** is still the only way a conflict is marked resolved.
  - `FGraphXPortTempPackage::ReplaceWorkingFileWithRevision` follows the engine's
    source-control revert order: `ResetLoaders` to release the file handle, write
    to a sibling `.gxptmp` file and move it over, then `UPackageTools::ReloadPackages`.
    Writing without detaching the linker fails because the editor holds the file
    open - that is why the first version failed. Never `UnloadPackages` instead.
  - Tear down every pane and drop every reference into the live asset *before* the
    reload, because reloading replaces its objects.
  - Refuse a level that is open in the editor.
  - The toolbar shows what Result's file currently matches (Mine, Incoming, or saved
    merge edits) by comparing `git hash-object` with the stage blob ids, so the
    state is visible rather than remembered.
- "Accept current/incoming" (take a side and mark resolved in one click) stays
  removed. Wholesale side-taking without review belongs in the user's git tool.
- Resolution is: the user edits Result with ordinary graph editing - copying nodes
  from any read-only pane and pasting into Result - then **Save & Mark Resolved**
  compiles, saves, and runs `git add`. A failed compile must abort the whole action
  - never stage a Blueprint that does not compile, because a conflict marked
  resolved stops being looked at.
- Copy is bound on every pane; paste and delete only on Result.
- Closing the tab purges loaded revisions: tear down the panes and drop every
  reference first, then collect garbage, then delete the temp files. That order
  matters - linkers must close before the loose files can be deleted, and
  collecting while a pane still points at a revision's graph crashes the editor.
  The purge exists because revisions are real assets while loaded, so their
  classes and structs otherwise appear in class/struct pickers.
- Staging is the only git write. Never commit, push, checkout, reset, or
  otherwise move the user's branch state from inside the editor. Start from
  Mine/Incoming writes the file itself from `git show` output; it runs no git
  command that changes the index or refs.
- Only **Unstaged** entries are editable. **Staged** (HEAD vs index) and **History**
  (a commit's first parent vs the commit) load both sides as diff-only revisions:
  no Save button, no paste, nothing written. Staging, unstaging, and anything else
  that changes the index stays in the user's git tool.

The bundled `GraphXPortConflictToolset` module may expose explicit
preview/apply/save/stage workflows. Its mutations delegate to UnrealMCP's transactional
patch engine; GraphXPort does not depend on that integration. Staging through the
exported reader service is only requested after successful compile/save, unchanged
Git-stage identities, a clean package and a matching saved-file hash. Read-only revision
loads may be cached by blob identity with bounded retention; never force-unload them
or purge files held by the merge window.

Future import or merge operations must be:

- previewable before mutation
- transactional where Unreal APIs permit it
- undoable through editor transactions
- explicit about unsupported nodes and unresolved references
- non-destructive by default
- blocked from silently overwriting user graph changes

Never deserialize arbitrary UObject classes from external data without a strict
allowlist and validation.

## Conflict Workflow

This shipped differently from the original canonical-snapshot plan, and
deliberately so: rather than diffing exported snapshots and replaying a merge plan,
the merge window loads the actual Blueprint revisions and shows them in real
`SGraphEditor` panes, so comparison and editing use the engine's own graph
rendering and editing rather than a reimplementation.

Current behaviour:

- The file list has three sections: **Unstaged Changes**, **Staged Changes**, and
  **History** (100 commits per page, "Load more" for older ones). A file staged and
  then edited again appears in both of the first two, each with its own diff.
- Conflicts (`U`) show Base/Mine (toggle) / Result / Theirs; unstaged edits
  (`M`, `R`) show HEAD / Working tree; staged entries show HEAD / Staged; a file
  under a commit shows Parent / Commit. Merge commits compare against their first
  parent (`--diff-merges=first-parent`).
- Data table, enum and struct rows keep their authored order - it is serialized,
  so a pure reorder is reported as its own "Order changed" difference.
- Node-level differences come from `FGraphDiffControl::DiffGraphs`, and each side
  is diffed against the common ancestor so a pane shows what *that* side changed.
  Results are handed to `SGraphEditor`'s own `DiffResults` argument, so
  highlighting is engine-rendered.
- Non-graph differences are surfaced too, because a change often lives outside
  the graphs: variables (added/removed/retyped/default changed), class defaults
  and component templates (via `DiffUtils::CompareUnrelatedObjects`), components
  (added/removed/class changed), and graphs present on only one side.
- Navigation uses typed destinations (`EGraphXPortView`), never localized labels.
  The flat roots are My Blueprint, Defaults, Class Settings, Components, and each
  graph; differences appear directly under their section, without a duplicate
  inventory/Changes tree. Keep unchanged and new-asset graphs accessible.
- Native public `SMyBlueprint`, `SKismetInspector`, `FDetailsDiff`, and `FSCSDiff`
  provide Unreal-style panes. Member metadata/class settings/graph inspectors
  are read-only; Result default/component details retain editing. Guard FSCSDiff
  construction for non-actor or missing generated classes. Retain native widget
  owners per side and release them after pane teardown, before revision references.
- Search and Changes only filter the same row identities; Prev/Next navigates
  differences, not inferred conflicts. Placeholders are never counted as changes.
- Enums, structs and data tables have no graphs and render as a column grid with
  per-cell change highlighting.
- Change rows retain their Mine/Theirs origin. Selecting a graph change focuses
  every pane on that node, each resolving it by `NodeGuid` in its own revision.
- Merging is done by hand: copy nodes from any pane, paste into Result only.

Diffs compare semantic graph identity, node properties, pin defaults, and edges
rather than exported text or node screen positions.

**There is deliberately no automatic conflict detection.** It was built twice and
removed twice. First by matching `FDiffSingleResult::DisplayString`, which is
unreliable because two incompatible changes can produce identical text - both
sides wiring a different node onto the same pin each read "Added Link to 'then'"
and so compared as agreement. Then as a proper three-way comparison of projected
graph state, which was correct in principle but produced false positives the user
judged not worth the noise: diff-only revisions load into per-revision temp
packages, so `PinSubCategoryObject` paths differ between sides and identical pins
compare as changed.

Do not reintroduce conflict analysis, Previous/Next Conflict navigation, or a
Conflicts section without explicit sign-off. The user's stated preference is to
see both sides' changes and decide by hand.

The shared graph document is used only as an internal source for the copy
formatters. The merge path operates on real Unreal assets and does not consume
copied output.

## Coding Standards

- Follow Unreal naming and ownership conventions.
- Prefer Unreal containers and value types.
- Avoid raw `new` and `delete`.
- Keep files focused; split source files that become difficult to review.
- Add comments only where editor API behavior or graph semantics are not clear.
- Keep user-visible strings concise and actionable.
- Keep UnrealMCP dependencies confined to GraphXPortConflictToolset.

## Verification

After C++ changes:

1. Build the host project's editor target for Win64 Development:
   `AR_GIS_MAPEditor` in DSM, or `UE_544_MCPEditor` in the dedicated test project.
2. Run the `GraphXPort.` automation group, including
   `GraphXPort.Export.SelectionAndWholeGraph`, `GraphXPort.Format.Ascii.*`,
   `GraphXPort.Beautify.*`, `GraphXPort.Merge.GitStatus.*`, and
   `GraphXPort.Merge.GitHistory.*`.
3. Verify export output remains deterministic.
4. For menu changes, manually verify the Blueprint node context submenu in the
   Unreal Editor when practical. For Beautify changes, manually verify in a
   live Blueprint graph that the layout is sane and that Ctrl+Z fully reverts
   both position and package dirty state.
5. For merge-window changes, manually verify against a real conflicted asset:
   the read-only panes stay non-editable (including pin connections), copy from a
   read-only pane pastes into Result, selecting a row focuses all panes, and no
   console window appears for any git call. Also check an ordinary modified file,
   which takes a different path through `LocalChangeSide()`. Much of this module
   cannot be covered by automation because it needs a real repository in a
   conflicted state.

**Confirm a build actually linked.** With Live Coding active, UBT prints
"Unable to build while Live Coding is active", performs zero link actions,
finishes in about two seconds and still reports success - leaving a stale DLL
that makes deleted code appear to still run. Look for a `Link [x64] ...dll` line,
not just "Total execution time".

Add focused automation coverage for serializer, formatter, boundary-link, and
future diff/import changes.

When the user prohibits running Unreal Editor, do not launch it (including
headless automation). Compile-only verification may use `-NoLink -NoHotReload
-NoHotReloadFromIDE`; report automation tests and the linked DLL as unverified
until a normal build/test run is permitted.

## Repository Hygiene

Track source, tests, documentation, and the `.uplugin` manifest.

Do not commit plugin-local generated output such as:

- `Binaries/`
- `Intermediate/`
- `Saved/`
- IDE-generated files
