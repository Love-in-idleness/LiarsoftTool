# GSC Opcode / TSC Command Reference

This document describes the GSC↔TSC implementation in LiarsoftTool 2.1.2. The
source of truth is `src/gsc_decompiler.cpp`; the "sample count" column comes
from local compatibility data and does not mean that every RScript generation
contains only these opcodes.

## Status Legend

- **Named**: both the operand boundaries and the command name are confirmed;
  the TSC body normally emits `*command`.
- **Special syntax**: the decompiler emits a jump, VM, data-block, or text
  instruction that recompiles directly.
- **Unknown semantics**: the signature is known, so the instruction round-trips
  structurally, but no reliable command name exists; the body emits
  `*opcode <decimal> ...`.
- **Fully unknown**: the operand length is unknown as well. None appear in the
  current ten sample sets; when one is met, the whole file is explicitly marked
  as not decompilable and uses the legacy `gsc-raw` fallback.

In operand signatures, `E` is an RScript expression value, `D` a 32-bit
unsigned value, `H` a 16-bit unsigned value, and `S` a 16-bit signed value. A
few named commands have different signatures across RScript generations; the
`;@gsc-schema` metadata of the TSC is authoritative. Code, string indices, and
data blocks are all recalculated from the source text — no copy of the original
GSC code section or string table is stored.

Some `D`/`E` operands are not numbers at all but **string references** (indices
into the string table). They must be emitted as quoted strings when
decompiling, and re-registered into the string table when recompiling. Missing
one makes that string vanish from the rebuilt table while the code keeps the
stale index, so the game reads an out-of-range or wrong string at runtime:

| Opcode | Operand positions holding a string reference |
|---|---|
| 14 `*select` | 2nd (choice text) and 8th–12th |
| 15 `*gosub` | 2nd (called subroutine name; always `"select"` in the main-story samples, i.e. the shared choice-branch handler) |
| 32 `*font` | 6th (text) |
| 81 `*TXT` | 5th (speaker name), 6th (body) |
| 82 `*TXA` | 5th (body) |
| 121 `*folder`, 150 `*strset`, 151 `*stradd` | 2nd |

The names of `pow`, `gmenuon`, `gmenuset`, `gmenuget`, and `map` come from the
2012 `RsComp.dll` shipped with UBAI (SHA-256
`0b1c3cdfc1587f576c30c09b3faa329e88e18fdaddfc0655d1ad398c1654a6a9`): its
compiler dispatch code matches command names with `lstrcmpiA` and writes the
corresponding opcode directly.

## Ordinary Opcodes

| Decimal | Hex | TSC command / representation | Status | Sample count |
|---:|---:|---|---|---:|
| 3 | `0x0003` | `*jz label` | Special syntax | 79944 |
| 4 | `0x0004` | `*jnz label` | Special syntax | 572 |
| 5 | `0x0005` | `*goto label` | Special syntax | 68967 |
| 8 | `0x0008` | `*end` | Named | 237 |
| 9 | `0x0009` | `*rnd` | Named | 980 |
| 10 | `0x000A` | `*hit` | Named | 1061 |
| 11 | `0x000B` | `*hitc` | Named | 7 |
| 12 | `0x000C` | `*jump` | Named | 887 |
| 13 | `0x000D` | `*wait` | Named | 9295 |
| 14 | `0x000E` | `*select` | Named | 372 |
| 15 | `0x000F` | `*gosub` | Named | 10962 |
| 16 | `0x0010` | `*return` | Named | 2765 |
| 17 | `0x0011` | `*save` | Named | 0 |
| 18 | `0x0012` | `*data E D`; contents declared by `*datablock` | Special syntax | 1460 |
| 19 | `0x0013` | `*subscript` | Named | 0 |
| 20 | `0x0014` | `*gload` | Named | 6376 |
| 21 | `0x0015` | `*gcls` | Named | 1184 |
| 22 | `0x0016` | `*gmove` | Named | 5 |
| 23 | `0x0017` | `*quake` | Named | 950 |
| 24 | `0x0018` | `*flash` | Named | 418 |
| 25 | `0x0019` | `*call` | Named | 0 |
| 26 | `0x001A` | `*queue` | Named | 40099 |
| 27 | `0x001B` | `*action` | Named | 2575 |
| 28 | `0x001C` | `*update` | Named | 38863 |
| 29 | `0x001D` | `*zupdate` | Named | 194 |
| 30 | `0x001E` | `*load` | Named | 25706 |
| 32 | `0x0020` | `*font` | Named | 4741 |
| 33 | `0x0021` | `*move` | Named | 65 |
| 34 | `0x0022` | `*movi` | Named | 2524 |
| 35 | `0x0023` | `*phase` | Named | 2 |
| 36 | `0x0024` | `*cls` | Named | 47057 |
| 37 | `0x0025` | `*enabl` | Named | 1570 |
| 38 | `0x0026` | `*locmode` | Named | 15814 |
| 39 | `0x0027` | `*draw` | Named | 1387 |
| 40 | `0x0028` | `*depth` | Named | 17363 |
| 41 | `0x0029` | `*group` | Named | 330 |
| 42 | `0x002A` | `*tbox` | Named | 8834 |
| 43 | `0x002B` | `*effect` | Named | 1601 |
| 44 | `0x002C` | `*effectdep` | Named | 478 |
| 45 | `0x002D` | `*tone` | Named | 4382 |
| 46 | `0x002E` | `*tonedep` | Named | 2191 |
| 47 | `0x002F` | `*locgrid` | Named | 353 |
| 48 | `0x0030` | `*face` | Named | 904 |
| 49 | `0x0031` | `*mode` | Named | 342 |
| 50 | `0x0032` | `*backup` | Named | 41 |
| 51 | `0x0033` | `*resume` | Named | 0 |
| 52 | `0x0034` | `*stop` | Named | 265 |
| 53 | `0x0035` | `*autobackup` | Named | 0 |
| 55 | `0x0037` | `*makesave` | Named | 29 |
| 56 | `0x0038` | `*sysmode` | Named | 584 |
| 57 | `0x0039` | `*logflash` | Named | 49 |
| 58 | `0x003A` | `*endscene` | Named | 12 |
| 59 | `0x003B` | `*swap` | Named | 0 |
| 60 | `0x003C` | `*bgm_on` | Named | 3588 |
| 61 | `0x003D` | `*bgm_off` | Named | 3691 |
| 62 | `0x003E` | `*se` | Named | 7311 |
| 63 | `0x003F` | `*se_on` | Named | 7339 |
| 64 | `0x0040` | `*se_off` | Named | 3622 |
| 65 | `0x0041` | `*movie` | Named | 25 |
| 66 | `0x0042` | `*voice` | Named | 3271 |
| 67 | `0x0043` | `*voice_off` | Named | 1552 |
| 68 | `0x0044` | `*se_wait` | Named | 70 |
| 69 | `0x0045` | `*voice_wait` | Named | 547 |
| 70 | `0x0046` | `*setclk` | Named | 993 |
| 71 | `0x0047` | `*setclksys` | Named | 34 |
| 72 | `0x0048` | `*resetclk` | Named | 90 |
| 73 | `0x0049` | `*click` | Named | 242 |
| 74 | `0x004A` | `*autoreset` | Named | 340 |
| 75 | `0x004B` | `*setlink` | Named | 1289 |
| 77 | `0x004D` | `*setclksub` | Named | 1 |
| 80 | `0x0050` | `*TCL` | Named | 0 |
| 81 | `0x0051` | `*TXT E D E E "name" "body" E` | Special syntax (TXT) | 148154 |
| 82 | `0x0052` | `*TXA E E E E "appended body" E` | Special syntax (TXA) | 318 |
| 83 | `0x0053` | `*txcls` | Named | 3476 |
| 84 | `0x0054` | — | **Unknown semantics**, signature `E D` | 0 |
| 90 | `0x005A` | `*tboxloc` | Named | 561 |
| 91 | `0x005B` | `*texloc` | Named | 340 |
| 92 | `0x005C` | `*tboxback` | Named | 1486 |
| 93 | `0x005D` | `*cmploc` | Named | 529 |
| 94 | `0x005E` | `*tboxcmp` | Named | 577 |
| 95 | `0x005F` | `*texcolor` | Named | 780 |
| 96 | `0x0060` | `*texsize` | Named | 561 |
| 97 | `0x0061` | `*texfont` | Named | 561 |
| 98 | `0x0062` | `*texmode` | Named | 880 |
| 99 | `0x0063` | `*texindent` | Named | 16847 |
| 100 | `0x0064` | `*waitloc` | Named | 338 |
| 101 | `0x0065` | `*faceloc` | Named | 32 |
| 102 | `0x0066` | `*tclsmode` | Named | 1 |
| 103 | `0x0067` | `*waitlod` | Named | 541 |
| 104 | `0x0068` | `*waitcol` | Named | 541 |
| 105 | `0x0069` | `*facedep` | Named | 32 |
| 106 | `0x006A` | `*namloc` | Named | 320 |
| 107 | `0x006B` | `*texpich` | Named | 320 |
| 108 | `0x006C` | `*texruby` | Named | 320 |
| 110 | `0x006E` | `*getloc` | Named | 0 |
| 111 | `0x006F` | `*muldev` | Named | 1 |
| 112 | `0x0070` | `*root` | Named | 0 |
| 113 | `0x0071` | `*pow` | Named | 0 |
| 114 | `0x0072` | `*getflag` | Named | 186 |
| 115 | `0x0073` | `*menuon` | Named | 0 |
| 116 | `0x0074` | `*menuset` | Named | 0 |
| 117 | `0x0075` | `*menuget` | Named | 0 |
| 120 | `0x0078` | `*fontsize` | Named | 412 |
| 121 | `0x0079` | `*folder` | Named | 19649 |
| 130 | `0x0082` | `*numload` | Named | 9 |
| 131 | `0x0083` | `*numreng` | Named | 9 |
| 132 | `0x0084` | `*numenable` | Named | 3359 |
| 134 | `0x0086` | `*numloc` | Named | 9 |
| 135 | `0x0087` | `*numset` | Named | 9 |
| 136 | `0x0088` | `*num` | Named | 40 |
| 140 | `0x008C` | `*gmenuon` | Named | 0 |
| 141 | `0x008D` | `*gmenuset` | Named | 0 |
| 142 | `0x008E` | `*gmenuget` | Named | 0 |
| 150 | `0x0096` | `*strset` | Named | 2 |
| 151 | `0x0097` | `*stradd` | Named | 0 |
| 152 | `0x0098` | `*numstr` | Named | 0 |
| 153 | `0x0099` | `*strnum` | Named | 0 |
| 154 | `0x009A` | `*strcpy` | Named | 0 |
| 155 | `0x009B` | `*strcat` | Named | 0 |
| 156 | `0x009C` | `*strinput` | Named | 0 |
| 157 | `0x009D` | `*strinprop` | Named | 0 |
| 158 | `0x009E` | `*strsave` | Named | 0 |
| 159 | `0x009F` | `*strload` | Named | 0 |
| 200 | `0x00C8` | `*insub` | Named | 12150 |
| 201 | `0x00C9` | `*metamor` | Named | 0 |
| 202 | `0x00CA` | `*flagset` | Named | 18 |
| 210 | `0x00D2` | `*dynsel` | Named | 1 |
| 211 | `0x00D3` | `*dynans` | Named | 3 |
| 212 | `0x00D4` | `*dynnext` | Named | 0 |
| 213 | `0x00D5` | `*dyndo` | Named | 1 |
| 220 | `0x00DC` | `*map` | Named | 0 |
| 221 | `0x00DD` | `*mapload` | Named | 0 |
| 222 | `0x00DE` | `*mapcls` | Named | 0 |
| 223 | `0x00DF` | `*mapobj` | Named | 0 |
| 225 | `0x00E1` | `*locmap` | Named | 2 |
| 230 | `0x00E6` | `*bganim` | Named | 0 |
| 231 | `0x00E7` | `*await` | Named | 0 |
| 255 | `0x00FF` | `*excmd` | Named | 4988 |

## Opcodes with Unknown Semantics

| Opcode | Known signature | Seen in the ten sample sets | Current handling |
|---:|---|---:|---|
| `0x0054` | `E D` | 0 | Keep the structure and emit the numeric opcode; wait for EXE or official material to confirm the command's meaning |

`0x0054` is "known structure, unknown name" rather than a parse failure. Do not
guess a command name without compiler or runtime evidence.

## VM-Encoded Instruction Families

When the top four bits of an opcode are non-zero, it is not a plain command
number: the operator and the addressing modes are encoded in the opcode itself.
The low bits further carry immediate values, temporaries, or indirect
addressing for the left and right operands. The TSC stores the complete
recompilable form as `*vm 0xHHHH ...`; the table below gives the meaning of
each high-nibble family.

| High family | Operation | Sample count |
|---:|---|---:|
| `0x1***` | assignment `=` | 189277 |
| `0x2***` | logical OR `||` | 2511 |
| `0x3***` | logical AND `&&` | 37021 |
| `0x4***` | equal `==` | 36014 |
| `0x5***` | greater or equal `>=` | 35914 |
| `0x6***` | greater `>` | 345 |
| `0x7***` | less or equal `<=` | 33809 |
| `0x8***` | less `<` | 3183 |
| `0x9***` | not equal `!=` | 10762 |
| `0xA***` | add `+` | 14648 |
| `0xB***` | subtract `-` | 977 |
| `0xC***` | multiply `*` | 2523 |
| `0xD***` | divide `/` | 952 |
| `0xE***` | modulo `%` | 2194 |
| `0xF***` | load/move `mov` | 385805 |

## TSC Metadata Lines

| Line | Meaning |
|---|---|
| `;@gsc-byte-format modern-36` / `legacy-28` | Header length and the Section D declared-size rule |
| `;@gsc-text-encoding <enc>` | Encoding of the string body |
| `;@gsc-schema <name>` | Instruction layout (`pre-codex`/`early`/`rscript18`/`rscript19`/`modern`) |
| `;@gsc-trailer <hex>` | Every trailing byte after Section D (the two debug tables plus the names blob), restored verbatim; defaults to the standard empty 9 bytes |
| `;@gsc-trailer-header <u7> <u8>` | Header words 7 and 8, which size those tables and the names blob; defaults to `4 1` |
| `;@gsc-raw-v1 …` | Whole-file raw fallback used when the instruction layout cannot be recognized |

Twelve scripts of 霞外籠逗留記 carry a 24-byte trailer naming the symbol
`scmode` (`u7=8 u8=8`), and one carries a 177-byte all-zero trailer
(`u7=4 u8=1`). Samples from Evermaiden, Jeanne, Albatross, Khime, CNMI, and
Sevenbridge likewise contain names such as `REP001`, `TOP1`, and `omake`. None
of that data is part of the instruction stream; only the two
`;@gsc-trailer*` lines keep recompilation from swallowing it.

## Current Verification Coverage

- All 2043 GSC files in the compatibility material, plus the 626 GSC files of
  an installed CannonBall, pass a full round-trip test.
- The round-trip test compares re-decompiled instructions, labels, strings, and
  data blocks, ensuring structural and execution-level equivalence.
- Strings are interned by content and re-indexed, so byte-identical string
  numbering is not promised; the trailing debug/names data and its two header
  length words, however, do round-trip byte for byte — all 1454 files with
  36-byte headers in the samples pass that check.
- Every string reference has been checked individually: apart from merging
  duplicates, every string of the original file survives recompilation, and the
  text resolved through each string reference is unchanged (which is how the
  `*gosub` subroutine name was confirmed).
- Sample sources include Evermaiden, Khime, Albatross, Forest, Houkago,
  Sevenbridge, Cannonball, 霞外籠逗留記, CNMI, and Jeanne.
