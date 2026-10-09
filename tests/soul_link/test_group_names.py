import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('group_names', Path(__file__).resolve().parents[2] / 'remote/group_names.py')
names = importlib.util.module_from_spec(spec)
spec.loader.exec_module(names)


class TrainerNames(unittest.TestCase):
    def test_international_name_and_terminator(self):
        self.assertEqual(names.decode_trainer_name(bytes([0x06, 0xE0, 0xD5, 0xFF, 0xFC, 0, 0, 0])), 'Éla')
        self.assertEqual(names.decode_trainer_name(b'\xff' * 8), '')
        self.assertEqual(names.decode_trainer_name(bytes([0xBB] * 7 + [0xFF])), 'AAAAAAA')

    def test_invalid_memory_buffers(self):
        for raw in (b'', b'\xbb' * 8, 'ASH', [0xFF] * 8):
            with self.assertRaises(ValueError):
                names.decode_trainer_name(raw)

    def test_equal_names_stable_seats_and_missing(self):
        self.assertEqual(names.trainer_labels(['ASH', 'MISTY', 'ash', '']),
                         ['ASH (1)', 'MISTY', 'ash (3)', 'Trainer 4'])
        self.assertEqual(names.trainer_labels(['ÉLI', 'E\u0301LI', 'BROCK', 'RED']),
                         ['ÉLI (1)', 'E\u0301LI (2)', 'BROCK', 'RED'])


if __name__ == '__main__':
    unittest.main()
