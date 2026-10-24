"""统一场景构建器：按场景目录里的 manifest.json 生成 payload.json。

场景目录只需两样东西：manifest.json（标题/入口/源文件清单/验收断言）与手写汇编源。
boot.s 与 layout.ld 由 _common/ 统一提供；entry 与 __stop 的实际地址用 nm 从
链接产物读取，不手写（避免函数排序变化后 initial 的 RIP 与 entry 冲突）。
产物（.o/.elf/initial.json/payload.json）留在场景目录，不入库。
"""
import base64
import json
import subprocess
import sys
from pathlib import Path

PAYLOADS_ROOT = Path(__file__).resolve().parent
COMMON = PAYLOADS_ROOT / '_common'
TOOL = PAYLOADS_ROOT.parents[3] / 'site' / 'tools' / 'asm-playground'
REGS = ['rax', 'rbx', 'rcx', 'rdx', 'rsi', 'rdi', 'rbp', 'rsp',
        'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15', 'rip', 'rflags']

STACK = {'map': '0x7fffffffc000', 'size': 8192, 'top': '0x7fffffffd0d8', 'frame': '0x7fffffffd0f0'}


def nm_addr(elf: Path, symbol: str) -> int:
    out = subprocess.check_output(['nm', str(elf)], text=True)
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == symbol:
            return int(parts[0], 16)
    raise SystemExit(f'符号 {symbol} 不在 {elf} 中')


def build(scene: Path) -> None:
    manifest = json.loads((scene / 'manifest.json').read_text())
    entry = manifest.get('entry', 'main')
    stop = manifest.get('stop_before', '__stop')

    objects = []
    for source in manifest['sources']:
        obj = scene / (Path(source).stem + '.o')
        subprocess.run(['as', '--64', source, '-o', obj.name], cwd=scene, check=True)
        objects.append(obj.name)
    subprocess.run(['as', '--64', str(COMMON / 'boot.s'), '-o', 'boot.o'], cwd=scene, check=True)
    elf = scene / (scene.name + '.elf')
    subprocess.run(['ld', '-T', str(COMMON / 'layout.ld'), *objects, 'boot.o', '-o', elf.name], cwd=scene, check=True)

    entry_addr = nm_addr(elf, entry)
    stop_addr = nm_addr(elf, stop)
    stack = bytearray(STACK['size'])
    top = int(STACK['top'], 16) - int(STACK['map'], 16)
    stack[top:top + 8] = stop_addr.to_bytes(8, 'little')
    regs = {name: '0x0' for name in REGS}
    regs.update(rsp=STACK['top'], rbp=STACK['frame'], rip=hex(entry_addr), rflags='0x202')
    (scene / 'initial.json').write_text(json.dumps({
        'regs': regs,
        'memory': [{'addr': STACK['map'], 'size': STACK['size'], 'perms': 'rw',
                    'bytes': base64.b64encode(stack).decode()}],
    }, indent=2) + '\n')

    subprocess.run([
        sys.executable, '-m', 'tools.make_payload',
        '--elf', str(elf), '--initial', str(scene / 'initial.json'),
        '--entry', entry, '--stop-before', stop,
        '--title', manifest['title'], '--out', str(scene / 'payload.json'),
    ], cwd=TOOL, check=True)
    print(f'[{scene.name}] payload.json built（entry {entry} @ {hex(entry_addr)}）')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('用法：build_scene.py <场景目录或场景名>')
    arg = Path(sys.argv[1])
    build(arg if arg.is_absolute() else PAYLOADS_ROOT / arg)
