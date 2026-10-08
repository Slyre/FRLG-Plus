"""Summarize isolated compiled-ROM callback costs, not visible frame latency."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
rows = [json.loads(line[7:]) for line in (root / 'build/tests/sliced-timing.log').read_text().splitlines()
        if line.startswith('SLICED ')]
assert len(rows) == 42
assert all(0 < row['peakCycles'] < 280896 and row['calls'] < 200 for row in rows)
result = {
    'fixture': 'Two actual mGBA cores, compiled Thumb ROM, isolated calls with IRQs disabled',
    'samples': len(rows),
    'largestCallbackMs': max(row['peakEmulatedMs'] for row in rows),
    'largestFixtureStackBytes': max(row['stackBytesBelowFixtureSP'] for row in rows),
    'perRegistrySize': {},
    'excluded': ['WAN transport', 'Lua', 'browser refresh', 'other game-frame work',
                 'physical death cleanup', 'PC team movement', 'distinct-PC pacing'],
    'rows': rows,
}
for count in (1, 6, 30, 87, 88):
    selected = [row for row in rows if row['pairs'] == count]
    result['perRegistrySize'][str(count)] = {
        'largestCallbackMs': max(row['peakEmulatedMs'] for row in selected),
        'largestTotalCpuMs': max(row['totalEmulatedMs'] for row in selected),
        'largestCallbackCount': max(row['calls'] for row in selected),
        'largestFixtureSubsequentFrames': max(row['calls'] for row in selected) - 1,
    }
(root / 'build/tests/sliced-apply-results.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({key: value for key, value in result.items() if key != 'rows'}, indent=2))
