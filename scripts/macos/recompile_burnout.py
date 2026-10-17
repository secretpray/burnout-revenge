#!/usr/bin/env python3
"""Generate Burnout PPC sources and refuse silently omitted instructions."""
import hashlib
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
xex = ROOT / 'game/burnout/files/default.xex'
expected = '5258c75d9ae649ed4fb71d4e2fe8808545db16092cfe764ea1a2504f5aae2e2d'
if not xex.is_file() or hashlib.sha256(xex.read_bytes()).hexdigest() != expected:
    raise SystemExit('This configuration requires the verified Burnout Revenge USA XEX; see docs/BURNOUT_REVENGE_MAC_PROGRESS.md')
tool = ROOT / 'tools/XenonRecomp/build/XenonRecomp/XenonRecomp'
header = ROOT / 'tools/XenonRecomp/XenonUtils/ppc_context.h'
output = ROOT / 'ppc/burnout'
output.mkdir(parents=True, exist_ok=True)
# XenonRecomp doesn't remove surplus units when its function count shrinks.
for source in output.glob('ppc_recomp.*.cpp'):
    source.unlink()
log = ROOT / 'build/logs/burnout-recompile.log'
log.parent.mkdir(parents=True, exist_ok=True)
with log.open('w') as stream:
    result = subprocess.run([str(tool), str(ROOT / 'config/burnout.toml'), str(header)],
                            cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
text = log.read_text(errors='replace')
if result.returncode or 'Usage:' in text or not list(output.glob('ppc_recomp.*.cpp')):
    raise SystemExit(f'Code generation failed; inspect {log}')
unknown = re.findall(r'Unrecognized instruction.*', text)
decode = re.findall(r'Unable to decode instruction .* at ([0-9A-Fa-f]+)', text)
# Known unresolved data-looking tail of this exact XEX. Keep it visible;
# don't let this allowance conceal decode failures elsewhere in the game.
unexpected = [address for address in decode if not 0x825C7910 <= int(address, 16) < 0x825C7D70]
if unknown or unexpected:
    raise SystemExit(f'Unsupported instructions or unexpected decode failures; inspect {log}')
print(f'Burnout generated: no unrecognized instructions; {len(decode)} decode diagnostics in the documented .text tail.')
print(f'Log: {log}')
