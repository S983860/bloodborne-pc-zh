from paths import ROOT
from pathlib import Path
import tempfile
import unittest
from run_windows import gpu_cache_dir, reset_stale_gpu_cache


class GpuCacheTests(unittest.TestCase):
    def test_another_engines_cache_is_cleared_once(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory) / 'cache'
            (cache / 'CUSA03173').mkdir(parents=True)
            (cache / 'CUSA03173' / 'a.spv').write_bytes(b'x')
            (cache / 'CUSA03173.vkcache').write_bytes(b'x')
            self.assertTrue(reset_stale_gpu_cache(cache, 'engine-2'))
            # The driver's own cache stays; the engine's records go.
            self.assertEqual(sorted(p.name for p in cache.iterdir()), ['CUSA03173.vkcache', 'engine.txt'])
            # The same engine keeps what it builds from then on.
            (cache / 'CUSA03173').mkdir()
            self.assertFalse(reset_stale_gpu_cache(cache, 'engine-2'))
            self.assertTrue((cache / 'CUSA03173').is_dir())
            self.assertTrue(reset_stale_gpu_cache(cache, 'engine-3'))
            self.assertFalse((cache / 'CUSA03173').exists())

    def test_no_cache_yet_and_unknown_engine(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory) / 'cache'
            self.assertFalse(reset_stale_gpu_cache(cache, ''))
            self.assertFalse(cache.exists())
            self.assertFalse(reset_stale_gpu_cache(cache, 'engine-1'))
            self.assertEqual((cache / 'engine.txt').read_text().strip(), 'engine-1')

    def test_the_cache_is_beside_the_engine_unless_moved(self):
        self.assertEqual(gpu_cache_dir({'BB_GPU_USER_DIR': 'X:/gpu'}), Path('X:/gpu/cache'))
        self.assertEqual(gpu_cache_dir({}).parent.name, 'user')


if __name__ == '__main__':
    unittest.main()
