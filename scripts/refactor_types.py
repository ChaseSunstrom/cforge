#!/usr/bin/env python3
"""
Refactoring script for cforge codebase.

This script:
1. Analyzes source files for declarations that should be in headers
2. Replaces bare C/C++ types with cforge predefined types
3. Ensures code is in the cforge namespace
"""

import os
import re
import sys
from pathlib import Path
from typing import Dict, List, Tuple, Set

# Type mappings from bare types to cforge types
TYPE_MAPPINGS = {
    # Only replace standalone types, not parts of other types
    r'\bint\b': 'cforge_int_t',
    r'\bunsigned int\b': 'cforge_uint_t',
    r'\bsigned int\b': 'cforge_int_t',
    r'\bunsigned char\b': 'cforge_byte_t',
    r'\bsigned char\b': 'cforge_sbyte_t',
    r'\bunsigned short\b': 'cforge_ushort_t',
    r'\bsigned short\b': 'cforge_short_t',
    r'\bunsigned long long\b': 'cforge_ulong_t',
    r'\bsigned long long\b': 'cforge_long_t',
    r'\blong long\b': 'cforge_long_t',
    r'\bfloat\b': 'cforge_float_t',
    r'\bdouble\b': 'cforge_double_t',
    r'\blong double\b': 'cforge_ldouble_t',
    r'\bsize_t\b': 'cforge_size_t',
}

# Patterns to skip (don't replace in these contexts)
SKIP_PATTERNS = [
    r'#include',
    r'#define',
    r'typedef',
    r'using\s+',
    r'static_cast<',
    r'dynamic_cast<',
    r'reinterpret_cast<',
    r'const_cast<',
    r'std::',
    r'fmt::',
    r'toml::',
    r'cforge_',  # Already using cforge types
]

# Files/directories to skip
SKIP_DIRS = {'vendor', 'build', 'deps', '.git', '__pycache__'}
SKIP_FILES = {'types.h', 'constants.h'}  # Don't modify type definitions

def should_skip_file(filepath: Path) -> bool:
    """Check if file should be skipped."""
    for part in filepath.parts:
        if part in SKIP_DIRS:
            return True
    if filepath.name in SKIP_FILES:
        return True
    return False

def should_skip_line(line: str) -> bool:
    """Check if line should be skipped for type replacement."""
    for pattern in SKIP_PATTERNS:
        if re.search(pattern, line):
            return True
    return False

def find_bare_types(content: str) -> List[Tuple[int, str, str]]:
    """Find bare types that should be replaced."""
    results = []
    lines = content.split('\n')

    for line_num, line in enumerate(lines, 1):
        if should_skip_line(line):
            continue

        for pattern, replacement in TYPE_MAPPINGS.items():
            matches = list(re.finditer(pattern, line))
            for match in matches:
                # Check context - don't replace in certain situations
                before = line[:match.start()]
                after = line[match.end():]

                # Skip if it's part of a template parameter
                if '<' in before and '>' not in before:
                    continue

                # Skip if it's already a cforge type
                if 'cforge_' in before[-20:]:
                    continue

                results.append((line_num, match.group(), replacement))

    return results

def find_declarations_in_source(content: str, filepath: Path) -> List[Tuple[int, str]]:
    """Find declarations in source files that should be in headers."""
    results = []
    lines = content.split('\n')

    # Patterns for declarations that should be in headers
    declaration_patterns = [
        # Class declarations
        r'^class\s+\w+\s*[{;]',
        # Struct declarations (not definitions)
        r'^struct\s+\w+\s*;',
        # Enum declarations
        r'^enum\s+(class\s+)?\w+\s*[{;]',
        # Global function declarations (not definitions)
        r'^(static\s+)?(inline\s+)?(const\s+)?(\w+::)?(\w+)\s+(\w+)\s*\([^)]*\)\s*;',
        # Type aliases
        r'^using\s+\w+\s*=',
        r'^typedef\s+',
    ]

    in_namespace = False
    namespace_depth = 0

    for line_num, line in enumerate(lines, 1):
        stripped = line.strip()

        # Track namespace depth
        if re.match(r'^namespace\s+\w+\s*{', stripped):
            in_namespace = True
            namespace_depth += 1
        if stripped == '}' and in_namespace:
            namespace_depth -= 1
            if namespace_depth == 0:
                in_namespace = False

        # Check for declarations
        for pattern in declaration_patterns:
            if re.match(pattern, stripped):
                # Skip if it's a definition (has body)
                if '{' in stripped and '}' not in stripped:
                    continue
                results.append((line_num, stripped[:80]))
                break

    return results

def find_missing_namespace(content: str, filepath: Path) -> List[str]:
    """Find code that's not in the cforge namespace."""
    issues = []

    # Check if file uses cforge namespace
    if 'namespace cforge' not in content:
        if filepath.suffix in ['.cpp', '.hpp']:
            # Check if it should be in cforge namespace
            if '#include "cforge/' in content or '#include "core/' in content:
                issues.append(f"File may need cforge namespace")

    return issues

def analyze_file(filepath: Path) -> Dict:
    """Analyze a single file for refactoring needs."""
    try:
        content = filepath.read_text(encoding='utf-8', errors='ignore')
    except Exception as e:
        return {'error': str(e)}

    result = {
        'filepath': str(filepath),
        'bare_types': [],
        'declarations_in_source': [],
        'namespace_issues': [],
    }

    # Only check for bare types in .cpp and .c files
    if filepath.suffix in ['.cpp', '.c']:
        result['bare_types'] = find_bare_types(content)
        result['declarations_in_source'] = find_declarations_in_source(content, filepath)

    result['namespace_issues'] = find_missing_namespace(content, filepath)

    return result

def analyze_codebase(root_dir: Path) -> Dict[str, Dict]:
    """Analyze entire codebase."""
    results = {}

    for ext in ['*.cpp', '*.c', '*.hpp', '*.h']:
        for filepath in root_dir.rglob(ext):
            if should_skip_file(filepath):
                continue

            rel_path = filepath.relative_to(root_dir)
            results[str(rel_path)] = analyze_file(filepath)

    return results

def print_report(results: Dict[str, Dict]):
    """Print analysis report."""
    print("=" * 80)
    print("CFORGE CODEBASE ANALYSIS REPORT")
    print("=" * 80)

    # Count issues
    total_bare_types = 0
    total_declarations = 0
    total_namespace = 0

    files_with_issues = []

    for filepath, analysis in results.items():
        if 'error' in analysis:
            continue

        bare_types = len(analysis.get('bare_types', []))
        declarations = len(analysis.get('declarations_in_source', []))
        namespace = len(analysis.get('namespace_issues', []))

        if bare_types or declarations or namespace:
            files_with_issues.append({
                'path': filepath,
                'bare_types': bare_types,
                'declarations': declarations,
                'namespace': namespace,
                'details': analysis
            })

        total_bare_types += bare_types
        total_declarations += declarations
        total_namespace += namespace

    print(f"\nSummary:")
    print(f"  - Files analyzed: {len(results)}")
    print(f"  - Files with issues: {len(files_with_issues)}")
    print(f"  - Bare types to replace: {total_bare_types}")
    print(f"  - Declarations in sources: {total_declarations}")
    print(f"  - Namespace issues: {total_namespace}")

    print("\n" + "-" * 80)
    print("FILES WITH BARE TYPES (top 20):")
    print("-" * 80)

    sorted_files = sorted(files_with_issues, key=lambda x: x['bare_types'], reverse=True)
    for f in sorted_files[:20]:
        if f['bare_types'] > 0:
            print(f"\n{f['path']} ({f['bare_types']} bare types)")
            for line_num, old, new in f['details']['bare_types'][:5]:
                print(f"  Line {line_num}: {old} -> {new}")
            if len(f['details']['bare_types']) > 5:
                print(f"  ... and {len(f['details']['bare_types']) - 5} more")

def replace_types_in_file(filepath: Path, dry_run: bool = True) -> int:
    """Replace bare types with cforge types in a file."""
    try:
        content = filepath.read_text(encoding='utf-8')
    except Exception as e:
        print(f"Error reading {filepath}: {e}")
        return 0

    changes = 0
    lines = content.split('\n')
    new_lines = []

    for line in lines:
        new_line = line

        if not should_skip_line(line):
            # Replace types in order of specificity (longer patterns first)
            sorted_mappings = sorted(TYPE_MAPPINGS.items(), key=lambda x: len(x[0]), reverse=True)

            for pattern, replacement in sorted_mappings:
                # Check if we should replace
                if re.search(pattern, new_line):
                    # More careful replacement - avoid function parameters and return types in STL
                    # Skip lines with std:: that use these types
                    if 'std::' in new_line:
                        continue

                    old_line = new_line
                    new_line = re.sub(pattern, replacement, new_line)
                    if old_line != new_line:
                        changes += 1

        new_lines.append(new_line)

    if changes > 0:
        new_content = '\n'.join(new_lines)
        if not dry_run:
            filepath.write_text(new_content, encoding='utf-8')
        print(f"{'Would modify' if dry_run else 'Modified'} {filepath}: {changes} replacements")

    return changes

def main():
    """Main entry point."""
    import argparse

    parser = argparse.ArgumentParser(description='Refactor cforge codebase')
    parser.add_argument('--analyze', action='store_true', help='Analyze codebase and print report')
    parser.add_argument('--replace-types', action='store_true', help='Replace bare types with cforge types')
    parser.add_argument('--dry-run', action='store_true', help='Don\'t modify files, just show what would change')
    parser.add_argument('--root', default='.', help='Root directory of codebase')

    args = parser.parse_args()

    root = Path(args.root).resolve()

    if args.analyze:
        results = analyze_codebase(root)
        print_report(results)
    elif args.replace_types:
        total_changes = 0
        for ext in ['*.cpp', '*.c']:
            for filepath in root.rglob(ext):
                if should_skip_file(filepath):
                    continue
                changes = replace_types_in_file(filepath, dry_run=args.dry_run)
                total_changes += changes
        print(f"\nTotal changes: {total_changes}")
    else:
        parser.print_help()

if __name__ == '__main__':
    main()
