#!/usr/bin/env python3
"""Exercise the audit against real temporary Git histories and working trees."""
import copy
import hashlib
import json
import sys
from pathlib import Path
import subprocess
import tempfile
import unittest

from check_noah_fork import AuditError, audit, git, patch, validate


class ForkAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        git(self.root, 'init', '-q')
        git(self.root, 'config', 'user.name', 'Test')
        git(self.root, 'config', 'user.email', 'test@example.invalid')
        git(self.root, 'config', 'core.autocrlf', 'false')
        self.write('quantum/action.c', 'upstream();\n')
        self.write('keyboards/other/config.h', 'upstream board\n')
        self.write('keyboards/ours/config.h', 'old pins\n')
        git(self.root, 'add', '.')
        git(self.root, '-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'upstream')
        self.base = git(self.root, 'rev-parse', 'HEAD').decode().strip()
        self.write('quantum/action.c', 'our_hook();\nupstream();\n')
        self.write('keyboards/ours/config.h', 'our pins\n')
        git(self.root, 'rm', '-q', 'keyboards/other/config.h')
        git(self.root, 'add', '.')
        self.manifest = {'format': 1, 'upstream_commit': self.base,
                         'excluded_keyboard_policy': 'unlisted-deletions-only',
                         'patches': [self.group('gesture', 'runtime', 'quantum/action.c'),
                                     self.group('board', 'board', 'keyboards/ours/config.h')]}

    def write(self, name, text):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)

    def group(self, name, kind, path):
        return {'id': name, 'kind': kind, 'purpose': 'Test contract', 'files': [path],
                'firmware_tests': ['tests/host/run_contract.sh'],
                'patch_sha256': hashlib.sha256(patch(self.root, self.base, [path])).hexdigest()}

    def errors(self):
        return audit(self.root, self.manifest)[2]

    def test_reviewed_patch_and_keyboard_deletions_pass(self):
        rows, excluded, errors = audit(self.root, self.manifest)
        self.assertEqual(errors, [])
        self.assertEqual(excluded, 1)
        self.assertEqual(len(rows), 2)

    def test_lost_hook_fails_even_in_inventoried_file(self):
        self.write('quantum/action.c', 'upstream();\n')
        self.assertTrue(any('patch disappeared' in e for e in self.errors()))

    def test_unreviewed_edit_in_owned_file_fails(self):
        self.write('quantum/action.c', 'our_hook();\nunreviewed();\nupstream();\n')
        self.assertTrue(any('patch changed' in e for e in self.errors()))

    def test_missing_board_fails(self):
        (self.root / 'keyboards/ours/config.h').unlink()
        self.assertTrue(any('missing regular file' in e for e in self.errors()))

    def test_unknown_staged_core_file_fails(self):
        self.write('quantum/extra.c', 'unexpected\n')
        git(self.root, 'add', 'quantum/extra.c')
        self.assertTrue(any('Uninventoried difference' in e for e in self.errors()))

    def test_unknown_untracked_core_file_fails(self):
        self.write('quantum/extra.c', 'unexpected\n')
        self.assertTrue(any('? quantum/extra.c' in e for e in self.errors()))

    def test_restored_identical_upstream_board_fails(self):
        git(self.root, 'restore', '--source=' + self.base, '--staged', '--worktree', 'keyboards/other/config.h')
        self.assertTrue(any('Uninventoried keyboard present' in e for e in self.errors()))

    def test_new_board_not_covered_by_deletion_policy(self):
        self.write('keyboards/new/config.h', 'new board\n')
        git(self.root, 'add', 'keyboards/new/config.h')
        self.assertTrue(any('Uninventoried keyboard' in e for e in self.errors()))

    def test_missing_declared_test_fails(self):
        _, _, errors = audit(self.root, self.manifest, self.root)
        self.assertTrue(any('missing firmware test' in e for e in errors))

    def test_present_declared_test_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            runner = Path(tmp) / 'tests/host/run_contract.sh'
            runner.parent.mkdir(parents=True)
            runner.write_text('exit 0\n')
            self.assertEqual(audit(self.root, self.manifest, Path(tmp))[2], [])

    def test_duplicate_ownership_rejected(self):
        self.manifest['patches'][1]['files'].append('quantum/action.c')
        with self.assertRaisesRegex(AuditError, 'Duplicate ownership'):
            validate(self.manifest)

    def test_wildcard_and_path_traversal_rejected(self):
        for path in ['quantum/*', '../elsewhere', '/absolute', ':(exclude)quantum/action.c']:
            manifest = copy.deepcopy(self.manifest)
            manifest['patches'][0]['files'] = [path]
            with self.subTest(path=path), self.assertRaises(AuditError):
                validate(manifest)

    def test_missing_base_fails_closed(self):
        self.manifest['upstream_commit'] = '0' * 40
        with self.assertRaises(AuditError):
            audit(self.root, self.manifest)

    def test_nonancestor_base_rejected(self):
        tree = git(self.root, 'write-tree').decode().strip()
        unrelated = git(self.root, 'commit-tree', tree, '-m', 'unrelated').decode().strip()
        self.manifest['upstream_commit'] = unrelated
        with self.assertRaises(AuditError):
            audit(self.root, self.manifest)

    def test_committing_does_not_change_fingerprint(self):
        before = self.manifest['patches'][0]['patch_sha256']
        git(self.root, '-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'our fork')
        self.assertEqual(hashlib.sha256(patch(self.root, self.base, ['quantum/action.c'])).hexdigest(), before)
        self.assertEqual(self.errors(), [])

    def test_cli_runs_shared_runner_once_with_explicit_qmk(self):
        with tempfile.TemporaryDirectory() as tmp:
            firmware = Path(tmp)
            runner = firmware / 'tests/host/run_contract.sh'
            runner.parent.mkdir(parents=True)
            runner.write_text('printf "%s\\n" "$QMK_ROOT" >> invoked.txt\n')
            inventory = self.root / 'inventory.json'
            manifest = copy.deepcopy(self.manifest)
            manifest['patches'].append({'id': 'inventory', 'kind': 'maintenance',
                                       'purpose': 'Test metadata', 'files': ['inventory.json']})
            inventory.write_text(json.dumps(manifest))
            git(self.root, 'add', 'inventory.json')
            command = [sys.executable, str(Path(__file__).with_name('check_noah_fork.py')),
                       '--root', str(self.root), '--manifest', 'inventory.json',
                       '--firmware', str(firmware), '--run-tests']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((firmware / 'invoked.txt').read_text().splitlines(), [str(self.root.resolve())])
            runner.write_text('exit 7\n')
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)

    def test_symlink_cannot_replace_retained_source(self):
        path = self.root / 'quantum/action.c'
        path.unlink()
        path.symlink_to('../keyboards/ours/config.h')
        self.assertTrue(any('missing regular file' in e for e in self.errors()))

    def test_audit_does_not_change_sources_or_index(self):
        before = git(self.root, 'diff', '--binary', 'HEAD')
        index = git(self.root, 'write-tree')
        audit(self.root, self.manifest)
        self.assertEqual(git(self.root, 'diff', '--binary', 'HEAD'), before)
        self.assertEqual(git(self.root, 'write-tree'), index)


if __name__ == '__main__':
    unittest.main()
