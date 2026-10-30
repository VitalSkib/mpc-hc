import importlib.util, unittest, tempfile, os, json, hashlib
from unittest.mock import patch
from pathlib import Path

def module(name):
    p = Path(__file__).resolve().parents[1]/'tools/fluent'/f'{name}.py'
    spec = importlib.util.spec_from_file_location(name, p)
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m

class Releases(unittest.TestCase):
    def test_feed_uses_published_release_url(self):
        m = module('publish_release')
        calls = []
        def request(url, data=None, method=None, binary=False):
            calls.append((url, data, method))
            if url.endswith('/releases'):
                return {'id':1, 'upload_url':'https://uploads.github.com/test{?name}',
                        'html_url':'https://github.com/test/untagged-draft'}
            if url.endswith('/releases/1'):
                return {'html_url':'https://github.com/test/releases/tag/fluent-2.8.4.10001'}
            if '/git/ref/heads/' in url: return {'object':{'sha':'parent'}}
            if '/git/commits/parent' in url: return {'tree':{'sha':'tree'}}
            return {'sha':'object'}
        with tempfile.TemporaryDirectory() as temporary:
            previous = os.getcwd()
            try:
                os.chdir(temporary)
                root = Path('fluent-release'); root.mkdir()
                (root/'update.zip').write_bytes(b'fixture')
                (root/'SHA256SUMS.txt').write_text(hashlib.sha256(b'fixture').hexdigest()+' update.zip')
                (root/'manifest.json').write_text(json.dumps({'product':'MPC-HC-Fluent',
                    'channel':'stable', 'version':'2.8.4.10001', 'source_commit':'source',
                    'upstream_tag':'2.8.4', 'source_url':'https://github.com/test/source'}))
                with patch.object(m, 'request', side_effect=request), patch.dict(os.environ,
                        {'PROMOTE_BASE':'', 'CANDIDATE_REF':'', 'GITHUB_STEP_SUMMARY':'summary.txt'}):
                    m.main()
                feeds = [json.loads(data['content']) for url,data,method in calls
                         if url.endswith('/git/blobs') and 'release_url' in data['content']]
                self.assertEqual(feeds[0]['release_url'],
                    'https://github.com/test/releases/tag/fluent-2.8.4.10001')
            finally: os.chdir(previous)
    def test_channels_do_not_cross(self):
        m = module('prepare_upstream')
        rows = [{'tag_name':'9.0.0','draft':True,'prerelease':False},
                {'tag_name':'2.8.5.7','draft':False,'prerelease':True},
                {'tag_name':'2.8.4','draft':False,'prerelease':False}]
        self.assertEqual(m.select_release(rows,'stable')['tag_name'],'2.8.4')
        self.assertEqual(m.select_release(rows,'beta')['tag_name'],'2.8.5.7')
    def test_no_beta_is_not_stable(self):
        m = module('prepare_upstream')
        self.assertIsNone(m.select_release([{'draft':False,'prerelease':False}], 'beta'))
    def test_tags_are_refs_not_shell_or_paths(self):
        m = module('prepare_upstream')
        for value in ['2.8.4','2.8.4.17','v2.9.0-beta.1']:
            self.assertIsNotNone(m.TAG.fullmatch(value))
        for value in ['../../develop','--upload-pack=bad','2.8.4; echo bad','2.8.4/other','2.8.4\n']:
            self.assertIsNone(m.TAG.fullmatch(value))

if __name__ == '__main__': unittest.main()
