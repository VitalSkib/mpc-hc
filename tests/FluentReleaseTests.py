import importlib.util, unittest
from pathlib import Path

def module(name):
    p = Path(__file__).resolve().parents[1]/'tools/fluent'/f'{name}.py'
    spec = importlib.util.spec_from_file_location(name, p)
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m

class Releases(unittest.TestCase):
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
