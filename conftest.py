import sys
from pathlib import Path

# Source modules import each other by bare name (e.g. `from frequency_analysis import ...`), so src/python must be on sys.path — pytest's rootdir injection alone is not enough.
sys.path.insert(0, str(Path(__file__).parent / "src" / "python"))