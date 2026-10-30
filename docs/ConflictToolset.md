# GraphXPort Conflict Toolset (UE 5.8)

The MCP conflict toolset is the bundled `GraphXPortConflictToolset` editor module in the GraphXPort plugin. Enable the Epic Toolset Registry and Model Context Protocol plugins and install UnrealMCP with `FUnrealMCPModule::ExecuteToolJson`, external variable support and compact patch results. Build with UE 5.8 and restart the editor. GraphXPort export and merge modules do not depend on MCP.

Epic Unreal MCP exposes the merge tools through Toolset Registry. Read-only revision inspection stays in GraphXPortMerge; mutation delegates to the custom UnrealMCP patch engine, which owns transactions, revision guards, compile-before-save and rollback. GraphXPort itself remains usable without either MCP implementation.

Discover `GraphXPortConflictToolset.GraphXPortConflictToolset` through Epic's `list_toolsets` and `describe_toolset`. These workflow tools are exposed by the official server; they are not independently mirrored into the custom adapter's catalog.

## Efficient workflow

1. `OpenBlueprintMerge(Path)` returns a session ID, working state and outgoing/incoming graph differences. The working asset must have no unsaved edits and all three Git stages must exist. Overlap candidates identify nodes edited on both sides, excluding pure layout movement; they do not prove semantic conflicts.
2. Optional `InspectMergeNodes(SessionId, Side, Graph, NodeGuidsJson)` returns only 1..100 selected node GUIDs, their pins, defaults, connections, positions and owner/member identities. `NodeGuidsJson` is a JSON string array. Side is base/current/incoming.
3. `PreviewMergeResolution(SessionId, PatchJson, DetailLevel="summary")` validates an explicitly authored `ApplyBlueprintGraphPatch` plan and stores it under a returned plan ID. The server fixes the target asset to the session, captures the live graph revision, and preflights without editing. Use `full` only when complete pin diagnostics are needed.
4. `ApplyMergeResolution(SessionId, PlanId, Save=false, Stage=false)` applies the stored patch. Compilation precedes optional saving; staging requires explicit Save=true and Stage=true. Stage changes, working-file changes, unsaved edits and changed graph revisions are rejected. Compile failure uses native rollback. The result separately reports compile, save and staging outcomes; `workflowSucceeded` is false if requested staging fails.
5. `CloseBlueprintMerge(SessionId)` releases the review. At most two sessions can be open; idle sessions expire after ten minutes. Repeat Apply with the same plan and flags to receive its stored receipt without another edit/save/stage. Different flags require a new review.

Patch JSON must provide `graphName` and a bounded explicit node/link plan using the native patch schema. Its asset path, operation ID, patch ID and expected revision are assigned by the workflow. The tool does not choose a resolution automatically or import every incoming change blindly. A no-change patch is not automatically staged when the native engine skips saving it; the receipt reports the incomplete staging request. This case still needs explicit compile/save and manual staging. It supports the existing native node kinds plus comments with colour, dimensions and zoom bubbles. External variable get/set nodes accept `ownerClassPath`, avoiding separate discovery/creation through Epic's node palette.

## Legacy read tools

- `ListConflicts`: unmerged paths and stage 1/2/3 blob identities. Empty IDs mean absent stages.
- `InspectRevision(Path, Revision, Graph, Offset, Limit)`: graph inventory when Graph is empty, otherwise nodes and pins.
- `CompareRevisions(Path, Before, After, Graph, Offset, Limit)`: directional Unreal graph differences, including graph additions/removals. Empty Graph selects all graphs.

Use base->current for outgoing and base->incoming for incoming. These names mean Git stages 1/2/3; branch ownership during rebase can differ from an ordinary merge. Revisions also accept HEAD and full 40/64-character commit hashes. Paths must be repository-relative .uasset/.umap paths with forward slashes. Detail pages clamp to 1..200 items and include total/hasMore/nextOffset. Legacy pages are fresh reads; verify their blob IDs stay constant.

## Scope and cache

Graph evidence covers nodes, pins, positions and engine graph differences. It does not cover variable declarations, component trees, class defaults or arbitrary asset properties. Zero graph differences do not establish whole-asset equivalence. Add/delete conflicts and reviews exceeding 200 differences on a side require the legacy/manual workflow. Missing dependencies can prevent historical package loading.

The loader caches up to six revision packages by repository/path/blob identity, with five-minute idle expiry. This avoids repeated Git extraction/package loading during comparisons and pagination. Eviction drops references without force-unloading packages or purging files held by the merge UI. Historical packages remain read-only and may receive skeleton classes for inspection. No branch movement, checkout, commit or push is exposed.

## Validation and benchmark

Measured results and limits: [Tests/BENCHMARK.md](../Tests/BENCHMARK.md).

UE 5.8 isolated-host compilation and linking succeeded. The final GraphXPort + native Blueprint authoring groups passed 34 tests, including cache GC retention, external-variable/summary and authored-comment regressions. The focused tests were also invoked through the custom adapter. The official HTTP workflow benchmark checks stage guards, dirty-state refusal, explicit save/stage, compile/save/stage, retry receipts and retention of both fixture edits.

`Tests/setup_fixture.py` creates three real Git-stage Blueprint blobs in a disposable `GraphXPortToolsetValidation-*` / GXPValidation host only. Start that editor with Python, EditorToolset, ModelContextProtocol and this plugin enabled, run the setup script at startup, and serve MCP on port 8011. Then run:

```powershell
python Tests/benchmark_workflow.py --project-root <absolute-disposable-host-path>
```

The benchmark rejects other repository roots before mutation. It writes `ConflictWorkflowBenchmark.json` inside the disposable host. Its calls/bytes/time exclude editor startup and model reasoning; the legacy read pattern runs first, so cold-load timing is not a controlled comparison. No claim is made about an eight-minute real-asset task taking the fixture's execution time.

### DSM activation

On 2026-10-30 the canonical DSM UE 5.8 editor build succeeded and the editor was restarted. Epic MCP advertised all eight conflict-toolset methods and listed the 14 remaining unmerged files. A read-only custom-engine patch preflight returned the existing node GUID, wouldCreate=false and no pin dump; the player-controller package remained clean and up to date. No additional conflict was resolved during activation.
