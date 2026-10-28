from pathlib import Path
import subprocess
import sys

launcher = Path(__file__).resolve().parents[1] / 'Update.command'
sys.exit(subprocess.call(['/bin/sh', str(launcher), *sys.argv[1:]]))
