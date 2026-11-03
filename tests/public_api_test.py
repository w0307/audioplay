import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

for scenario in ('accepted', 'disabled', 'missing', 'invalid'):
    with tempfile.TemporaryDirectory(prefix='audioplay-api-') as directory:
        root = pathlib.Path(directory)
        binary = root / 'api_test'
        shutil.copy2(sys.argv[1], binary)
        config = root / 'config'
        config.mkdir()
        if scenario != 'missing':
            data = {'audio': {'enabled': scenario != 'disabled'}, 'policy': {'mode_prompt_enabled': False}, 'voices': {}}
            (config / 'audioplay.json').write_text('{' if scenario == 'invalid' else json.dumps(data))
        subprocess.run([str(binary), scenario], cwd='/', timeout=5, check=True)
print('Public API: accepted/disabled/missing/invalid configuration and exit cleanup passed')
