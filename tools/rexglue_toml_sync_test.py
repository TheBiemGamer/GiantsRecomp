"""Unit tests for tools/rexglue_toml_sync.py. Stdlib only.

Run with: python3 -m unittest tools.rexglue_toml_sync_test
"""

import unittest

from rexglue_toml_sync import (
    SyncResult,
    decimal_size,
    find_functions_section,
    format_entry,
    parse_entry,
    quoted_name,
    sync_functions,
)


SAMPLE_LINES = [
    "# header comment\n",
    "[[switch_tables]]\n",
    "address = 0x1000\n",
    "\n",
    "[functions]\n",
    "# leaf reached only through vtable calls\n",
    "0x822B9D58 = { size = 12 }\n",
    "\n",
    "# chunk of the function above\n",
    "0x822B9D70 = { parent = 0x822B9D58, size = 8 }\n",
]

SAMPLE_LINES_EOF = [
    "[functions]\n",
    "0x822B9D58 = { size = 12 }\n",
]


class ParseFormatTests(unittest.TestCase):
    def test_parse_entry_size_only(self):
        self.assertEqual(
            parse_entry("0x822B9D58 = { size = 12 }\n"),
            (0x822B9D58, "0x822B9D58", {"size": "12"}),
        )

    def test_parse_entry_non_matching_line_returns_none(self):
        self.assertIsNone(parse_entry("# just a comment\n"))
        self.assertIsNone(parse_entry("[functions]\n"))

    def test_format_entry_key_order(self):
        self.assertEqual(
            format_entry("0x1", {"size": "12", "name": '"Foo"', "parent": "0x2"}),
            '0x1 = { name = "Foo", parent = 0x2, size = 12 }\n',
        )

    def test_format_entry_empty_props(self):
        self.assertEqual(format_entry("0x1", {}), "0x1 = {}\n")


class HelperTests(unittest.TestCase):
    def test_quoted_name_sanitizes(self):
        self.assertEqual(quoted_name("Update Camera!"), '"Update_Camera"')

    def test_quoted_name_leading_digit(self):
        self.assertEqual(quoted_name("2ndFunc"), '"_2ndFunc"')

    def test_decimal_size(self):
        self.assertEqual(decimal_size(12), "12")


class FindSectionTests(unittest.TestCase):
    def test_finds_bounded_section(self):
        start, end = find_functions_section(SAMPLE_LINES)
        self.assertEqual(SAMPLE_LINES[start].strip(), "[functions]")
        self.assertEqual(end, len(SAMPLE_LINES))

    def test_section_can_run_to_end_of_file(self):
        start, end = find_functions_section(SAMPLE_LINES_EOF)
        self.assertEqual(start, 0)
        self.assertEqual(end, len(SAMPLE_LINES_EOF))

    def test_missing_section_raises(self):
        with self.assertRaises(ValueError):
            find_functions_section(["[other]\n", "x = 1\n"])


class SyncFunctionsTests(unittest.TestCase):
    def test_update_existing_entry_preserves_other_keys(self):
        result = sync_functions(
            list(SAMPLE_LINES),
            {0x822B9D70: {"name": '"ChunkFunc"'}},
        )
        self.assertEqual(result.updated, [0x822B9D70])
        updated_line = next(l for l in result.lines if l.startswith("0x822B9D70"))
        self.assertIn("parent = 0x822B9D58", updated_line)
        self.assertIn("size = 8", updated_line)
        self.assertIn('name = "ChunkFunc"', updated_line)

    def test_adds_new_entry_sorted_at_end_of_section(self):
        result = sync_functions(
            list(SAMPLE_LINES),
            {0x82500000: {"name": '"NewFunc"', "size": "40"}},
        )
        self.assertEqual(result.added, [0x82500000])
        self.assertEqual(
            result.lines[-1], '0x82500000 = { name = "NewFunc", size = 40 }\n'
        )

    def test_section_runs_to_end_of_file(self):
        result = sync_functions(
            list(SAMPLE_LINES_EOF),
            {0x82600000: {"name": '"Tail"'}},
        )
        self.assertEqual(result.added, [0x82600000])
        self.assertEqual(result.lines[-1], '0x82600000 = { name = "Tail" }\n')

    def test_idempotent_rerun_no_changes(self):
        first = sync_functions(list(SAMPLE_LINES), {0x822B9D58: {"name": '"Leaf"'}})
        second = sync_functions(list(first.lines), {0x822B9D58: {"name": '"Leaf"'}})
        self.assertEqual(second.updated, [])
        self.assertEqual(second.added, [])
        self.assertEqual(second.lines, first.lines)

    def test_missing_section_raises(self):
        with self.assertRaises(ValueError):
            sync_functions(["[other]\n"], {0x1: {"name": '"X"'}})

    def test_duplicate_name_collision_reported_and_skipped(self):
        lines = list(SAMPLE_LINES)
        result = sync_functions(
            lines,
            {
                0x822B9D58: {"name": '"SameName"'},
                0x822B9D70: {"name": '"SameName"'},
            },
        )
        names = [l for l in result.lines if "SameName" in l]
        self.assertEqual(len(names), 1)
        self.assertEqual(len(result.conflicts), 1)
        conflict_name, new_addr, existing_addr = result.conflicts[0]
        self.assertEqual(conflict_name, '"SameName"')


if __name__ == "__main__":
    unittest.main()
