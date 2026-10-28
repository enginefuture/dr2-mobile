"""Independent GPU tests for real intermediate motion, dark occlusion and toggles.

Adapted from the ns2mac Castlevania regression suite. This measures the Mac GPU;
phone presentation rate and thermal cost require a separate device run.
"""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(os.environ.get('DM_ROOT', str(Path.home() / 'dm-work'))) / 'run/present-fx-tests'
OUT.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable,str(ROOT/'dm/gfx/tools/present_shaders.py')],check=True)
exe = OUT / 't_present_fx'
subprocess.run(['clang', '-fobjc-arc', '-O2', '-I', str(ROOT/'dm/gfx/nvn'),
                str(ROOT/'dm/gfx/tests/t_present_fx.m'), str(ROOT/'dm/gfx/nvn/nvn_present_fx.m'),
                '-framework', 'Metal', '-framework', 'MetalFX', '-framework', 'Foundation',
                '-framework', 'QuartzCore', '-framework', 'ImageIO', '-framework', 'CoreGraphics',
                '-o', str(exe)], cwd=ROOT, check=True)
for args in ([], ['complex', 'cold'], ['mobile', 'cold'], ['native']):
    subprocess.run([str(exe), *args], cwd=ROOT, env=dict(os.environ, MTL_DEBUG_LAYER='1'),
                   check=True, timeout=60)

display=OUT/'t_present_display'
subprocess.run(['clang','-fobjc-arc','-O2',str(ROOT/'dm/gfx/tests/t_present_display.m'),
                '-framework','Metal','-framework','MetalFX','-framework','Foundation',
                '-framework','QuartzCore','-o',str(display)],cwd=ROOT,check=True)
subprocess.run([str(display)],cwd=ROOT,env=dict(os.environ,MTL_DEBUG_LAYER='1'),check=True,timeout=30)
