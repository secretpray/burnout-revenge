"""Reject stale or incomplete native qualification without a compiler or game."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'make_bundle', Path(__file__).resolve().parents[1] / 'scripts/linux/make_bundle.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class NativeValidationTest(unittest.TestCase):
    def setUp(self):
        self.report = dict(schema=2, revision='abcdef12', architecture='x86_64',
                           native_hardware_tested=True, environments=['SteamOS on physical Deck'],
                           limitations=['Candidate smoke only; baseline gameplay recorded separately'],
                           checks=dict(full_tests='pass', clean_install='pass', candidate_smoke='pass'))

    def test_matching_candidate(self):
        self.assertEqual(module.native_validation(self.report, 'abcdef12', 'x86_64'), self.report)

    def test_rejects_stale_revision_wrong_arch_and_incomplete_claims(self):
        for key, value in [('revision', 'old'), ('architecture', 'arm64'),
                           ('native_hardware_tested', False), ('environments', []),
                           ('limitations', []), ('checks', {'full_tests': 'pass'}),
                           ('checks', {'full_tests': 'pass', 'clean_install': 'pass',
                                       'candidate_smoke': 'pending'})]:
            with self.subTest(key=key, value=value):
                report = copy.deepcopy(self.report)
                report[key] = value
                with self.assertRaises(ValueError):
                    module.native_validation(report, 'abcdef12', 'x86_64')


if __name__ == '__main__':
    unittest.main()
