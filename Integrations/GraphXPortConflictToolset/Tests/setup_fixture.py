"""Run inside a fresh disposable GXPValidation editor with Python + EditorToolset enabled."""
import json
from pathlib import Path
import subprocess
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools

root = Path(unreal.Paths.project_dir()).resolve()
assert root.name.startswith('GraphXPortToolsetValidation-')
assert (root / 'GXPValidation.uproject').is_file()
asset_path = '/Game/Fixture/BP_ConflictFixture'
file_path = 'Content/Fixture/BP_ConflictFixture.uasset'


def git(*params, data=None):
    return subprocess.run(['git', '-C', str(root), *params], input=data,
                          capture_output=True, check=True).stdout.decode().strip()


git('init')
if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
    assert unreal.EditorAssetLibrary.delete_asset(asset_path)
bp = BlueprintTools.create('/Game/Fixture', 'BP_ConflictFixture', unreal.Actor.static_class())
graph = BlueprintTools.get_graph(bp, 'EventGraph')
begin_play = BlueprintTools.find_nodes(graph, 'Event BeginPlay')[0]
tick = BlueprintTools.find_nodes(graph, 'Event Tick')[0]
hashes = []
for first_x, other_x in [(0, 0), (200, 0), (0, 400)]:
    begin_play.set_node_pos(unreal.IntPoint(first_x, 0))
    tick.set_node_pos(unreal.IntPoint(other_x, 500))
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    hashes.append(git('hash-object', '-w', str(root / file_path)))
# Keep the live working copy on the current side; the index holds three historical stages.
begin_play.set_node_pos(unreal.IntPoint(200, 0))
tick.set_node_pos(unreal.IntPoint(0, 500))
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
lines = f'0 {"0" * 40}\t{file_path}\n'
lines += ''.join(f'100644 {oid} {stage}\t{file_path}\n' for stage, oid in enumerate(hashes, 1))
git('update-index', '--index-info', data=lines.encode())
(root / 'fixture_ready.json').write_text(json.dumps({'hashes': hashes}))
