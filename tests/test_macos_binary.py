"""Deployment checks must catch dependencies accidentally built on a newer Mac."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/macos'))
from check_binary import check_binary


class MacBinaryTests(unittest.TestCase):
    def test_modern_and_legacy_load_commands(self):
        for command in ('cmd LC_BUILD_VERSION\n cmdsize 32\n platform 1\n minos 11.0\n sdk 26.0',
                        'cmd LC_VERSION_MIN_MACOSX\n cmdsize 16\n version 10.13\n sdk 13.1'):
            with patch('subprocess.check_output', return_value=command):
                check_binary('fixture', '11.0')

    def test_rejects_newer_minimum_and_missing_metadata(self):
        for command in ('cmd LC_BUILD_VERSION\n minos 26.0\n', 'cmd LC_SEGMENT_64\n'):
            with patch('subprocess.check_output', return_value=command):
                with self.assertRaises(RuntimeError):
                    check_binary('fixture', '11.0')

    def test_rejects_homebrew_dylib(self):
        outputs = ['cmd LC_BUILD_VERSION\n minos 11.0\n',
                   'fixture:\n\t/opt/homebrew/lib/libpng.dylib (compatibility version 1.0.0)\n']
        with patch('subprocess.check_output', side_effect=outputs):
            with self.assertRaisesRegex(RuntimeError, 'unexpected dependency'):
                check_binary('fixture', '11.0', system_libraries_only=True)


if __name__ == '__main__':
    unittest.main()
