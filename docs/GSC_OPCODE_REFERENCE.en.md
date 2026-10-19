# TSC Command Reference

[Documentation](README.md) · [中文](GSC_OPCODE_REFERENCE.md)

Use this page to look up generated TSC commands and their opcode numbers.
To translate dialogue, edit quoted text; keep generated labels and metadata.
Run `liarsofttool -h` for export and compilation commands.

## Reading the tables

- **Named**: a command with an identified name and operand layout.
- **Special syntax**: jumps, VM operations and other generated forms.
- **Unknown semantics**: a known operand layout, but an unconfirmed name;
  emitted as `*opcode`. Do not guess a replacement.
- Unrecognized instruction layouts cannot be exported as editable commands.

Operand signatures use `E` for expressions, `D` for unsigned 32-bit values,
`H` for unsigned 16-bit values and `S` for signed 16-bit values.
Some operands are quoted strings. Follow the layout recorded in the TSC,
since signatures vary by engine generation.

The implementation is in `src/gsc_decompiler.cpp`. These tables are a reference,
not a promise that every engine uses every command.

## Ordinary Opcodes

| Decimal | Hex | TSC command / representation | Status |
|---:|---:|---|---|
| 3 | `0x0003` | `*jz label` | Special syntax |
| 4 | `0x0004` | `*jnz label` | Special syntax |
| 5 | `0x0005` | `*goto label` | Special syntax |
| 8 | `0x0008` | `*end` | Named |
| 9 | `0x0009` | `*rnd` | Named |
| 10 | `0x000A` | `*hit` | Named |
| 11 | `0x000B` | `*hitc` | Named |
| 12 | `0x000C` | `*jump` | Named |
| 13 | `0x000D` | `*wait` | Named |
| 14 | `0x000E` | `*select` | Named |
| 15 | `0x000F` | `*gosub` | Named |
| 16 | `0x0010` | `*return` | Named |
| 17 | `0x0011` | `*save` | Named |
| 18 | `0x0012` | `*data E D`; contents declared by `*datablock` | Special syntax |
| 19 | `0x0013` | `*subscript` | Named |
| 20 | `0x0014` | `*gload` | Named |
| 21 | `0x0015` | `*gcls` | Named |
| 22 | `0x0016` | `*gmove` | Named |
| 23 | `0x0017` | `*quake` | Named |
| 24 | `0x0018` | `*flash` | Named |
| 25 | `0x0019` | `*call` | Named |
| 26 | `0x001A` | `*queue` | Named |
| 27 | `0x001B` | `*action` | Named |
| 28 | `0x001C` | `*update` | Named |
| 29 | `0x001D` | `*zupdate` | Named |
| 30 | `0x001E` | `*load` | Named |
| 32 | `0x0020` | `*font` | Named |
| 33 | `0x0021` | `*move` | Named |
| 34 | `0x0022` | `*movi` | Named |
| 35 | `0x0023` | `*phase` | Named |
| 36 | `0x0024` | `*cls` | Named |
| 37 | `0x0025` | `*enabl` | Named |
| 38 | `0x0026` | `*locmode` | Named |
| 39 | `0x0027` | `*draw` | Named |
| 40 | `0x0028` | `*depth` | Named |
| 41 | `0x0029` | `*group` | Named |
| 42 | `0x002A` | `*tbox` | Named |
| 43 | `0x002B` | `*effect` | Named |
| 44 | `0x002C` | `*effectdep` | Named |
| 45 | `0x002D` | `*tone` | Named |
| 46 | `0x002E` | `*tonedep` | Named |
| 47 | `0x002F` | `*locgrid` | Named |
| 48 | `0x0030` | `*face` | Named |
| 49 | `0x0031` | `*mode` | Named |
| 50 | `0x0032` | `*backup` | Named |
| 51 | `0x0033` | `*resume` | Named |
| 52 | `0x0034` | `*stop` | Named |
| 53 | `0x0035` | `*autobackup` | Named |
| 55 | `0x0037` | `*makesave` | Named |
| 56 | `0x0038` | `*sysmode` | Named |
| 57 | `0x0039` | `*logflash` | Named |
| 58 | `0x003A` | `*endscene` | Named |
| 59 | `0x003B` | `*swap` | Named |
| 60 | `0x003C` | `*bgm_on` | Named |
| 61 | `0x003D` | `*bgm_off` | Named |
| 62 | `0x003E` | `*se` | Named |
| 63 | `0x003F` | `*se_on` | Named |
| 64 | `0x0040` | `*se_off` | Named |
| 65 | `0x0041` | `*movie` | Named |
| 66 | `0x0042` | `*voice` | Named |
| 67 | `0x0043` | `*voice_off` | Named |
| 68 | `0x0044` | `*se_wait` | Named |
| 69 | `0x0045` | `*voice_wait` | Named |
| 70 | `0x0046` | `*setclk` | Named |
| 71 | `0x0047` | `*setclksys` | Named |
| 72 | `0x0048` | `*resetclk` | Named |
| 73 | `0x0049` | `*click` | Named |
| 74 | `0x004A` | `*autoreset` | Named |
| 75 | `0x004B` | `*setlink` | Named |
| 77 | `0x004D` | `*setclksub` | Named |
| 80 | `0x0050` | `*TCL` | Named |
| 81 | `0x0051` | `*TXT E D E E "name" "body" E` | Special syntax (TXT) |
| 82 | `0x0052` | `*TXA E E E E "appended body" E` | Special syntax (TXA) |
| 83 | `0x0053` | `*txcls` | Named |
| 84 | `0x0054` | — | **Unknown semantics**, signature `E D` |
| 90 | `0x005A` | `*tboxloc` | Named |
| 91 | `0x005B` | `*texloc` | Named |
| 92 | `0x005C` | `*tboxback` | Named |
| 93 | `0x005D` | `*cmploc` | Named |
| 94 | `0x005E` | `*tboxcmp` | Named |
| 95 | `0x005F` | `*texcolor` | Named |
| 96 | `0x0060` | `*texsize` | Named |
| 97 | `0x0061` | `*texfont` | Named |
| 98 | `0x0062` | `*texmode` | Named |
| 99 | `0x0063` | `*texindent` | Named |
| 100 | `0x0064` | `*waitloc` | Named |
| 101 | `0x0065` | `*faceloc` | Named |
| 102 | `0x0066` | `*tclsmode` | Named |
| 103 | `0x0067` | `*waitlod` | Named |
| 104 | `0x0068` | `*waitcol` | Named |
| 105 | `0x0069` | `*facedep` | Named |
| 106 | `0x006A` | `*namloc` | Named |
| 107 | `0x006B` | `*texpich` | Named |
| 108 | `0x006C` | `*texruby` | Named |
| 110 | `0x006E` | `*getloc` | Named |
| 111 | `0x006F` | `*muldev` | Named |
| 112 | `0x0070` | `*root` | Named |
| 113 | `0x0071` | `*pow` | Named |
| 114 | `0x0072` | `*getflag` | Named |
| 115 | `0x0073` | `*menuon` | Named |
| 116 | `0x0074` | `*menuset` | Named |
| 117 | `0x0075` | `*menuget` | Named |
| 120 | `0x0078` | `*fontsize` | Named |
| 121 | `0x0079` | `*folder` | Named |
| 130 | `0x0082` | `*numload` | Named |
| 131 | `0x0083` | `*numreng` | Named |
| 132 | `0x0084` | `*numenable` | Named |
| 134 | `0x0086` | `*numloc` | Named |
| 135 | `0x0087` | `*numset` | Named |
| 136 | `0x0088` | `*num` | Named |
| 140 | `0x008C` | `*gmenuon` | Named |
| 141 | `0x008D` | `*gmenuset` | Named |
| 142 | `0x008E` | `*gmenuget` | Named |
| 150 | `0x0096` | `*strset` | Named |
| 151 | `0x0097` | `*stradd` | Named |
| 152 | `0x0098` | `*numstr` | Named |
| 153 | `0x0099` | `*strnum` | Named |
| 154 | `0x009A` | `*strcpy` | Named |
| 155 | `0x009B` | `*strcat` | Named |
| 156 | `0x009C` | `*strinput` | Named |
| 157 | `0x009D` | `*strinprop` | Named |
| 158 | `0x009E` | `*strsave` | Named |
| 159 | `0x009F` | `*strload` | Named |
| 200 | `0x00C8` | `*insub` | Named |
| 201 | `0x00C9` | `*metamor` | Named |
| 202 | `0x00CA` | `*flagset` | Named |
| 210 | `0x00D2` | `*dynsel` | Named |
| 211 | `0x00D3` | `*dynans` | Named |
| 212 | `0x00D4` | `*dynnext` | Named |
| 213 | `0x00D5` | `*dyndo` | Named |
| 220 | `0x00DC` | `*map` | Named |
| 221 | `0x00DD` | `*mapload` | Named |
| 222 | `0x00DE` | `*mapcls` | Named |
| 223 | `0x00DF` | `*mapobj` | Named |
| 225 | `0x00E1` | `*locmap` | Named |
| 230 | `0x00E6` | `*bganim` | Named |
| 231 | `0x00E7` | `*await` | Named |
| 255 | `0x00FF` | `*excmd` | Named |

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

| High family | Operation |
|---:|---|
| `0x1***` | assignment `=` |
| `0x2***` | logical OR `||` |
| `0x3***` | logical AND `&&` |
| `0x4***` | equal `==` |
| `0x5***` | greater or equal `>=` |
| `0x6***` | greater `>` |
| `0x7***` | less or equal `<=` |
| `0x8***` | less `<` |
| `0x9***` | not equal `!=` |
| `0xA***` | add `+` |
| `0xB***` | subtract `-` |
| `0xC***` | multiply `*` |
| `0xD***` | divide `/` |
| `0xE***` | modulo `%` |
| `0xF***` | load/move `mov` |

## TSC Metadata Lines

| Line | Meaning |
|---|---|
| `;@gsc-byte-format modern-36` / `legacy-28` | Header length and the Section D declared-size rule |
| `;@gsc-schema <name>` | Instruction layout (`pre-codex`/`early-short-select`/`early`/`rscript18`/`rscript19`/`modern`) |
| `;@gsc-trailer <hex>` | Every trailing byte after Section D (the two entry tables plus the names blob), restored verbatim; defaults to the standard empty 9 bytes |
| `;@gsc-trailer-header <u7> <u8>` | Header words 7 and 8, which size those tables and the names blob; defaults to `4 1` |
| `;@gsc-raw-v1 …` | Whole-file raw fallback used when the instruction layout cannot be recognized |

Save TSC as UTF-8 and choose the GSC encoding with `-e`.
