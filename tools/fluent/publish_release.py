"""Publish only a completed build; updater feed changes after assets are public."""
import base64, hashlib, json, os, subprocess, urllib.error, urllib.parse, urllib.request
from pathlib import Path

REPO = 'VitalSkib/mpc-hc'
API = 'https://api.github.com/repos/' + REPO

def request(url, data=None, method=None, binary=False):
    if urllib.parse.urlparse(url).hostname not in {'api.github.com', 'uploads.github.com'}:
        raise ValueError('Unexpected GitHub host')
    body = data if binary else (json.dumps(data).encode() if data is not None else None)
    req = urllib.request.Request(url, body, method=method,
        headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                 'User-Agent': 'MPC-HC-Fluent-release',
                 'Accept': 'application/vnd.github+json',
                 'Content-Type': 'application/octet-stream' if binary else 'application/json'})
    with urllib.request.urlopen(req, timeout=90) as response:
        return None if response.status == 204 else json.load(response)

def main():
    root = Path('fluent-release')
    m = json.loads((root/'manifest.json').read_text())
    if m['product'] != 'MPC-HC-Fluent' or m['channel'] not in ('stable','beta'):
        raise ValueError('Invalid release manifest')
    archives = list(root.glob('*.zip'))
    if len(archives) != 1: raise ValueError('Expected one update archive')
    checksums = dict(line.split()[::-1] for line in (root/'SHA256SUMS.txt').read_text().splitlines() if line.strip())
    setup = root/m['installer_name']
    if setup.name != m['installer_name'] or setup.suffix != '.exe': raise ValueError('Invalid installer name')
    for path in (archives[0], setup):
        if checksums.get(path.name) != hashlib.sha256(path.read_bytes()).hexdigest():
            raise ValueError('Release file digest mismatch')
    digest = checksums[archives[0].name]
    if checksums[setup.name] != m['installer_sha256']: raise ValueError('Installer manifest digest mismatch')
    beta = m['channel'] == 'beta'
    tag = 'fluent-' + m['version'] + ('-beta' if beta else '')
    release = request(API+'/releases', {
        'tag_name': tag, 'target_commitish': m['source_commit'],
        'name': 'MPC-HC Fluent '+m['version']+(' beta' if beta else ''),
        'draft': True, 'prerelease': beta,
        'body': f"Based on official MPC-HC {m['upstream_tag']}.\n\n"
                f"Download **{setup.name}** for a complete Windows x64 installation, including codecs, languages and Fluent artwork. "
                'No original installation is required. Close MPC-HC and run the installer. '
                'It also upgrades existing MPC-HC x64 installations through the native setup logic. '
                'No automatic installation.\n\n'
                'The optional update ZIP is only for an existing portable folder.\n\n'
                f"[Source and build scripts]({m['source_url']}) · GPLv3+\n\n"
                'See README-Fluent.md for preserved UI behavior, installation and audit limits.'})
    upload = release['upload_url'].split('{')[0]
    for path in (setup, archives[0], root/'SHA256SUMS.txt', root/'manifest.json'):
        request(upload+'?name='+urllib.parse.quote(path.name), path.read_bytes(), 'POST', True)
    # A stable automatic merge is promoted only if the reviewed base has not moved.
    expected = os.environ.get('PROMOTE_BASE', '')
    if expected and not beta:
        current = request(API+'/git/ref/heads/develop')['object']['sha']
        if current != expected: raise RuntimeError('Stable base changed during build; release remains draft')
        request(API+'/git/refs/heads/develop', {'sha':m['source_commit'], 'force':False}, 'PATCH')
    release = request(API+'/releases/'+str(release['id']), {'draft':False, 'make_latest':'false' if beta else 'true'}, 'PATCH')
    feed = 'updates-' + m['channel']
    try: parent = request(API+'/git/ref/heads/'+feed)['object']['sha']
    except urllib.error.HTTPError as e:
        if e.code != 404: raise
        parent = None
    latest = dict(m, release_url=release['html_url'], archive_sha256=digest,
        installer_url=f"https://github.com/{REPO}/releases/download/{tag}/{urllib.parse.quote(setup.name)}")
    entries=[]
    for path, content in {'version.txt':m['version']+'\n', 'latest.json':json.dumps(latest,indent=2)+'\n'}.items():
        blob = request(API+'/git/blobs', {'content':content,'encoding':'utf-8'})
        entries.append({'path':path,'mode':'100644','type':'blob','sha':blob['sha']})
    tree_args={'tree':entries}
    if parent: tree_args['base_tree'] = request(API+'/git/commits/'+parent)['tree']['sha']
    tree = request(API+'/git/trees', tree_args)
    commit = request(API+'/git/commits', {'message':'Publish '+tag+' updater feed',
        'tree':tree['sha'], 'parents':[parent] if parent else []})
    if parent: request(API+'/git/refs/heads/'+feed, {'sha':commit['sha'],'force':False}, 'PATCH')
    else: request(API+'/git/refs', {'ref':'refs/heads/'+feed, 'sha':commit['sha']})
    candidate = os.environ.get('CANDIDATE_REF', '')
    if candidate.startswith(('auto/stable-', 'auto/beta-')):
        ref = request(API+'/git/ref/heads/'+candidate)
        if ref['object']['sha'] == m['source_commit']:
            req = urllib.request.Request(API+'/git/refs/heads/'+candidate, method='DELETE',
                headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'], 'User-Agent':'MPC-HC-Fluent-release'})
            with urllib.request.urlopen(req, timeout=30): pass
    with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as f: f.write(f"Release: {release['html_url']}\n")

if __name__ == '__main__': main()
