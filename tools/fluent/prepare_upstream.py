"""Prepare an isolated merge of an official release. Never resolve conflicts automatically."""
import argparse, base64, json, os, re, subprocess, urllib.request, urllib.error
from pathlib import Path

TAG = re.compile(r"v?\d+\.\d+\.\d+(?:\.\d+)?(?:-[A-Za-z0-9.-]+)?\Z")

def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()

def select_release(releases, channel):
    return next((r for r in releases if not r.get('draft')
                 and bool(r.get('prerelease')) == (channel == 'beta')), None)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--channel', choices=['stable', 'beta'], default='stable')
    p.add_argument('--force', action='store_true')
    args = p.parse_args()
    request = urllib.request.Request('https://api.github.com/repos/clsid2/mpc-hc/releases?per_page=100',
        headers={'Accept': 'application/vnd.github+json', 'User-Agent': 'MPC-HC-Fluent-release-monitor',
                 'Authorization': 'Bearer ' + os.environ['GH_TOKEN']})
    with urllib.request.urlopen(request, timeout=30) as response:
        raw = response.read(2 * 1024 * 1024 + 1)
    if len(raw) > 2 * 1024 * 1024:
        raise RuntimeError('Release response exceeds limit')
    release = select_release(json.loads(raw), args.channel)
    if not release or not TAG.fullmatch(release['tag_name']):
        raise RuntimeError('No valid official release for channel')
    tag = release['tag_name']
    config = json.loads(Path('tools/fluent/project.json').read_text())
    output = Path(os.environ['GITHUB_OUTPUT'])
    published = None
    try:
        feed_request = urllib.request.Request('https://api.github.com/repos/VitalSkib/mpc-hc/contents/latest.json?ref=updates-'+args.channel, headers=dict(request.headers))
        with urllib.request.urlopen(feed_request, timeout=30) as response:
            entry = json.load(response)
        published = json.loads(base64.b64decode(entry['content']))
    except urllib.error.HTTPError as error:
        if error.code != 404: raise
    if not args.force and published and published['upstream_tag'] == tag:
        with output.open('a') as f: f.write('changed=false\n')
        return
    base = git('rev-parse', 'HEAD')
    git('config', 'user.name', 'MPC-HC Fluent release automation')
    git('config', 'user.email', '41898282+github-actions[bot]@users.noreply.github.com')
    git('fetch', '--no-tags', 'https://github.com/clsid2/mpc-hc.git',
        f'refs/tags/{tag}:refs/tags/fluent-upstream/{tag}')
    upstream = git('rev-parse', f'refs/tags/fluent-upstream/{tag}^{{commit}}')
    branch = f'auto/{args.channel}-{os.environ["GITHUB_RUN_ID"]}-{os.environ["GITHUB_RUN_ATTEMPT"]}'
    git('switch', '-c', branch)
    try:
        git('merge', '--no-ff', '--no-edit', upstream)
    except subprocess.CalledProcessError:
        git('merge', '--abort')
        raise RuntimeError(f'Upstream {tag} conflicts with the fork. Manual review required; no build/release.')
    config.update(upstream_tag=tag, upstream_commit=upstream)
    Path('tools/fluent/project.json').write_text(json.dumps(config, indent=2) + '\n')
    git('add', 'tools/fluent/project.json')
    if git('diff', '--cached', '--name-only'):
        git('commit', '-m', f'Record official {args.channel} release {tag}')
    source = git('rev-parse', 'HEAD')
    git('push', 'origin', f'HEAD:refs/heads/{branch}')
    with output.open('a') as f:
        f.write(f'changed=true\nref={branch}\nsource={source}\nbase={base}\nchannel={args.channel}\nupstream={tag}\n')

if __name__ == '__main__':
    main()
