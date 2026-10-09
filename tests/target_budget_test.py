#!/usr/bin/env python3
"""Control-flow regressions for the pi32v2 static loop-cost scanner."""
import tempfile
import unittest
from pathlib import Path

import target_budget as budget


class TargetBudgetFlowTest(unittest.TestCase):
    def test_cold_block_after_return_is_not_a_loop(self):
        # Actual drum_render shape: setup branches forward to an out-of-line load,
        # which jumps backward to the common render path ending at a return.
        insns = [
            (0x100, "if (r0 > 11) goto 18 <drum_render+0x16 : 116 >"),
            (0x104, "r14 = [sp+12]"),
            (0x108, "r0 = r1 + r2"),
            (0x10c, "sp += 292"),
            (0x110, "{pc, r15-r4} = [sp++]"),
            (0x116, "r14 = [sp+12]"),
            (0x11a, "goto -22 <drum_render+0x8 : 108 >"),
        ]
        self.assertEqual(budget.cost(insns)["cost"], 0)

    def test_unconditional_goto_is_a_barrier(self):
        insns = [(0x100, "goto 16 <analog_render+0x14 : 114 >"),
                 (0x104, "r0 += 1"),
                 (0x108, "goto -12 <analog_render : 100 >"),
                 (0x114, "rts")]
        self.assertEqual(budget.cost(insns)["loop"], 0)

    def test_true_loop_and_conditional_return_survive(self):
        for end in ("if (r0 == 0) {pc, r4} = [sp++]", "if (r0 == 0) rts"):
            insns = [(0x100, end), (0x104, "r1 += 1"),
                     (0x108, "if (r1 < r2) goto -12 <analog_render : 100 >"),
                     (0x10c, "rts")]
            self.assertEqual(budget.cost(insns)["cost"], 3)

    def test_nested_loop_weights_and_divide_are_unchanged(self):
        insns = [(0x100, "r0 = 0"), (0x104, "r1 = r2 / r3 (u)"),
                 (0x108, "if (r1 < r2) goto -8 <analog_render+0x4 : 104 >"),
                 (0x10c, "r0 += 1"),
                 (0x110, "if (r0 < r4) goto -20 <analog_render : 100 >"),
                 (0x114, "rts")]
        result = budget.cost(insns)
        self.assertEqual(result["loop"], 5)
        self.assertEqual(result["div"], 1)
        self.assertEqual(result["cost"], 1 + 9 * 4 + 4 + 1 + 1)

    def test_jump_tables_remain_conservative(self):
        for instruction in ("tbb [r4]", "tbh [r2]", "goto r2", "pc = r2"):
            insns = [(0x100, instruction), (0x104, "rts"),
                     (0x108, "goto -12 <analog_render : 100 >")]
            self.assertEqual(budget.cost(insns)["loop"], 3)

    def test_listing_predicated_return_keeps_false_path(self):
        listing = """analog_render:
 100:    30 e8 04 40       if (r0 == 4) {
 104:    80 00                rts
                         }
 106:    41 21             r1 += 1
 108:    00 00             if (r1 < r2) goto -12 <analog_render : 100 >
 10c:    00 04             pc = [sp++]
"""
        with tempfile.TemporaryDirectory() as directory:
            filename = Path(directory) / "target.dis"
            filename.write_text(listing)
            parsed = budget.functions(filename)["analog_render"]
        self.assertEqual(budget.cost(parsed)["cost"], 4)


if __name__ == "__main__":
    unittest.main()
