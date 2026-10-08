"""Summarize isolated compiled-ROM cycle measurements without claiming WAN latency."""
import json
from pathlib import Path

root=Path(__file__).resolve().parents[2]
def read(name):
    rows=[json.loads(line[7:]) for line in (root/'build/tests'/name).read_text().splitlines() if line.startswith('TIMING ')]
    assert len(rows)==42
    assert all(1000<r['cycles']<16777216*30 for r in rows)
    return rows
before=read('apply-timing-before.log');after=read('apply-timing.log')
assert [(r['role'],r['pairs'],r['change']) for r in before]==[(r['role'],r['pairs'],r['change']) for r in after]
summary=[]
for size in (1,6,30,87,88):
    b=[r for r in before if r['pairs']==size];a=[r for r in after if r['pairs']==size]
    summary.append(dict(pairs=size,beforeMaxEmulatedMs=max(r['emulatedMs'] for r in b),
                        afterMaxEmulatedMs=max(r['emulatedMs'] for r in a)))
changes=[dict(role=b['role'],change=b['change'],beforeMs=b['emulatedMs'],afterMs=a['emulatedMs'])
         for b,a in zip(before,after) if a['pairs']==88 and a['change']!='unchanged']
report=dict(date='2026-10-07',samplesPerROM=42,saveVersion=3,
            cpuClockHz=16777216,cyclesPerFrame=280896,registries=summary,fullCapacityChanges=changes,
            maximumFixtureStackBytes=max(r['stackBytesBelowFixtureSP'] for r in after),
            allMeasuredAppliesBelow100ms=all(r['emulatedMs']<100 for r in after),
            allMeasuredOrdinaryChangesBelow100ms=all(r['emulatedMs']<100 for r in after if r['change'] in ('one-capture','one-death','death-confirmation')),
            method='Release mGBA wrapping scheduler clock; isolated compiled Thumb routine with IRQs disabled. Matching ROM symbols, identical metadata fixtures, both roles. Host stepping time is excluded.',
            exclusions=['WAN and Lua transport','Browser refresh','Linked-death body cleanup','Accepted-team PC movement','Other game-loop work and IRQ handlers','Real two-PC frame pacing'],
            limitations='Metadata-only registry, no 88 boxed bodies. Sample maxima are not population p95 or a universal frame-pause guarantee. Turbo and slow-host wall time are not measured.')
(root/'build/tests/apply-timing-results.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
