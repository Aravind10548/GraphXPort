"""Exercise only the disposable GXPValidation fixture on localhost:8011.

Start its editor with setup_fixture.py first. Never use this against DSM.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time
import urllib.request
import uuid

parser = argparse.ArgumentParser()
parser.add_argument('--project-root', required=True, type=Path)
args = parser.parse_args()
root = args.project_root.resolve()
assert (root / 'GXPValidation.uproject').is_file(), 'Disposable GXPValidation project required'
URL = 'http://127.0.0.1:8011/mcp'
TOOLSET = 'GraphXPortConflictToolset.GraphXPortConflictToolset'
PATH = 'Content/Fixture/BP_ConflictFixture.uasset'
session = None
request_id = 0


def rpc(method, params):
    global session, request_id
    request_id += 1
    headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}
    if session:
        headers['Mcp-Session-Id'] = session
    req = urllib.request.Request(URL, json.dumps({'jsonrpc': '2.0', 'id': request_id,
                                                'method': method, 'params': params}).encode(), headers)
    with urllib.request.urlopen(req, timeout=45) as response:
        text = response.read().decode()
        session = response.headers.get('Mcp-Session-Id', session)
    if text.startswith(('event:', 'data:')):
        text = next(line[5:].strip() for line in text.splitlines() if line.startswith('data:'))
    result = json.loads(text)
    assert 'error' not in result, result
    return result['result']


def call(name, **arguments):
    result = rpc('tools/call', {'name': 'call_tool', 'arguments': {
        'toolset_name': TOOLSET, 'tool_name': name, 'arguments': arguments}})
    assert not result.get('isError'), result
    obj = json.loads(result['content'][0]['text'])
    value = obj.get('returnValue')
    return (json.loads(value) if isinstance(value, str) else value), len(json.dumps(result).encode())


def other(toolset, name, **arguments):
    result = rpc('tools/call', {'name': 'call_tool', 'arguments': {
        'toolset_name': toolset, 'tool_name': name, 'arguments': arguments}})
    assert not result.get('isError'), result
    return json.loads(result['content'][0]['text']).get('returnValue')


def git(*params, data=None):
    return subprocess.run(['git', '-C', str(root), *params], input=data, check=True,
                          capture_output=True).stdout.decode().strip()


rpc('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                   'clientInfo': {'name': 'merge-benchmark', 'version': '1'}})
listed, _ = call('ListConflicts')
# Verify the server is the expected disposable repository BEFORE any mutation.
assert Path(listed['repository']).resolve() == root
assert root.name.startswith('GraphXPortToolsetValidation-')
assert (root / 'fixture_ready.json').is_file()
fixture_ids = json.loads((root / 'fixture_ready.json').read_text())['hashes']
remove_fixture_entry = f'0 {"0" * 40}\t{PATH}\n'
initial_stages = remove_fixture_entry + ''.join(f'100644 {oid} {stage}\t{PATH}\n'
                                                for stage, oid in enumerate(fixture_ids, 1))
git('update-index', '--index-info', data=initial_stages.encode())

# Reproduce the previous review's seven calls. These intentionally run first.
start = time.perf_counter()
legacy = [call('ListConflicts')]
for side in ['base', 'current', 'incoming']:
    legacy.append(call('InspectRevision', path=PATH, revision=side, graph='', offset=0, limit=200))
for before, after in [('base', 'current'), ('base', 'incoming'), ('current', 'incoming')]:
    legacy.append(call('CompareRevisions', path=PATH, before=before, after=after,
                       graph='', offset=0, limit=200))
legacy_ms = (time.perf_counter() - start) * 1000
assert all(result['ok'] for result, _ in legacy), [result for result, _ in legacy if not result['ok']]
start = time.perf_counter()
review, session_bytes = call('OpenBlueprintMerge', path=PATH)
session_ms = (time.perf_counter() - start) * 1000
assert review['ok'], review
sid = review['sessionId']
incoming = [row for row in review['incoming']['rows']
            if row.get('description', '').startswith('Moved Node')]
outgoing = [row for row in review['outgoing']['rows']
            if row.get('description', '').startswith('Moved Node')]
assert len(incoming) == len(outgoing) == 1
new_node, current_node = incoming[0]['afterNode'], outgoing[0]['afterNode']
patch = {'graphName': 'EventGraph', 'nodes': [
    {'id': 'incomingMove', 'kind': 'existingNode', 'nodeGuid': new_node['guid'],
     'positionX': new_node['positionX'], 'positionY': new_node['positionY']},
    {'id': 'retainCurrent', 'kind': 'existingNode', 'nodeGuid': current_node['guid'],
     'positionX': current_node['positionX'], 'positionY': current_node['positionY']},
    {'id': 'comment', 'kind': 'comment', 'comment': 'Retain both fixture changes',
     'positionX': 700, 'positionY': 100, 'width': 800, 'height': 300,
     'color': {'r': 0.04, 'g': 0.18, 'b': 0.3}, 'showBubbleWhenZoomed': True}]}

full, full_bytes = call('PreviewMergeResolution', sessionId=sid,
                        patchJson=json.dumps(patch), detailLevel='full')
compact, compact_bytes = call('PreviewMergeResolution', sessionId=sid,
                              patchJson=json.dumps(patch), detailLevel='summary')
assert full['success'] and compact['success']
assert all('pins' not in node for node in compact['nodes'])
assert compact_bytes < full_bytes
existing = next(node for node in compact['nodes'] if node['id'] == 'incomingMove')
assert not existing['wouldCreate'] and uuid.UUID(existing['nodeGuid']).int != 0

# A changed Git stage must invalidate a plan before it edits anything.
ids = json.loads((root / 'fixture_ready.json').read_text())['hashes']


def set_stages(stage3):
    lines = remove_fixture_entry + ''.join(f'100644 {oid} {stage}\t{PATH}\n'
                                           for stage, oid in enumerate([ids[0], ids[1], stage3], 1))
    git('update-index', '--index-info', data=lines.encode())


set_stages(ids[0])
stale, _ = call('ApplyMergeResolution', sessionId=sid, planId=compact['planId'], save=True, stage=True)
assert not stale['ok'] and 'changed' in stale['error']
set_stages(ids[2])
start = time.perf_counter()
preview, preview_bytes = call('PreviewMergeResolution', sessionId=sid, patchJson=json.dumps(patch))
preview_ms = (time.perf_counter() - start) * 1000
assert preview['success'], preview
plan = preview['planId']
refused, _ = call('ApplyMergeResolution', sessionId=sid, planId=plan, save=False, stage=True)
assert not refused['ok']
start = time.perf_counter()
applied, applied_bytes = call('ApplyMergeResolution', sessionId=sid, planId=plan, save=True, stage=True)
apply_ms = (time.perf_counter() - start) * 1000
assert all(applied[key] for key in ['success', 'compileSucceeded', 'saved', 'staged', 'workflowSucceeded']), applied
assert not git('ls-files', '-u', '--', PATH)
retry, _ = call('ApplyMergeResolution', sessionId=sid, planId=plan, save=True, stage=True)
assert retry == applied, 'Retry must return the saved receipt without another edit/save/stage'
result_nodes = {node['id']: node for node in applied['nodes']}
assert result_nodes['incomingMove']['positionX'] == 400
assert result_nodes['retainCurrent']['positionX'] == 200
call('CloseBlueprintMerge', sessionId=sid)

# A user edit after preview must stop Apply, and dirty assets must not open a session.
set_stages(ids[2])
dirty_review, _ = call('OpenBlueprintMerge', path=PATH)
assert dirty_review['ok']
dirty_sid = dirty_review['sessionId']
dirty_plan, _ = call('PreviewMergeResolution', sessionId=dirty_sid, patchJson=json.dumps(patch))
assert dirty_plan['success']
graph = {'refPath': '/Game/Fixture/BP_ConflictFixture.BP_ConflictFixture:EventGraph'}
bp_tools = 'editor_toolset.toolsets.blueprint.BlueprintTools'
nodes = other(bp_tools, 'find_nodes', graph=graph, title='Event Tick')
assert len(nodes) == 1
before_disk = git('hash-object', '--', PATH)
other(bp_tools, 'set_node_position', node=nodes[0], pos={'x': 450, 'y': 500})
blocked, _ = call('ApplyMergeResolution', sessionId=dirty_sid, planId=dirty_plan['planId'], save=True, stage=True)
assert not blocked['ok'] and 'changed' in blocked['error']
blocked_open, _ = call('OpenBlueprintMerge', path=PATH)
assert not blocked_open['ok'] and 'unsaved' in blocked_open['error']
assert git('hash-object', '--', PATH) == before_disk
assert git('ls-files', '-u', '--', PATH), 'Dirty-state refusal must not stage'
call('CloseBlueprintMerge', sessionId=dirty_sid)
other(bp_tools, 'set_node_position', node=nodes[0], pos={'x': 400, 'y': 500})
other(bp_tools, 'compile_blueprint', blueprint={'refPath': '/Game/Fixture/BP_ConflictFixture.BP_ConflictFixture'})
assert other('editor_toolset.toolsets.asset.AssetTools', 'save_assets', asset_paths=['/Game/Fixture/BP_ConflictFixture'])

report = {
    'scope': 'Disposable fixture; local tool execution only, excluding editor startup/model reasoning. '
             'Legacy reads run first, so timing includes their cold-load cost; no eight-minute speedup claim.',
    'legacy_review': {'calls': 7, 'response_bytes': sum(size for _, size in legacy),
                      'elapsed_ms': round(legacy_ms, 2)},
    'session_review': {'calls': 1, 'response_bytes': session_bytes, 'elapsed_ms': round(session_ms, 2)},
    'preflight': {'full_bytes': full_bytes, 'summary_bytes': compact_bytes,
                  'response_reduction_percent': round(100 * (1 - compact_bytes / full_bytes), 1)},
    'three_call_workflow': {'calls': 3, 'response_bytes': session_bytes + preview_bytes + applied_bytes,
                            'elapsed_ms': round(session_ms + preview_ms + apply_ms, 2)},
    'checks': ['official MCP dispatch', 'three stages', 'stale-stage refusal', 'stage requires save',
               'compile/save/stage', 'idempotent retry', 'both edits retained', 'summary IDs',
               'dirty-after-preview refusal', 'dirty-open refusal', 'refusal leaves disk/index untouched']}
(root / 'ConflictWorkflowBenchmark.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
