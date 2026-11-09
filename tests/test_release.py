"""Check release boundary conditions without network access or publication."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('stel_release', ROOT / 'scripts/release.py')
release = importlib.util.module_from_spec(spec);spec.loader.exec_module(release)
spec = importlib.util.spec_from_file_location('stel_archives', ROOT / 'scripts/ci/fetch-source-archives.py')
archives = importlib.util.module_from_spec(spec);spec.loader.exec_module(archives)


class ReleaseTests(unittest.TestCase):
    def test_pinned_archive_integrity_before_replacement(self):
        import io
        commit = 'a' * 40
        for mode in ('valid', 'wrong-checksum', 'wrong-commit'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp);sdk = root / 'sdk';sdk.mkdir();cache = root / 'cache';cache.mkdir()
                target = sdk / 'module';target.mkdir();(target / 'old.txt').write_text('preserve on failure')
                archive = cache / (commit + '.tar.gz')
                with tarfile.open(archive, 'w:gz', format=tarfile.PAX_FORMAT,
                                  pax_headers={'comment': 'b' * 40 if mode == 'wrong-commit' else commit}) as tar:
                    directory = tarfile.TarInfo('snapshot');directory.type = tarfile.DIRTYPE;tar.addfile(directory)
                    file = tarfile.TarInfo('snapshot/source.c');file.size = 7;tar.addfile(file, io.BytesIO(b'source!'))
                entry = {'path': 'module', 'sdk_gitlink': 'module', 'sdk_commit': commit,
                         'commit': commit, 'url': 'https://unused.test/source.tar.gz',
                         'sha256': '0' * 64 if mode == 'wrong-checksum' else hashlib.sha256(archive.read_bytes()).hexdigest()}
                with mock.patch.object(archives.subprocess, 'check_output', return_value=commit):
                    if mode == 'valid':
                        archives.restore(sdk, entry, cache)
                        self.assertEqual((target / 'source.c').read_bytes(), b'source!')
                    else:
                        with self.assertRaises(ValueError):archives.restore(sdk, entry, cache)
                        self.assertEqual((target / 'old.txt').read_text(), 'preserve on failure')

    def test_source_archive_excludes_credentials_and_build_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'CMakeLists.txt').write_text('project(Stel VERSION 1.2.3 LANGUAGES C CXX)')
            for file in ('src/main.cpp', 'src/private.key', 'src/.secrets/pbx.env',
                         'tests/__pycache__/cached.pyc', '.secrets/pbx.env', 'build/stel',
                         '.github/workflows/build.yml', 'website/index.html', 'LICENSES/AGPL-3.0.txt'):
                p = root / file;p.parent.mkdir(parents=True, exist_ok=True);p.write_text('fixture')
            archive = release.source_archive(root / 'dist', root)
            with tarfile.open(archive) as tar:
                names = set(tar.getnames())
            self.assertIn('Stel-1.2.3/src/main.cpp', names)
            self.assertIn('Stel-1.2.3/.github/workflows/build.yml', names)
            self.assertFalse(any('.secrets' in n or 'private.key' in n or '__pycache__' in n or '/build/' in n for n in names))

    def test_metadata_rejects_mismatching_tag(self):
        proc = subprocess.run([sys.executable, str(ROOT / 'scripts/release.py'), 'metadata',
                               '--ref', 'refs/tags/v999.0.0'], capture_output=True)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn(b'Tag must match', proc.stderr)

    def test_checksums_match_asset_contents(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp);(path / 'binary.exe').write_bytes(b'native test payload')
            release.checksums(path)
            self.assertEqual((path / 'SHA256SUMS').read_text(),
                             hashlib.sha256(b'native test payload').hexdigest() + '  binary.exe\n')

    def test_local_publisher_refuses_without_github_access(self):
        import os
        env = os.environ.copy();env.pop('GITHUB_ACTIONS', None)
        proc = subprocess.run([sys.executable, str(ROOT / 'scripts/ci/publish-release.py')],
                              capture_output=True, env=env)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn(b'only available in GitHub Actions', proc.stderr)


if __name__ == '__main__':
    unittest.main()
