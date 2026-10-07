#!/usr/bin/env python3
"""Audit the working tree's reviewed delta from upstream. Never updates inventory."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


class AuditError(Exception):
    pass


def git(root, *args):
    result = subprocess.run(['git', '-C', str(root), *args], capture_output=True)
    if result.returncode:
        raise AuditError(result.stderr.decode(errors='replace').strip())
    return result.stdout


def path_name(value):
    if (not isinstance(value, str) or not value or value.startswith(':')
            or '\\' in value or any(c in value for c in '*?[')
            or PurePosixPath(value).is_absolute()
            or any(p in ('', '.', '..') for p in value.split('/'))):
        raise AuditError('Expected an exact repository-relative path: ' + repr(value))
    return value


def validate(manifest):
    if manifest.get('format') != 1:
        raise AuditError('Unsupported inventory format')
    base = manifest.get('upstream_commit', '')
    if not re.fullmatch(r'[0-9a-f]{40}', base):
        raise AuditError('upstream_commit must be a full commit SHA')
    groups = manifest.get('patches', [])
    if not groups:
        raise AuditError('Inventory must contain patches')
    owners, ids = {}, set()
    for group in groups:
        name = group.get('id', '')
        if not re.fullmatch(r'[a-z0-9-]+', name) or name in ids:
            raise AuditError('Invalid or duplicate patch id: ' + repr(name))
        ids.add(name)
        if not group.get('purpose') or not group.get('files'):
            raise AuditError(name + ': purpose and files are required')
        kind = group.get('kind')
        if kind not in ('runtime', 'board', 'maintenance'):
            raise AuditError(name + ': unknown patch kind')
        if kind != 'maintenance':
            if not re.fullmatch(r'[0-9a-f]{64}', group.get('patch_sha256', '')):
                raise AuditError(name + ': reviewed patch_sha256 is required')
            if not group.get('firmware_tests'):
                raise AuditError(name + ': firmware_tests are required')
        for name in group.get('firmware_tests', []):
            path_name(name)
            if not name.startswith('tests/host/') or not name.endswith('.sh'):
                raise AuditError('Expected a firmware host runner: ' + name)
        for name in group['files']:
            path_name(name)
            if name in owners:
                raise AuditError('Duplicate ownership: ' + name)
            owners[name] = group['id']
    # The only broad exclusion is deletions of boards we deliberately do not ship.
    if manifest.get('excluded_keyboard_policy') != 'unlisted-deletions-only':
        raise AuditError('Expected unlisted-deletions-only keyboard policy')
    return base, groups, owners


def patch(root, base, files):
    # Stable options: do not inherit user diff drivers, rename heuristics or context.
    return git(root, '-c', 'diff.algorithm=myers', '-c', 'diff.indentHeuristic=false',
               'diff', '--no-ext-diff', '--no-textconv', '--no-renames', '--binary',
               '--full-index', '--no-color', '--no-prefix', '--no-relative',
               '--inter-hunk-context=0', '--unified=0', base,
               '--', *sorted(files))


def audit(root, manifest, firmware=None):
    base, groups, owners = validate(manifest)
    git(root, 'cat-file', '-e', base + '^{commit}')
    git(root, 'merge-base', '--is-ancestor', base, 'HEAD')
    if git(root, 'ls-files', '-u'):
        raise AuditError('Resolve merge conflicts before auditing')
    raw = git(root, 'diff', '--no-ext-diff', '--no-textconv', '--no-renames',
              '--name-status', '-z', base).split(b'\0')
    changes = [(raw[i].decode(), raw[i + 1].decode()) for i in range(0, len(raw) - 1, 2)]
    changes += [('?', p.decode()) for p in git(root, 'ls-files', '--others', '--exclude-standard', '-z').split(b'\0') if p]
    errors, excluded = [], 0
    for status, name in changes:
        if name in owners:
            if status == '?':
                errors.append('Stage inventoried new file before auditing: ' + name)
        elif name.startswith('keyboards/') and status == 'D':
            excluded += 1
        else:
            errors.append('Uninventoried difference: ' + status + ' ' + name)
    # Restoring an excluded board to its exact upstream bytes creates no diff.
    # Check the present tree too, so even that restoration requires an inventory entry.
    present = git(root, 'ls-files', '--cached', '--others', '--exclude-standard', '-z', '--', 'keyboards/').split(b'\0')
    for raw_name in set(present) - {b''}:
        name = raw_name.decode()
        if name not in owners and (root / name).exists():
            errors.append('Uninventoried keyboard present: ' + name)
    rows = []
    for group in groups:
        for name in group['files']:
            if not (root / name).is_file() or (root / name).is_symlink():
                errors.append(group['id'] + ': missing regular file: ' + name)
        if group['kind'] == 'maintenance':
            rows.append((group['id'], 'listed maintenance files (not fingerprinted)'))
        else:
            delta = patch(root, base, group['files'])
            digest = hashlib.sha256(delta).hexdigest()
            rows.append((group['id'], digest))
            if not delta:
                errors.append(group['id'] + ': custom patch disappeared; review upstream adoption or removal')
            if digest != group['patch_sha256']:
                errors.append(group['id'] + ': patch changed; inspect --show-patch ' + group['id'] + ' before updating its reviewed fingerprint')
        if firmware:
            for name in group.get('firmware_tests', []):
                if not (firmware / name).is_file():
                    errors.append(group['id'] + ': missing firmware test: ' + name)
    return rows, excluded, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--manifest', default='noah-fork-patches.json')
    parser.add_argument('--firmware', type=Path, help='Explicit firmware checkout; validate referenced test paths')
    parser.add_argument('--run-tests', action='store_true', help='Run each listed host runner once; requires --firmware')
    parser.add_argument('--show-patch', metavar='ID', help='Print the current delta for one patch; inspection only')
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        manifest = json.loads((root / args.manifest).read_text())
        base, groups, _ = validate(manifest)
        if args.show_patch:
            group = next((g for g in groups if g['id'] == args.show_patch), None)
            if group is None:
                raise AuditError('Unknown patch: ' + args.show_patch)
            sys.stdout.buffer.write(patch(root, base, group['files']))
            return 0
        if args.run_tests and not args.firmware:
            raise AuditError('--run-tests requires --firmware')
        firmware = args.firmware.resolve() if args.firmware else None
        rows, excluded, errors = audit(root, manifest, firmware)
        for name, digest in rows:
            print(name + ': ' + digest)
        print('Excluded keyboard deletions: ' + str(excluded))
        if errors:
            for error in errors:
                print('FAIL: ' + error, file=sys.stderr)
            return 1
        print('Fork patch inventory: PASS', flush=True)
        if args.run_tests:
            import os
            env = dict(os.environ, QMK_ROOT=str(root))
            for runner in sorted({t for g in groups for t in g.get('firmware_tests', [])}):
                print('Running ' + runner, flush=True)
                subprocess.run(['sh', str(firmware / runner)], cwd=firmware, env=env, check=True)
        else:
            print('Behavior tests not run; use --firmware PATH --run-tests or the full firmware suite.')
        return 0
    except (AuditError, OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print('FAIL: ' + str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
