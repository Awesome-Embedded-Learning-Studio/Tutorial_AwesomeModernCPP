"""ELF64 x86-64 + initial JSON -> asm-executor/payload@1. Python standard library only."""
import argparse
import base64
import json
import re
import struct
import subprocess
from pathlib import Path


def make_payload(elf_path, initial, entry, stops, title, max_steps=10000):
    raw = Path(elf_path).read_bytes()
    if raw[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', raw, 18)[0] != 62:
        raise ValueError('only little-endian ELF64 x86-64 is supported')
    phoff = struct.unpack_from('<Q', raw, 32)[0]
    phsize, phnum = struct.unpack_from('<HH', raw, 54)
    pages = {}
    for i in range(phnum):
        kind, flags, offset, addr, _, filesz, memsz, _ = struct.unpack_from('<IIQQQQQQ', raw, phoff + i * phsize)
        if kind != 1 or not memsz:
            continue
        if filesz > memsz or offset + filesz > len(raw) or memsz > 16777216:
            raise ValueError('invalid or oversized ELF segment')
        for page in range(addr // 4096 * 4096, (addr + memsz + 4095) // 4096 * 4096, 4096):
            item = pages.setdefault(page, {'data': bytearray(4096), 'flags': 0})
            item['flags'] |= flags
        for j, byte in enumerate(raw[offset:offset + filesz]):
            pos = addr + j
            pages[pos // 4096 * 4096]['data'][pos % 4096] = byte
    memory = []
    for addr, page in sorted(pages.items()):
        perms = ''.join(c for c, flag in [('r', 4), ('w', 2), ('x', 1)] if page['flags'] & flag)
        memory.append({'addr': hex(addr), 'size': 4096, 'perms': perms, 'bytes': base64.b64encode(page['data']).decode()})
    memory += initial.get('memory', [])
    symbols = {}
    for line in subprocess.check_output(['nm', '-n', str(elf_path)], text=True).splitlines():
        parts = line.split()
        if len(parts) >= 3 and re.fullmatch('[0-9a-fA-F]+', parts[0]):
            symbols[parts[2]] = int(parts[0], 16)
    def resolve(value):
        return symbols[value] if value in symbols else int(value, 0)
    code = []
    for line in subprocess.check_output(['objdump', '-d', '-w', '-M', 'att', str(elf_path)], text=True).splitlines():
        match = re.match(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\s*(.*)$', line)
        if match:
            code.append({'addr': hex(int(match[1], 16)), 'bytes': ''.join(match[2].split()), 'text': match[3].strip()})
    return {'format': 'asm-executor/payload@1', 'meta': {'isa': 'x86-64', 'syntax': 'att', 'title': title, 'entry': hex(resolve(entry))},
            'code': code, 'initial': {'regs': initial['regs'], 'memory': memory},
            'execution': {'stop_before': [hex(resolve(stop)) for stop in stops], 'max_steps': max_steps},
            'ui': initial.get('ui', {'watch_regs': ['rip', 'rsp', 'rbp', 'rax', 'edi', 'esi', 'rflags'], 'stack_window': {'around': 'rsp', 'rows': 8}})}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True)
    parser.add_argument('--initial', required=True)
    parser.add_argument('--entry', required=True, help='ELF symbol or 0x address')
    parser.add_argument('--stop-before', required=True, action='append', help='repeat for multiple exits')
    parser.add_argument('--title', default='汇编执行器')
    parser.add_argument('--max-steps', type=int, default=10000)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    if not 1 <= args.max_steps <= 10000:
        parser.error('--max-steps must be 1..10000')
    payload = make_payload(args.elf, json.loads(Path(args.initial).read_text()), args.entry, args.stop_before, args.title, args.max_steps)
    Path(args.out).write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
