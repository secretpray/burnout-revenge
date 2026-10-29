#!/usr/bin/env python3
"""Exercise user install/update/removal and desktop launching with synthetic files."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

SCRIPTS = Path(__file__).resolve().parents[1] / 'scripts/linux'


@unittest.skipUnless(sys.platform.startswith('linux') and os.geteuid() != 0,
                     'The user installer runs on Linux as a non-root user')
class UserInstall(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='burnout-user-install-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / 'home space " $ % ` \\'
        self.home.mkdir()
        self.data = self.home / 'custom data'
        self.app = self.data / 'burnout-revenge/runtime'
        self.launcher = self.home / '.local/bin/burnout-revenge'
        self.desktop = self.data / 'applications/burnout-revenge.desktop'
        self.result = self.root / 'launched'
        self.env = dict(os.environ, HOME=str(self.home), XDG_DATA_HOME=str(self.data),
                        BR_INSTALL_TEST_RESULT=str(self.result))
        self.source = self.root / 'extracted archive'
        self.source.mkdir()
        for script in ('install.sh', 'uninstall.sh'):
            shutil.copy2(SCRIPTS / script, self.source / script)
        (self.source / 'lib').mkdir()
        (self.source / 'licenses').mkdir()
        (self.source / 'lib/BurnoutRevenge').write_text('synthetic executable fixture\n')
        (self.source / 'licenses/COPYING').write_text('synthetic license fixture\n')
        (self.source / 'burnout-revenge.sh').write_text(
            '#!/usr/bin/env bash\n'
            '[[ ${1:-} != --check-system ]] || exit 0\n'
            '{ printf "ran\\0"; printf "%s\\0" "$@"; } > "$BR_INSTALL_TEST_RESULT"\n')
        (self.source / 'burnout-revenge.sh').chmod(0o755)
        for name, value in {'burnout-revenge.png': 'synthetic icon', 'README.md': 'fixture',
                            'ARCHITECTURE': 'x86_64\n', 'MIN_GLIBC': '2.38\n'}.items():
            (self.source / name).write_text(value)
        (self.source / 'artwork').mkdir()
        (self.source / 'artwork/burnout-revenge-cover.jpg').write_bytes(b'cover fixture')
        self.identity('abcdef1')

    def identity(self, revision, version='1.2.3'):
        (self.source / 'bundle-info.json').write_text(json.dumps(
            dict(version=version, revision=revision, architecture='x86_64'), indent=2))
        self.hashes()

    def hashes(self):
        files = sorted(p for p in self.source.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
        (self.source / 'SHA256SUMS').write_text(''.join(
            hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(self.source).as_posix() + '\n'
            for p in files))

    def install(self, *args, ok=True):
        result = subprocess.run(['bash', str(self.source / 'install.sh'), *args], env=self.env,
                                text=True, capture_output=True, timeout=20)
        self.assertEqual(result.returncode == 0, ok, result.stdout + result.stderr)
        return result

    def test_update_launch_and_remove_preserve_user_data(self):
        save = self.data / 'burnout-revenge/save/Player Files'
        save.parent.mkdir(parents=True)
        save.write_bytes(b'untouched player data')
        self.install()
        first = (self.app / 'current').resolve()
        self.assertIn('Name=Burnout Revenge\n', self.desktop.read_text())
        self.assertIn('Icon=', self.desktop.read_text())
        self.assertEqual((first / 'artwork/burnout-revenge-cover.jpg').read_bytes(), b'cover fixture')
        subprocess.run([str(self.launcher), 'argument with spaces', '$literal%"'],
                       env=self.env, check=True, timeout=10)
        self.assertEqual(self.result.read_bytes(), b'ran\0argument with spaces\0$literal%"\0')
        self.install()  # idempotent
        self.identity('abcdef2')
        self.install()
        self.assertNotEqual(first, (self.app / 'current').resolve())
        self.assertTrue(first.is_dir())  # a running old binary is not overwritten
        self.assertEqual(save.read_bytes(), b'untouched player data')
        subprocess.run([str(self.launcher), '--uninstall'], env=self.env, check=True,
                       capture_output=True, timeout=10)
        self.assertFalse(self.app.exists())
        self.assertFalse(self.launcher.exists())
        self.assertFalse(self.desktop.exists())
        self.assertEqual(save.read_bytes(), b'untouched player data')

    def test_corrupt_update_does_not_replace_working_install(self):
        self.install()
        current = (self.app / 'current').resolve()
        self.identity('abcdef2')
        (self.source / 'lib/BurnoutRevenge').write_bytes(b'damaged')
        self.install(ok=False)
        self.assertEqual((self.app / 'current').resolve(), current)

    def test_foreign_launcher_is_not_overwritten(self):
        self.launcher.parent.mkdir(parents=True)
        self.launcher.write_text('another application')
        self.install(ok=False)
        self.assertEqual(self.launcher.read_text(), 'another application')
        self.assertFalse(self.app.exists())

    def test_prerelease_identity(self):
        self.identity('abcdef1', '1.2.3-linux-beta.1')
        self.install()
        self.assertEqual((self.app / 'current').resolve().name, '1.2.3-linux-beta.1-abcdef1')

    def test_legacy_archive_entry_is_not_used_for_the_menu(self):
        legacy = '[Desktop Entry]\nType=Application\nExec=burnout-revenge.sh\n'
        (self.source / 'burnout-revenge.desktop').write_text(legacy)
        self.hashes()
        self.install()
        self.assertEqual((self.app / 'current/burnout-revenge.desktop').read_text(), legacy)
        self.assertIn('Exec=/usr/bin/env -- ', self.desktop.read_text())
        subprocess.run([str(self.launcher), 'legacy archive'], env=self.env, check=True, timeout=10)
        self.assertEqual(self.result.read_bytes(), b'ran\0legacy archive\0')

    def test_portable_bundle_is_not_migrated_implicitly(self):
        (self.source / 'portable.txt').write_text('')
        self.install(ok=False)
        self.assertFalse(self.app.exists())

    def test_uninstall_refuses_an_active_installer(self):
        self.install()
        (self.app / '.install-lock').mkdir()
        self.install('--uninstall', ok=False)
        self.assertTrue(self.launcher.exists())
        self.assertTrue((self.app / 'current').exists())

    @unittest.skipUnless(shutil.which('gio'), 'GIO is needed for the real desktop parser check')
    def test_desktop_exec_quotes_special_characters(self):
        self.install()
        result = subprocess.run(['gio', 'launch', str(self.desktop)], env=self.env,
                                text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr + self.desktop.read_text())
        for _ in range(50):
            if self.result.exists():
                break
            time.sleep(0.1)
        self.assertTrue(self.result.exists(), self.desktop.read_text())
        self.assertTrue(self.result.read_bytes().startswith(b'ran\0'))


if __name__ == '__main__':
    unittest.main()
