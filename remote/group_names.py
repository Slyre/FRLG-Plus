"""Decode the international game's trainer names; names never identify a seat."""
import unicodedata

_CHARS = {
    0x00: ' ', 0x01: 'À', 0x02: 'Á', 0x03: 'Â', 0x04: 'Ç',
    0x05: 'È', 0x06: 'É', 0x07: 'Ê', 0x08: 'Ë', 0x09: 'Ì',
    0x0B: 'Î', 0x0C: 'Ï', 0x0D: 'Ò', 0x0E: 'Ó', 0x0F: 'Ô',
    0x10: 'Œ', 0x11: 'Ù', 0x12: 'Ú', 0x13: 'Û', 0x14: 'Ñ',
    0x15: 'ß', 0x16: 'à', 0x17: 'á', 0x19: 'ç', 0x1A: 'è',
    0x1B: 'é', 0x1C: 'ê', 0x1D: 'ë', 0x1E: 'ì', 0x20: 'î',
    0x21: 'ï', 0x22: 'ò', 0x23: 'ó', 0x24: 'ô', 0x25: 'œ',
    0x26: 'ù', 0x27: 'ú', 0x28: 'û', 0x29: 'ñ', 0x2A: 'º',
    0x2B: 'ª', 0x2D: '&', 0x2E: '+', 0x35: '=', 0x36: ';',
    0x51: '¿', 0x52: '¡', 0x5A: 'Í', 0x5B: '%', 0x5C: '(',
    0x5D: ')', 0x68: 'â', 0x6F: 'í', 0x85: '<', 0x86: '>',
    0xAB: '!', 0xAC: '?', 0xAD: '.', 0xAE: '-', 0xAF: '·',
    0xB0: '…', 0xB1: '“', 0xB2: '”', 0xB3: '‘', 0xB4: '’',
    0xB5: '♂', 0xB6: '♀', 0xB7: '¥', 0xB8: ',', 0xB9: '×',
    0xBA: '/', 0xF0: ':', 0xF1: 'Ä', 0xF2: 'Ö', 0xF3: 'Ü',
    0xF4: 'ä', 0xF5: 'ö', 0xF6: 'ü',
}
_CHARS.update({0xA1 + i: c for i, c in enumerate('0123456789')})
_CHARS.update({0xBB + i: c for i, c in enumerate('ABCDEFGHIJKLMNOPQRSTUVWXYZ')})
_CHARS.update({0xD5 + i: c for i, c in enumerate('abcdefghijklmnopqrstuvwxyz')})


def decode_trainer_name(raw):
    """Read a bounded, terminated 7-character name, without control sequences."""
    if not isinstance(raw, (bytes, bytearray)) or len(raw) != 8:
        raise ValueError('Invalid trainer name buffer')
    end = raw.find(b'\xff')
    if end < 0 or end > 7:
        raise ValueError('Unterminated trainer name')
    return ''.join(_CHARS.get(value, '�') for value in raw[:end]).strip()


def trainer_labels(names):
    """Disambiguate equal names without making display text a network identity."""
    if not isinstance(names, (list, tuple)) or len(names) != 4:
        raise ValueError('Four trainer names are required')
    if any(not isinstance(name, str) or len(name) > 7 for name in names):
        raise ValueError('Invalid trainer name')
    clean = [name.strip() for name in names]
    keys = [unicodedata.normalize('NFC', name).casefold() for name in clean]
    return [((name + f' ({i + 1})') if keys.count(keys[i]) > 1 else name)
            if name else f'Trainer {i + 1}' for i, name in enumerate(clean)]
