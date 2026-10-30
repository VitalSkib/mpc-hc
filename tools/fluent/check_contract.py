"""Small release guard for features repeatedly lost during earlier rollbacks."""
from pathlib import Path
import json

root = Path(__file__).resolve().parents[2]
required = {
    'src/mpc-hc/NativeChromeMotion.h': ['DetachControls()', 'NativeHeaderInput::AttachBorder'],
    'src/mpc-hc/NativeHeaderInput.h': ['HTTRANSPARENT', 'WM_NCLBUTTONDOWN'],
    'src/mpc-hc/PlayerToolBar.cpp': ['CompactControls()', 'm_overflowButtons', 'ID_TRANSPORT_OVERFLOW'],
    'src/mpc-hc/NativePlaylistPopupImpl.h': ['HasReadableSpace()', 'm_materialPainting', 'KillTimer(3)'],
    'src/mpc-hc/PlayerPlaylistBar.cpp': ['HasChannelHeader', 'm_streamEpg.Remember(m_programmeGuide)', 'RefreshProgrammeRows()'],
    'src/mpc-hc/NativeStreamEpg.h': ['LatestWorkQueue work', 'ValidateGuide(bytes,cancel)', 'ComApartment'],
    'src/mpc-hc/NativeStreamIdentity.h': ['entries.size()>512', 'rowBudget', 'MOVEFILE_REPLACE_EXISTING'],
}
for file, markers in required.items():
    text = (root/file).read_text(encoding='utf-8-sig')
    for marker in markers:
        assert marker in text, f'Review required: {file} lost {marker}'
assert not (root/'src/mpc-hc/NativeModernMenu.h').exists(), 'Abandoned WinUI integration'
assert 'NativeModernMenu::' not in (root/'src/mpc-hc/MainFrm.cpp').read_text(encoding='utf-8-sig')
workflow = (root/'.github/workflows/fluent-build.yml').read_text()
assert '/DMPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS' not in workflow
assert 'Compile optional WinUI' not in workflow
config = json.loads((root/'tools/fluent/project.json').read_text())
assert config['auto_install'] is False
print('Fluent retained-feature contract passed (source guard; not visual verification)')
