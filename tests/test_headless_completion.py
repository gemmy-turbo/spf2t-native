"""Exercise completed-match idle frames with a locally built game and ROM data.

Set PF_NATIVE_GAME and PF_DECOMP_DATA to run these integration tests. Game
content and user state stay outside the source repository.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


GAME = os.environ.get("PF_NATIVE_GAME")
DATA = os.environ.get("PF_DECOMP_DATA")


@unittest.skipUnless(GAME and DATA, "Set PF_NATIVE_GAME and PF_DECOMP_DATA")
class HeadlessCompletionTests(unittest.TestCase):
    def run_game(self, seed, frames):
        with tempfile.TemporaryDirectory() as state:
            result = subprocess.run(
                [str(Path(GAME).resolve()), "--data-dir", str(Path(DATA).resolve()),
                 "--user-dir", state, "--no-save-options", "--headless",
                 "--exercise", "--seed", str(seed), "--frames", str(frames)],
                capture_output=True, text=True, timeout=30,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_finished_match_can_remain_idle_without_changing_game_state(self):
        before = self.run_game(1234, 12371)
        self.assertTrue(before["match"]["finished"])
        for frames in (12372, 36000):
            with self.subTest(frames=frames):
                self.assertEqual(self.run_game(1234, frames), before)

    def test_other_reported_seeds_survive_extended_idle(self):
        for seed in (1, 99, 4321):
            with self.subTest(seed=seed):
                self.run_game(seed, 36000)


if __name__ == "__main__":
    unittest.main()
