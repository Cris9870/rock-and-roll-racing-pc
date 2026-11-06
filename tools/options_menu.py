"""PC settings in the game's own OPTIONS screen (garage -> SET OPTIONS).

The stock screen (bank $01, $BC17-$BEF3, always interpreted: none of it is in
src/gen) has MUSIC / SOUND / LARRY / EXIT in a small panel between four car
bubbles. This patch turns it into one Spanish list:

    OPCIONES: MUSICA, SONIDO, LARRY, FORMATO, PANTALLA, FILTRO, MOVIMIENTO, SALIR

The four PC rows keep their value in a WRAM mailbox at $7F:FFF0 that the host
reads after every frame (src/rnrr_options.c):

    $7F:FFF0  FORMATO     0 4:3, 1 16:9, 2 21:9, 3 AJUSTAR
    $7F:FFF1  PANTALLA    0 COMPLETA, 1 VENTANA
    $7F:FFF2  FILTRO      0 SUAVIZADA, 1 NITIDA, 2 CRT, 3 TV CLASICA
    $7F:FFF3  MOVIMIENTO  0 SUAVE, 1 ORIGINAL
    $7F:FFF4  $A5 when the menu changed a value (the host applies and clears it)
    $7F:FFF5  1 while the screen is open (the host then widens the panel)

The host widens the top panel (BG1 tilemap) over the bubbles; the bubbles'
cars are parked off screen. MUSICA also restarts the music when it is turned
back on (the stock screen left it stopped). All code and text here is
original; the only ROM bytes in this file are the short signatures checked
before patching.

Usage:
    options_menu.py --write-inc src/rnrr_options_patch.inc
    options_menu.py --check <rom>   (apply to a copy in memory and disassemble)
tools/patch_rom.py imports apply() so regen sees the same image.
"""
import os
import sys

FREE_START = 0x01FA10
FREE_END = 0x01FF10          # the widescreen sprite patch starts here
MAILBOX = 0x7FFFF0
DIRTY = MAILBOX + 4
ACTIVE = MAILBOX + 5

LABEL_COL = 6                # labels; the title is padded to sit centred
VALUE_COL = 18
CURSOR_X = LABEL_COL * 8 - 16
TITLE_ROW = 3
ROWS = [5, 6, 7, 9, 10, 11, 12, 14]   # MUSICA .. SALIR


class Asm:
    def __init__(self, org):
        self.org = org
        self.buf = bytearray()
        self.labels = {}
        self.fixups = []

    @property
    def pc(self):
        return self.org + len(self.buf)

    def label(self, name):
        assert name not in self.labels, name
        self.labels[name] = self.pc

    def raw(self, *bs):
        self.buf += bytes(bs)

    def _ref(self, kind, target):
        if isinstance(target, int):
            v = target
            if kind == "abs":
                self.buf += bytes([v & 0xFF, (v >> 8) & 0xFF])
            elif kind == "long":
                self.buf += bytes([v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF])
            else:
                raise ValueError(kind)
            return
        self.fixups.append((kind, len(self.buf), target))
        self.buf += b"\0" * {"abs": 2, "long": 3, "rel": 1}[kind]

    def op(self, opcode, kind=None, target=None):
        self.raw(opcode)
        if kind:
            self._ref(kind, target)

    def imm8(self, opcode, v):
        self.raw(opcode, v & 0xFF)

    def imm16(self, opcode, v):
        if isinstance(v, int):
            self.raw(opcode, v & 0xFF, (v >> 8) & 0xFF)
        else:
            self.op(opcode, "abs", v)

    def string(self, text):
        for ch in text:
            c = 0x0D if ch == "\r" else ord(ch)
            assert c == 0x0D or 0x20 <= c <= 0x5F, ch
            self.buf.append(c)
        self.buf.append(0)

    def resolve(self):
        for kind, pos, name in self.fixups:
            addr = self.labels[name]
            if kind == "rel":
                delta = addr - (self.org + pos + 1)
                assert -128 <= delta <= 127, (name, delta)
                self.buf[pos] = delta & 0xFF
            elif kind == "abs":
                assert addr >> 16 == self.org >> 16, name
                self.buf[pos:pos + 2] = bytes([addr & 0xFF, (addr >> 8) & 0xFF])
            else:
                self.buf[pos:pos + 3] = bytes([addr & 0xFF, (addr >> 8) & 0xFF, addr >> 16])
        return bytes(self.buf)


# 65816 opcodes used below
LDA_IMM, LDX_IMM, LDY_IMM = 0xA9, 0xA2, 0xA0
LDA_ABS, LDX_ABS, STA_ABS = 0xAD, 0xAE, 0x8D
LDA_ABS_X, CMP_ABS_X = 0xBD, 0xDD
LDA_LONG, STA_LONG = 0xAF, 0x8F
ADC_LONG_X, STA_LONG_X = 0x7F, 0x9F
AND_IMM, CMP_IMM, ADC_IMM, SBC_IMM = 0x29, 0xC9, 0x69, 0xE9
JMP, JSR, JSL, JML = 0x4C, 0x20, 0x22, 0x5C
BCC, BCS, BPL, BRA, BEQ = 0x90, 0xB0, 0x10, 0x80, 0xF0
EOR_IMM = 0x49
RTS, RTL = 0x60, 0x6B
PHA, PLA, PHY, PLY, PHP, PLP = 0x48, 0x68, 0x5A, 0x7A, 0x08, 0x28
PHX, PLX, ADC_SR = 0xDA, 0xFA, 0x63
TAX, TYA, SEC, CLC, ASL_A, DEC_A = 0xAA, 0x98, 0x38, 0x18, 0x0A, 0x3A
SEP, REP = 0xE2, 0xC2
STA_DP = 0x85

PRINT_ENTRY = 0xE102     # $01:E0FA after it has stored the string pointer in $04

VALUES = [  # (mailbox offset, strings); every string padded to its row's widest
    ["4:3", "16:9", "21:9", "AJUSTAR"],
    ["COMPLETA", "VENTANA"],
    ["SUAVIZADA", "NITIDA", "CRT", "TV CLASICA"],
    ["SUAVE", "ORIGINAL"],
]
TITLE = " " * ((32 - 8) // 2 - LABEL_COL) + "OPCIONES"


def labels_text():
    names = ["MUSICA", "SONIDO", "LARRY", "FORMATO", "PANTALLA", "FILTRO", "MOVIMIENTO", "SALIR"]
    out, row = TITLE, TITLE_ROW
    for name, r in zip(names, ROWS):
        out += "\r" * (r - row) + name
        row = r
    return out


def build():
    a = Asm(FREE_START)

    # print_ptr: A = string address (bank $01), X = column, Y = row. 16-bit.
    a.label("setup_print")                   # the title print: also mark the screen open
    a.raw(PHA)
    a.imm8(SEP, 0x20)
    a.imm8(LDA_IMM, 0x01)
    a.op(STA_LONG, "long", ACTIVE)
    a.imm8(REP, 0x20)
    a.raw(PLA)
    a.label("print_ptr")
    a.raw(PHY)
    a.imm8(STA_DP, 0x04)
    a.op(JMP, "abs", PRINT_ENTRY)

    # beep_keep_y: the menu beep clobbers Y, which holds the direction (+1/-1).
    a.label("beep_keep_y")
    a.raw(PHY)
    a.op(JSL, "long", 0x80FB6E)
    a.raw(PLY)
    a.raw(RTL)

    # dispatch: replaces "LDA $1394" before the toggle chain. 8-bit A/X/Y.
    a.label("dispatch")
    a.op(LDA_ABS, "abs", 0x1394)
    a.imm8(CMP_IMM, 3)
    a.op(BCS, "rel", "d_pc")
    a.op(LDA_ABS, "abs", 0x1394)             # Z for the stock BEQ chain
    a.op(JMP, "abs", 0xBD75)
    a.label("d_pc")
    a.imm8(CMP_IMM, 7)
    a.op(BCC, "rel", "d_set")
    a.imm8(LDA_IMM, 1)                       # what the stock code holds at EXIT
    a.op(JMP, "abs", 0xBDBF)
    a.label("d_set")
    a.raw(SEC)
    a.imm8(SBC_IMM, 3)
    a.raw(TAX)
    a.raw(TYA)
    a.raw(CLC)
    a.op(ADC_LONG_X, "long", MAILBOX)
    a.op(BPL, "rel", "d_up")
    a.op(LDA_ABS_X, "abs", "counts")
    a.raw(DEC_A)
    a.op(BRA, "rel", "d_store")
    a.label("d_up")
    a.op(CMP_ABS_X, "abs", "counts")
    a.op(BCC, "rel", "d_store")
    a.imm8(LDA_IMM, 0)
    a.label("d_store")
    a.op(STA_LONG_X, "long", MAILBOX)
    a.imm8(LDA_IMM, 0xA5)
    a.op(STA_LONG, "long", DIRTY)
    a.op(JMP, "abs", 0xBDB5)                 # reprint the values, back to the loop
    a.label("counts")
    a.raw(*[len(v) for v in VALUES])

    # music_toggle: the MUSICA row. Off as the stock code does it (flush the
    # sound queue, stop); on queues "play" again ($80:F942 with X=0: no reload).
    a.label("music_toggle")
    a.op(LDA_ABS, "abs", 0x02E8)
    a.imm8(EOR_IMM, 0x01)
    a.op(STA_ABS, "abs", 0x02E8)
    a.op(BEQ, "rel", "m_on")
    a.op(JSL, "long", 0x80F9AD)
    a.op(JSL, "long", 0x80F95B)
    a.raw(RTL)
    a.label("m_on")
    a.imm8(LDX_IMM, 0)
    a.op(JSL, "long", 0x80F942)
    a.raw(RTL)

    # exit_hook: replaces "JSL $80F9AD" on the way out; the screen is closed.
    a.label("exit_hook")
    a.raw(PHP)
    a.raw(PHA)
    a.imm8(LDA_IMM, 0)
    a.op(STA_LONG, "long", ACTIVE)
    a.raw(PLA)
    a.raw(PLP)
    a.op(JML, "long", 0x80F9AD)

    # cursor_pos: replaces the cursor's "LDX $1394; LDA $BEF0,X; STA $13D5".
    a.label("cursor_pos")
    a.op(LDX_ABS, "abs", 0x1394)
    a.op(LDA_ABS_X, "abs", "cursor_y")
    a.op(STA_ABS, "abs", 0x13D5)
    a.imm8(LDA_IMM, CURSOR_X)
    a.op(STA_ABS, "abs", 0x13D4)
    a.raw(RTS)
    a.label("cursor_y")
    a.raw(*[r * 8 for r in ROWS])

    # print_values: replaces the stock ON/OFF printer. 16-bit A/X/Y.
    a.label("print_values")
    for flag, row in ((0x02E8, ROWS[0]), (0x02E7, ROWS[1]), (0x02E9, ROWS[2])):
        a.op(LDA_ABS, "abs", flag)
        a.imm16(AND_IMM, 0x0001)
        a.imm16(LDY_IMM, row)
        a.imm16(LDX_IMM, "yesno")
        a.op(JSR, "abs", "pv_item")
    for i, row in enumerate(ROWS[3:7]):
        a.op(LDA_LONG, "long", MAILBOX + i)
        a.imm16(AND_IMM, 0x00FF)
        a.imm16(CMP_IMM, len(VALUES[i]))
        a.op(BCC, "rel", f"pv_ok{i}")
        a.imm16(LDA_IMM, 0)
        a.label(f"pv_ok{i}")
        a.imm16(LDY_IMM, row)
        a.imm16(LDX_IMM, f"tab{i}")
        a.op(JSR, "abs", "pv_item")
    a.raw(RTS)
    # pv_item: A = value, X = its string table, Y = row. X = table + 2 * value,
    # summed on the stack so no direct-page byte is borrowed.
    a.label("pv_item")
    a.raw(ASL_A)
    a.raw(PHX)
    a.raw(CLC)
    a.imm8(ADC_SR, 1)                         # ADC 1,S
    a.raw(PLX)
    a.raw(TAX)
    a.op(LDA_ABS_X, "abs", 0x0000)
    a.imm16(LDX_IMM, VALUE_COL)
    a.op(JMP, "abs", "print_ptr")

    # text
    a.label("labels")
    a.string(labels_text())
    a.label("clave")
    a.string("CLAVE")
    a.label("jugador")
    a.string("JUGADOR")
    a.label("yesno")
    a.raw(0, 0, 0, 0)
    for i, vals in enumerate(VALUES):
        a.label(f"tab{i}")
        a.raw(*([0, 0] * len(vals)))
    width = max(len(s) for s in ("SI", "NO"))
    for s in ("SI", "NO"):
        a.label("s_" + s)
        a.string(s.ljust(width))
    for i, vals in enumerate(VALUES):
        width = max(len(s) for s in vals)
        for j, s in enumerate(vals):
            a.label(f"v{i}_{j}")
            a.string(s.ljust(width))
    code = bytearray(a.resolve())

    def put16(label, value_label):
        pos = a.labels[label] - FREE_START
        v = a.labels[value_label]
        code[pos:pos + 2] = bytes([v & 0xFF, (v >> 8) & 0xFF])

    put16("yesno", "s_SI")
    pos = a.labels["yesno"] - FREE_START + 2
    v = a.labels["s_NO"]
    code[pos:pos + 2] = bytes([v & 0xFF, (v >> 8) & 0xFF])
    for i, vals in enumerate(VALUES):
        for j in range(len(vals)):
            pos = a.labels[f"tab{i}"] - FREE_START + 2 * j
            v = a.labels[f"v{i}_{j}"]
            code[pos:pos + 2] = bytes([v & 0xFF, (v >> 8) & 0xFF])
    assert FREE_START + len(code) <= FREE_END, hex(FREE_START + len(code))
    L = a.labels

    def lo(name):
        return L[name] & 0xFF

    def hi(name):
        return (L[name] >> 8) & 0xFF

    h = bytes.fromhex
    sites = [
        # cursor starts on SALIR
        (0x01BC1F, h("a903"), bytes([0xA9, 7])),
        # title + labels: our string, label column, setup_print
        (0x01BC4A, h("a97200a20b00a0030020fae0"),
         bytes([0xA9, lo("labels"), hi("labels"), 0xA2, LABEL_COL, 0, 0xA0, TITLE_ROW, 0,
                0x20, lo("setup_print"), hi("setup_print")])),
        # no bubble cars: skip loading their graphics ($907C x4, BRA $BCA7) and
        # park their sprites off screen. The screen's sprite table is
        # [5][5 OAM entries][2][2 high-table bytes]; the cursor (entry 0) stays,
        # the cars get X=$80 with the 9th X bit set (X=384, past a 64 px sprite).
        (0x01BC77, h("a92000a20300a0000020" "7c90"), h("802e")),
        (0x01BCA7, h("e230"), b""),
        (0x01A98F, h("05" "48700038" "04092070" "054a2872" "be0a8035" "bd468837" "02a802"),
         h("05" "48700038" "80092070" "804a2872" "800a8035" "80468837" "02fc03")),
        # MUSICA back on: the stock code only stops the music when it goes off
        # and leaves it stopped until the screen closes; music_toggle also
        # restarts it when it comes back on. JSL music_toggle; BRA $BDB5.
        (0x01BD7F, h("ade8024901" "8de802" "f02c" "22adf980" "225bf980" "8022"),
         bytes([0x22, lo("music_toggle"), hi("music_toggle"), 0x01, 0x80, 0x30])),
        # beep that keeps Y, then our dispatch
        (0x01BD6E, h("226efb80ad9413"),
         bytes([0x22, lo("beep_keep_y"), hi("beep_keep_y"), 0x01, 0x4C, lo("dispatch"), hi("dispatch")])),
        # leaving: clear the open flag
        (0x01BDBF, h("22adf980"), bytes([0x22, lo("exit_hook"), hi("exit_hook"), 0x01])),
        # values printer
        (0x01BDCD, h("ade802"), bytes([0x4C, lo("print_values"), hi("print_values")])),
        # password panel: CLAVE heading, JUGADOR 1 (digit one column right)
        (0x01BE18, h("a97800a20c00a0130020fae0a90000a20400a0150020fae0a93100a20b00"),
         bytes([0xA9, lo("clave"), hi("clave"), 0xA2, 0x0D, 0, 0xA0, 0x13, 0,
                0x20, lo("print_ptr"), hi("print_ptr"),
                0xA9, lo("jugador"), hi("jugador"), 0xA2, 0x04, 0, 0xA0, 0x15, 0,
                0x20, lo("print_ptr"), hi("print_ptr"),
                0xA9, 0x31, 0, 0xA2, 0x0C, 0])),
        (0x01BE65, h("a90000a20400a0170020fae0a93200a20b00"),
         bytes([0xA9, lo("jugador"), hi("jugador"), 0xA2, 0x04, 0, 0xA0, 0x17, 0,
                0x20, lo("print_ptr"), hi("print_ptr"),
                0xA9, 0x32, 0, 0xA2, 0x0C, 0])),
        # password characters one column right, past the longer JUGADOR
        (0x01BE9C, h("0d000e000f00100012001300140015001700180019001a001b00"),
         bytes(sum(([c + 1, 0] for c in (0x0D, 0x0E, 0x0F, 0x10, 0x12, 0x13, 0x14, 0x15,
                                          0x17, 0x18, 0x19, 0x1A, 0x1B)), []))),
        # eight rows for the cursor's wrap (down wraps at X, up goes to X)
        (0x01BEB6, h("a20420c4a5a203"), h("a20820c4a5a207")),
        # cursor position (X and Y)
        (0x01BECB, h("ae9413bdf0be8dd51360"), bytes([0x4C, lo("cursor_pos"), hi("cursor_pos")])),
    ]
    return bytes(code), sites, L


def lorom_offset(addr):
    return ((addr >> 16) & 0x7F) * 0x8000 + (addr & 0x7FFF)


def check(rom) -> str:
    """Why the patch cannot be applied to this image, or ''."""
    code, sites, _ = build()
    for addr, expect, _new in sites:
        off = lorom_offset(addr)
        if bytes(rom[off:off + len(expect)]) != expect:
            return f"unexpected bytes at ${addr:06X}"
    off = lorom_offset(FREE_START)
    if any(b not in (0x00, 0xFF) for b in rom[off:off + len(code)]):
        return f"no free ROM at ${FREE_START:06X}"
    return ""


def apply(rom: bytearray) -> None:
    why = check(rom)
    if why:
        raise SystemExit(f"options menu patch: {why}")
    code, sites, _ = build()
    for addr, _expect, new in sites:
        off = lorom_offset(addr)
        rom[off:off + len(new)] = new
    off = lorom_offset(FREE_START)
    rom[off:off + len(code)] = code


def c_bytes(data):
    return ", ".join(f"0x{b:02x}" for b in data)


def write_inc(path):
    code, sites, labels = build()
    lines = [
        "/* Generated by tools/options_menu.py; do not edit. The OPTIONS screen patch:",
        " * signatures checked before patching, the bytes written there, and the new",
        " * code and text (original, not from the ROM) placed in free ROM. */",
        f"#define RNRR_OPT_CODE_ADDR 0x{FREE_START:06x}u",
        f"#define RNRR_OPT_MAILBOX 0x{MAILBOX & 0x1FFFF:05x}u   /* offset in WRAM */",
        f"static const uint8_t kRnrrOptCode[{len(code)}] = {{",
    ]
    for i in range(0, len(code), 16):
        lines.append("    " + c_bytes(code[i:i + 16]) + ",")
    lines.append("};")
    for n, (addr, expect, new) in enumerate(sites):
        lines.append(f"static const uint8_t kRnrrOptExpect{n}[] = {{{c_bytes(expect)}}};")
        if new:
            lines.append(f"static const uint8_t kRnrrOptNew{n}[] = {{{c_bytes(new)}}};")
    lines.append("static const RnrrOptSite kRnrrOptSites[] = {")
    for n, (addr, expect, new) in enumerate(sites):
        nw = f"kRnrrOptNew{n}" if new else "NULL"
        lines.append(f"    {{0x{addr:06x}u, kRnrrOptExpect{n}, {len(expect)}, {nw}, {len(new)}}},")
    lines.append("};")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {path}: {len(code)} bytes at ${FREE_START:06X}-${FREE_START + len(code) - 1:06X}, "
          f"{len(sites)} sites")


def disassemble(rom):
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(here, "..", "snesrecomp", "recompiler"))
    import snes65816 as s
    code, sites, L = build()
    names = {v: k for k, v in L.items()}
    modes = {"setup_print": (0, 0), "print_ptr": (0, 0), "beep_keep_y": (1, 1), "dispatch": (1, 1),
             "music_toggle": (1, 1), "exit_hook": (1, 1), "cursor_pos": (1, 1), "print_values": (0, 0), "pv_item": (0, 0)}
    tables = {"counts": len(VALUES), "cursor_y": len(ROWS)}
    pc, m, x = FREE_START, 0, 0
    while pc < L["labels"]:
        name = names.get(pc)
        off = lorom_offset(pc)
        if name in tables:
            print(f"  .{name}: {rom[off:off + tables[name]].hex()}")
            pc += tables[name]
            continue
        if name in modes:
            m, x = modes[name]
            print(f"{name}:")
        elif name:
            print(f"  .{name}:")
        i = s.decode_insn(rom, off, pc & 0xFFFF, 1, m, x)
        print(f"  {pc:06X}  {rom[off:off + i.length].hex():12s} {i.mnem} {i.operand:#x}")
        if i.mnem == "REP":
            m = 0 if i.operand & 0x20 else m
            x = 0 if i.operand & 0x10 else x
        if i.mnem == "SEP":
            m = 1 if i.operand & 0x20 else m
            x = 1 if i.operand & 0x10 else x
        pc += i.length
    for addr, _expect, new in sites:
        if new:
            print(f"site ${addr:06X}: {new.hex()}")


def main() -> int:
    if len(sys.argv) == 3 and sys.argv[1] == "--write-inc":
        write_inc(sys.argv[2])
        return 0
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        rom = bytearray(open(sys.argv[2], "rb").read())
        if len(rom) % 1024 == 512:
            rom = rom[512:]
        apply(rom)
        disassemble(rom)
        code, sites, _ = build()
        print(f"ok: {len(code)} bytes of code and text, {len(sites)} sites; labels:")
        print("  " + labels_text().replace("\r", "|"))
        return 0
    raise SystemExit(__doc__)


if __name__ == "__main__":
    raise SystemExit(main())
