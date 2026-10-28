"""Regenerate the embedded Metal shaders for both platforms."""
from pathlib import Path
import json
p=Path(__file__).resolve().parents[1]/'nvn/nvn_present_fx.metal'
p.with_name('nvn_present_fx_shaders.h').write_text('// Generated from nvn_present_fx.metal by dm/gfx/tools/present_shaders.py.\nstatic const char *dm_present_fx_msl =\n'+''.join(json.dumps(s+'\n')+'\n' for s in p.read_text().splitlines())+';\n')
