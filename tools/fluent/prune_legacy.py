"""Remove an explicit set of superseded fork releases after a replacement ships."""
import json
import urllib.error
from pathlib import Path

OBSOLETE = ('fluent-2.8.4.10001', 'fluent-2.8.4.10006',
            'fluent-2.8.4.10008', 'fluent-2.8.4.10009')
EXPERIMENT = 'a1c7e4aa08f1e2dbe0ae5ad7c872b0a060aeff5d'


def prune(request, api, current):
    # Never touch upstream tags, the latest build or an unpublished replacement.
    if current in OBSOLETE or not current.startswith('fluent-'):
        raise ValueError('Replacement must supersede the legacy releases')
    published = request(api + '/releases/tags/' + current)
    if published['draft'] or published['prerelease']:
        raise ValueError('Replacement must be a public stable release')
    assets = {a['name'] for a in published['assets']}
    if not any(name.endswith('-x64-setup.exe') for name in assets) or 'manifest.json' not in assets:
        raise ValueError('Replacement installer assets are incomplete')

    def optional(url, method=None):
        try:
            return request(url, method=method)
        except urllib.error.HTTPError as error:
            if error.code != 404:
                raise
            error.close()
            return None

    for tag in OBSOLETE:
        release = optional(api + '/releases/tags/' + tag)
        if release:
            optional(api + '/releases/' + str(release['id']), 'DELETE')
        # GitHub returns 422 (rather than 404) when DELETE targets an absent ref.
        # Check first so rerunning cleanup after a partial success is harmless.
        ref_url = api + '/git/ref/tags/' + tag
        if optional(ref_url):
            try:
                request(api + '/git/refs/tags/' + tag, method='DELETE')
            except urllib.error.HTTPError as error:
                if error.code != 422:
                    raise
                error.close()
                if optional(ref_url) is not None:
                    raise error
    ref = optional(api + '/git/ref/heads/fluent-hover-ui')
    if ref and ref['object']['sha'] == EXPERIMENT:
        optional(api + '/git/refs/heads/fluent-hover-ui', 'DELETE')


if __name__ == '__main__':
    from publish_release import request, API
    manifest = json.loads(Path('fluent-release/manifest.json').read_text())
    if manifest['channel'] == 'stable':
        prune(request, API, 'fluent-' + manifest['version'])
