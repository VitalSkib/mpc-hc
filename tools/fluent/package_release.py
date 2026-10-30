"""Package the complete native installer plus an optional existing-portable update."""
import argparse, hashlib, json, os, re, struct, subprocess, zipfile, shutil
from pathlib import Path

def sha(data): return hashlib.sha256(data).hexdigest()

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--exe', required=True)
    p.add_argument('--setup', required=True)
    p.add_argument('--version', required=True)
    p.add_argument('--revision', type=int, required=True)
    p.add_argument('--channel', choices=['stable', 'beta'], default='stable')
    args = p.parse_args()
    parts = args.version.split('.')
    if len(parts) != 4 or not all(x.isdecimal() and 0 <= int(x) <= 65535 for x in parts):
        raise ValueError('Expected four-part Windows version')
    if int(parts[3]) != args.revision: raise ValueError('PE version and updater revision disagree')
    exe = Path(args.exe).read_bytes()
    if exe[:2] != b'MZ' or struct.unpack_from('<H', exe, struct.unpack_from('<I', exe, 60)[0]+4)[0] != 0x8664:
        raise ValueError('Expected x64 player')
    for marker in (b'Native geometry diagnostics v1', b'UI state diagnostics v2', b'MpcModernMenu'):
        if marker in exe: raise ValueError('Diagnostic or abandoned menu integration in release')
    source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    config = json.loads(Path('tools/fluent/project.json').read_text())
    manifest = {'product': 'MPC-HC-Fluent', 'version': args.version, 'channel': args.channel,
                'source_commit': source, 'upstream_tag': config['upstream_tag'],
                'upstream_commit': config['upstream_commit'], 'exe_sha256': sha(exe),
                'source_url': f'https://github.com/VitalSkib/mpc-hc/tree/{source}',
                'build_run': os.environ.get('GITHUB_RUN_ID', '')}
    setup = Path(args.setup)
    if not setup.is_file() or setup.read_bytes()[:2] != b'MZ': raise ValueError('Native installer is required')
    manifest['installer_name'] = setup.name
    manifest['installer_sha256'] = sha(setup.read_bytes())
    manifest['dependencies'] = json.loads(Path('tools/fluent/dependencies.json').read_text(encoding='utf-8-sig'))
    out = Path('fluent-release'); out.mkdir(exist_ok=True)
    shutil.copyfile(setup, out/setup.name)
    name = f'MPC-HC-Fluent-{args.version}-x64-{args.channel}-update.zip'
    with zipfile.ZipFile(out/name, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('payload/mpc-hc64.exe', exe)
        z.writestr('manifest.json', json.dumps(manifest, indent=2))
        z.write('tools/fluent/Update-Fluent.ps1', 'Update-Fluent.ps1')
        z.write('COPYING.txt', 'COPYING.txt')
        z.writestr('README.txt', 'MPC-HC Fluent x64 update package. Existing portable player required.\n'
            'Run Update-Fluent.ps1 -TargetDirectory "path to your portable player" in PowerShell.\n'
            'The player must be closed. No process is killed and no player is launched.\n'
            'Only EXE and existing BrowserRouting integrity hash are updated. INI, icons, DLLs and EPG remain intact.\n'
            'Three previous EXE backups are retained in .fluent-backups.\n'
            'Source including build scripts and upstream attribution: '+manifest['source_url']+'\n'
            'Clone that commit recursively for corresponding library sources. GPLv3+, see COPYING.txt.\n')
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (out/'SHA256SUMS.txt').write_text(sha((out/name).read_bytes())+'  '+name+'\n'+manifest['installer_sha256']+'  '+setup.name+'\n')
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a') as f: f.write(f'version={args.version}\n')

if __name__ == '__main__': main()
