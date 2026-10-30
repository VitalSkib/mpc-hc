"""Keep fork revisions increasing across direct and reusable workflow runs."""
import base64, json, os, re, urllib.error, urllib.request
from pathlib import Path


def next_revision(run_number, current, published=None):
    revision = 10000 + int(run_number)
    if published:
        previous = published.strip().split('.')
        if len(previous) != 4 or not all(p.isdecimal() for p in previous):
            raise ValueError('Invalid published version')
        if tuple(map(int, previous[:3])) == tuple(current):
            revision = max(revision, int(previous[3]) + 1)
    if not 0 < revision <= 65535:
        raise ValueError('Release revision requires a versioning review')
    return revision


def main():
    channel = os.environ.get('CHANNEL', 'stable')
    if channel not in ('stable', 'beta'): raise ValueError('Invalid release channel')
    version = Path('include/version.h').read_text()
    current = tuple(int(re.search(r'^#define\s+MPC_VERSION_'+name+r'\s+(\d+)', version, re.M)[1])
                    for name in ('MAJOR','MINOR','PATCH'))
    request = urllib.request.Request(
        'https://api.github.com/repos/VitalSkib/mpc-hc/contents/version.txt?ref=updates-'+channel,
        headers={'User-Agent':'MPC-HC-Fluent-version', 'Authorization':'Bearer '+os.environ['GH_TOKEN']})
    published = None
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            entry = json.load(response)
        published = base64.b64decode(entry['content']).decode('utf-8')
    except urllib.error.HTTPError as error:
        if error.code != 404: raise
    revision = next_revision(os.environ['GITHUB_RUN_NUMBER'], current, published)
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        output.write(f'revision={revision}\nbeta={int(channel == "beta")}\n')


if __name__ == '__main__': main()
