#!/usr/bin/env python3
"""Exercise the product composition refusal without starting a native host."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
source = (root / "CMakeLists.txt").read_text()
start = source.index('option(SCO_ASOP_HOST_COMPOSITION ')
end = source.index('set(SCO_HOST_SOURCE_DIR ', start)
gate = source[start:end]
with tempfile.TemporaryDirectory() as temp:
    project = pathlib.Path(temp)
    (project / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.30)\nproject(asop_gate NONE)\n" + gate
    )
    for host, selection in ((False, False), (True, False), (False, True), (True, True)):
        result = subprocess.run(
            ["cmake", "-S", str(project), "-B", str(project / f"b{int(host)}{int(selection)}"),
             f"-DSCO_ASOP_HOST_COMPOSITION={'ON' if host else 'OFF'}",
             f"-DSCO_ASOP_SELECTION_COMPOSITION={'ON' if selection else 'OFF'}"],
            text=True, capture_output=True, check=False,
        )
        assert (result.returncode == 0) == (not host and not selection), result.stdout + result.stderr
        if host or selection:
            assert "ASOP compositions are disabled" in result.stdout + result.stderr
print("ASOP composition gate: default accepted; host, selection, and combined opt-ins refused")
