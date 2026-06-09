#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "ELF_Loader.h"
#include "mmu/Paging_Main.h"

#include "Drivers/Module/DriverManager.h"
#include "MemoryManagement/Memory_Main.h"
#include "Core/sync/Spinlock.h"
#include "Core/vfs/VFS.h"
#include "Debug/printf/printf.h"
#include "Debug/serial/Serial.h"

#include <stddef.h>
#include <stdint.h>

#define ELF_MAGIC_0 0x7F
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'

#define ET_EXEC 2
#define ET_DYN  3

#define EM_X86_64  62
#define EM_AARCH64 183

#define PT_LOAD    1
#define PT_DYNAMIC 2

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

#define SHT_RELA 4

#define SHN_UNDEF 0

/* -----------------------------------------------------------------------
 * x86-64 relocation types  (System V ABI, psABI supplement)
 * --------------------------------------------------------------------- */
#define R_X86_64_NONE      0   /* 処理不要                                */
#define R_X86_64_64        1   /* S + A          → 64-bit絶対           */
#define R_X86_64_PC32      2   /* S + A - P      → 32-bit PC相対        */
#define R_X86_64_COPY      5   /* 動的コピー（カーネルモジュール禁止）    */
#define R_X86_64_GLOB_DAT  6   /* S + A          → 64-bit GOTエントリ   */
#define R_X86_64_JUMP_SLOT 7   /* S              → PLTエントリ          */
#define R_X86_64_RELATIVE  8   /* B + A          → 64-bit相対           */
#define R_X86_64_PC64      24  /* S + A - P      → 64-bit PC相対        */
#define R_X86_64_PLT32     4   /* L + A - P → PC相対32(PLT、モジュールではPC32として処理) */
#define R_X86_64_32        10  /* S + A          → 32-bit符号なし絶対   */
#define R_X86_64_32S       11  /* S + A          → 32-bit符号付き絶対   */

/* -----------------------------------------------------------------------
 * AArch64 relocation types  (ELF for the Arm 64-bit Architecture)
 * --------------------------------------------------------------------- */
#define R_AARCH64_NONE           0    /* 処理不要                                        */
#define R_AARCH64_ABS64          257  /* S + A          → 64-bit絶対                   */
#define R_AARCH64_ABS32          258  /* S + A          → 32-bit絶対（範囲チェック付き）*/
#define R_AARCH64_ABS16          259  /* S + A          → 16-bit絶対（範囲チェック付き）*/
#define R_AARCH64_PREL64         260  /* S + A - P      → 64-bit PC相対                */
#define R_AARCH64_PREL32         261  /* S + A - P      → 32-bit PC相対                */
#define R_AARCH64_PREL16         262  /* S + A - P      → 16-bit PC相対（範囲チェック）*/

/* MOVZ/MOVK 即値パッチ (Gn = Nビット目16bit窓) */
#define R_AARCH64_MOVW_UABS_G0        263  /* S + A の [15:0]  → MOVZ/MOVK imm16     */
#define R_AARCH64_MOVW_UABS_G0_NC     264  /* S + A の [15:0]  → (範囲チェックなし)  */
#define R_AARCH64_MOVW_UABS_G1        265  /* S + A の [31:16] → MOVZ/MOVK imm16     */
#define R_AARCH64_MOVW_UABS_G1_NC     266  /* S + A の [31:16] → (範囲チェックなし)  */
#define R_AARCH64_MOVW_UABS_G2        267  /* S + A の [47:32] → MOVZ/MOVK imm16     */
#define R_AARCH64_MOVW_UABS_G2_NC     268  /* S + A の [47:32] → (範囲チェックなし)  */
#define R_AARCH64_MOVW_UABS_G3        269  /* S + A の [63:48] → MOVZ/MOVK imm16     */

/* ADRP + ADD / ADRP + LDR 系 */
#define R_AARCH64_ADR_PREL_PG_HI21    275  /* (S + A の Page) - (P の Page) → ADRP imm21 */
#define R_AARCH64_ADR_PREL_PG_HI21_NC 276  /* 同上（範囲チェックなし）                   */
#define R_AARCH64_ADD_ABS_LO12_NC     277  /* S + A の [11:0]  → ADD  imm12             */
#define R_AARCH64_LDST8_ABS_LO12_NC   278  /* S + A の [11:0]  → LDR/STR 8bit  imm12   */

/* B / BL 系 */
#define R_AARCH64_JUMP26              282  /* S + A - P → B    imm26 (±128 MiB)        */
#define R_AARCH64_CALL26              283  /* S + A - P → BL   imm26 (±128 MiB)        */

/* LDR/STR 系 (他の幅) */
#define R_AARCH64_LDST16_ABS_LO12_NC  284  /* S + A の [11:1]  → LDR/STR 16bit imm12  */
#define R_AARCH64_LDST32_ABS_LO12_NC  285  /* S + A の [11:2]  → LDR/STR 32bit imm12  */
#define R_AARCH64_LDST64_ABS_LO12_NC  286  /* S + A の [11:3]  → LDR/STR 64bit imm12  */
#define R_AARCH64_LDST128_ABS_LO12_NC 299  /* S + A の [11:4]  → LDR/STR 128bit imm12 */

/* 動的リンク用 (カーネルモジュールでは禁止) */
#define R_AARCH64_COPY       1024
#define R_AARCH64_GLOB_DAT   1025
#define R_AARCH64_JUMP_SLOT  1026
#define R_AARCH64_RELATIVE   1027  /* delta(S) + A → 64-bit (PIE用)                    */

#define ELF_PAGE_SIZE 4096ULL

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

static inline uint32_t elf64_r_type(uint64_t info)
{
    return (uint32_t)(info & 0xFFFFFFFFu);
}

static inline uint32_t elf64_r_sym(uint64_t info)
{
    return (uint32_t)(info >> 32);
}

static uint64_t align_down_u64(uint64_t value, uint64_t align)
{
    if (align == 0) {
        return value;
    }
    return value & ~(align - 1u);
}

static uint64_t align_up_u64(uint64_t value, uint64_t align)
{
    if (align == 0) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static bool range_in_file(uint64_t file_size, uint64_t offset, uint64_t size)
{
    if (offset > file_size) {
        return false;
    }
    return size <= (file_size - offset);
}

static bool in_vaddr_range(uint64_t start,
                           uint64_t size,
                           uint64_t min_vaddr,
                           uint64_t max_vaddr)
{
    if (size == 0) {
        return false;
    }
    if (start < min_vaddr) {
        return false;
    }
    if (start >= max_vaddr) {
        return false;
    }
    if (size > (max_vaddr - start)) {
        return false;
    }
    return true;
}

static bool runtime_range_ok(uint64_t image_start,
                             uint64_t image_span,
                             uint64_t addr,
                             uint64_t size)
{
    if (size == 0 || addr < image_start) {
        return false;
    }
    uint64_t offset = addr - image_start;
    if (offset > image_span) {
        return false;
    }
    return size <= (image_span - offset);
}

static bool elf_validate_ehdr_arch(const Elf64_Ehdr *ehdr,
                                   uint64_t file_size,
                                   bool allow_exec)
{
    if (ehdr->e_ident[0] != ELF_MAGIC_0 ||
        ehdr->e_ident[1] != ELF_MAGIC_1 ||
        ehdr->e_ident[2] != ELF_MAGIC_2 ||
        ehdr->e_ident[3] != ELF_MAGIC_3) {
        return false;
    }

#if defined(PLATFORM_ARM64)
    if (ehdr->e_machine != EM_AARCH64) {
        return false;
    }
#elif defined(PLATFORM_X86_64)
    if (ehdr->e_machine != EM_X86_64) {
        return false;
    }
#else
#error "PLATFORM_ARM64 or PLATFORM_X86_64 must be defined"
#endif

    if (ehdr->e_type == ET_DYN) {
    } else if (allow_exec && ehdr->e_type == ET_EXEC) {
    } else {
        return false;
    }

    if (ehdr->e_phentsize != sizeof(Elf64_Phdr) || ehdr->e_phnum == 0) {
        return false;
    }

    uint64_t phdr_table_bytes = (uint64_t)ehdr->e_phnum *
                                (uint64_t)ehdr->e_phentsize;
    if (!range_in_file(file_size, ehdr->e_phoff, phdr_table_bytes)) {
        return false;
    }

    return true;  /* 元コードの typo "return true;zz" を修正 */
}

static void safe_memcpy(void *dest, const void *src, size_t n) {
    volatile uint8_t *d = (volatile uint8_t *)dest;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

/* -----------------------------------------------------------------------
 * sym_value_for_reloc
 *
 * リロケーションエントリのシンボルインデックスを解決し、
 * ランタイムアドレス（load_bias + st_value）を *sym_addr_out に返す。
 *
 * sym_index == 0（シンボルなしのリロケーション）の場合は
 * must_have_symbol = false で呼ぶこと。
 * SHN_UNDEF シンボルは *sym_addr_out = 0 として true を返す
 * （後続でゼロ書き込みとする動的リンク慣習に従う）。
 * --------------------------------------------------------------------- */
static bool sym_value_for_reloc(const Elf64_Sym *symbols,
                                uint32_t         symbol_count,
                                uint32_t         sym_index,
                                uint64_t         load_bias,
                                bool             must_have_symbol,
                                uint64_t        *sym_addr_out)
{
    if (sym_index == 0) {
        if (must_have_symbol) {
            return false;
        }
        *sym_addr_out = 0;
        return true;
    }
    if (symbols == NULL || sym_index >= symbol_count) {
        return false;
    }
    const Elf64_Sym *sym = &symbols[sym_index];
    *sym_addr_out = (sym->st_shndx == SHN_UNDEF)
                    ? 0ULL
                    : (load_bias + sym->st_value);
    return true;
}

/* -----------------------------------------------------------------------
 * AArch64 命令パッチ用ヘルパー
 *
 * AArch64 の多くのリロケーションは、特定の命令フィールドのビット列を
 * 部分的に書き換える。以下は各命令フォーマットへのビット挿入関数。
 * --------------------------------------------------------------------- */

/*
 * aarch64_patch_movzk
 * MOVZ / MOVK 命令の imm16 フィールド [20:5] を value の下位16ビットで更新する。
 * 命令レイアウト:
 *   [31]    sf  (64bit=1)
 *   [30:29] opc (MOVZ=10, MOVK=11)
 *   [28:23] 100101
 *   [22:21] hw  (シフト量 hw*16)
 *   [20:5]  imm16  ← ここを書き換える
 *   [4:0]   Rd
 */
static bool aarch64_patch_movzk(void *insn_ptr, uint16_t value)
{
    uint32_t insn;
    safe_memcpy(&insn, insn_ptr, sizeof(insn));
    insn = (insn & ~(0xFFFFu << 5)) | ((uint32_t)value << 5);
    safe_memcpy(insn_ptr, &insn, sizeof(insn));
    return true;
}

/*
 * aarch64_patch_adrp
 * ADRP 命令の imm21 フィールドを書き換える（ページオフセット付き相対アドレス）。
 * imm21 = imm_hi[23:5] || imm_lo[30:29]  （ビット連結）
 * 命令レイアウト:
 *   [31]    1
 *   [30:29] immlo (下位 2 bit)
 *   [28:24] 10000
 *   [23:5]  immhi (上位 19 bit)
 *   [4:0]   Rd
 * page_delta は符号付き21ビット値（4KiB単位ページ差分）。
 */
static bool aarch64_patch_adrp(void *insn_ptr, int64_t page_delta)
{
    /* ±4GiB (= ±2^20 pages × 4KiB) が到達可能範囲 */
    if (page_delta < -(1LL << 20) || page_delta >= (1LL << 20)) {
        return false;
    }
    uint32_t imm21 = (uint32_t)(page_delta & 0x1FFFFF);
    uint32_t immlo = imm21 & 0x3u;
    uint32_t immhi = (imm21 >> 2) & 0x7FFFFu;

    uint32_t insn;
    safe_memcpy(&insn, insn_ptr, sizeof(insn));
    insn = (insn & ~((0x7FFFFu << 5) | (0x3u << 29)))
         | (immhi << 5)
         | (immlo << 29);
    safe_memcpy(insn_ptr, &insn, sizeof(insn));
    return true;
}

/*
 * aarch64_patch_add_imm12
 * ADD (immediate) 命令の imm12 フィールド [21:10] を書き換える。
 * value は 12 ビット符号なし整数でなければならない（[11:0] のみ使用）。
 */
static bool aarch64_patch_add_imm12(void *insn_ptr, uint64_t value)
{
    uint32_t imm12 = (uint32_t)(value & 0xFFFu);
    uint32_t insn;
    safe_memcpy(&insn, insn_ptr, sizeof(insn));
    insn = (insn & ~(0xFFFu << 10)) | (imm12 << 10);
    safe_memcpy(insn_ptr, &insn, sizeof(insn));
    return true;
}

/*
 * aarch64_patch_ldst_imm12
 * LDR / STR (immediate, unsigned offset) 命令の imm12 フィールド [21:10] を書き換える。
 * アクセス幅に応じてビットシフトが異なるため、shift_bits で指定する。
 *   8bit  アクセス → shift_bits = 0
 *   16bit アクセス → shift_bits = 1
 *   32bit アクセス → shift_bits = 2
 *   64bit アクセス → shift_bits = 3
 *   128bit アクセス → shift_bits = 4
 * value はバイトアドレスの下位12ビット。アクセス幅で割り切れる必要がある。
 */
static bool aarch64_patch_ldst_imm12(void *insn_ptr,
                                     uint64_t value,
                                     unsigned shift_bits)
{
    uint64_t byte_off = value & 0xFFFu;
    /* アライメント確認 */
    if (shift_bits > 0 && (byte_off & ((1u << shift_bits) - 1u)) != 0) {
        return false;
    }
    uint32_t imm12 = (uint32_t)(byte_off >> shift_bits);
    uint32_t insn;
    safe_memcpy(&insn, insn_ptr, sizeof(insn));
    insn = (insn & ~(0xFFFu << 10)) | (imm12 << 10);
    safe_memcpy(insn_ptr, &insn, sizeof(insn));
    return true;
}

/*
 * aarch64_patch_branch26
 * B / BL 命令の imm26 フィールド [25:0] を書き換える。
 * オフセットは命令アドレスからのバイト差を 4 で割った値（±128 MiB）。
 */
static bool aarch64_patch_branch26(void *insn_ptr, int64_t byte_delta)
{
    /* 4バイトアライメント確認 */
    if (byte_delta & 3) {
        return false;
    }
    int64_t instr_delta = byte_delta >> 2;
    /* ±2^25 の範囲チェック */
    if (instr_delta < -(1LL << 25) || instr_delta >= (1LL << 25)) {
        return false;
    }
    uint32_t imm26 = (uint32_t)(instr_delta & 0x3FFFFFFu);
    uint32_t insn;
    safe_memcpy(&insn, insn_ptr, sizeof(insn));
    insn = (insn & ~0x3FFFFFFu) | imm26;
    safe_memcpy(insn_ptr, &insn, sizeof(insn));
    return true;
}

/* -----------------------------------------------------------------------
 * apply_relocations
 *
 * 単一の RELA セクションを処理する。
 * x86-64 / AArch64 双方のリロケーションタイプを網羅する。
 * --------------------------------------------------------------------- */
static bool apply_relocations(const uint8_t     *image,
                               uint64_t           file_size,
                               const Elf64_Shdr  *rel_sec,
                               const Elf64_Shdr  *shdrs,
                               uint16_t           shnum,
                               uint64_t           runtime_start,
                               uint64_t           image_span,
                               uint64_t           load_bias,
                               uint32_t          *reloc_budget)
{
    if (rel_sec->sh_entsize != sizeof(Elf64_Rela) ||
        (rel_sec->sh_size % rel_sec->sh_entsize) != 0) {
        return false;
    }

    if (!range_in_file(file_size, rel_sec->sh_offset, rel_sec->sh_size)) {
        return false;
    }

    uint64_t rel_count64 = rel_sec->sh_size / rel_sec->sh_entsize;
    if (rel_count64 > UINT32_MAX) {
        return false;
    }
    uint32_t rel_count = (uint32_t)rel_count64;

    if (rel_count > *reloc_budget) {
        return false;
    }
    *reloc_budget -= rel_count;

    const Elf64_Rela *rels = (const Elf64_Rela *)(image + rel_sec->sh_offset);

    const Elf64_Sym *symbols      = NULL;
    uint32_t         symbol_count = 0;

    if (rel_sec->sh_link < shnum) {
        const Elf64_Shdr *sym_sec = &shdrs[rel_sec->sh_link];
        if (sym_sec->sh_entsize == sizeof(Elf64_Sym) &&
            range_in_file(file_size, sym_sec->sh_offset, sym_sec->sh_size)) {
            symbols      = (const Elf64_Sym *)(image + sym_sec->sh_offset);
            symbol_count = (uint32_t)(sym_sec->sh_size / sym_sec->sh_entsize);
        }
    }

    for (uint32_t r = 0; r < rel_count; ++r) {
        const Elf64_Rela *rela = &rels[r];
        uint32_t type      = elf64_r_type(rela->r_info);
        uint32_t sym_index = elf64_r_sym(rela->r_info);

#if defined(PLATFORM_ARM64)
        /* ----------------------------------------------------------------
         * AArch64 リロケーション処理
         * ---------------------------------------------------------------- */
        switch (type) {

        /* ---- 何もしない ---- */
        case R_AARCH64_NONE:
            continue;

        /* ---- 64ビット絶対値 ---- */
        case R_AARCH64_ABS64:
        case R_AARCH64_GLOB_DAT:  /* GOT エントリ: S + A */
        case R_AARCH64_JUMP_SLOT: /* PLT エントリ: S      */
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint64_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias,
                                         /*must_have_symbol=*/true,
                                         &sym_addr)) {
                    return false;
                }
                /* SHN_UNDEF: ゼロで埋める（外部シンボル未解決） */
                uint64_t res64 = (sym_addr == 0 && type != R_AARCH64_ABS64)
                                 ? 0ULL
                                 : (uint64_t)((int64_t)sym_addr + rela->r_addend);
                safe_memcpy((void *)(uintptr_t)where_addr, &res64, sizeof(uint64_t));
            }
            continue;

        /* ---- 32ビット絶対値（範囲チェック必須）---- */
        case R_AARCH64_ABS32:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint32_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                int64_t result = (int64_t)sym_addr + rela->r_addend;
                /* AArch64 ABI: 符号なし32ビット範囲 [0, 2^32) */
                if (result < 0 || result > (int64_t)UINT32_MAX) {
                    return false;
                }
                uint32_t res32 = (uint32_t)result;
                safe_memcpy((void *)(uintptr_t)where_addr, &res32, sizeof(uint32_t));
            }
            continue;

        /* ---- 16ビット絶対値（範囲チェック必須）---- */
        case R_AARCH64_ABS16:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint16_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                int64_t result = (int64_t)sym_addr + rela->r_addend;
                if (result < 0 || result > (int64_t)UINT16_MAX) {
                    return false;
                }
                uint16_t res16 = (uint16_t)result;
                safe_memcpy((void *)(uintptr_t)where_addr, &res16, sizeof(uint16_t));
            }
            continue;

        /* ---- 64ビット PC相対 ---- */
        case R_AARCH64_PREL64:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint64_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                uint64_t res64 = (uint64_t)((int64_t)sym_addr
                                            + rela->r_addend
                                            - (int64_t)where_addr);
                safe_memcpy((void *)(uintptr_t)where_addr, &res64, sizeof(uint64_t));
            }
            continue;

        /* ---- 32ビット PC相対（範囲チェック付き）---- */
        case R_AARCH64_PREL32:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint32_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                int64_t result = (int64_t)sym_addr
                               + rela->r_addend
                               - (int64_t)where_addr;
                if (result < INT32_MIN || result > INT32_MAX) {
                    return false;
                }
                uint32_t res32 = (uint32_t)(int32_t)result;
                safe_memcpy((void *)(uintptr_t)where_addr, &res32, sizeof(uint32_t));
            }
            continue;

        /* ---- 16ビット PC相対（範囲チェック付き）---- */
        case R_AARCH64_PREL16:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint16_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                int64_t result = (int64_t)sym_addr
                               + rela->r_addend
                               - (int64_t)where_addr;
                if (result < INT16_MIN || result > INT16_MAX) {
                    return false;
                }
                uint16_t res16 = (uint16_t)(int16_t)result;
                safe_memcpy((void *)(uintptr_t)where_addr, &res16, sizeof(uint16_t));
            }
            continue;

        /* ---- PIE 用 load_bias 相対 ---- */
        case R_AARCH64_RELATIVE:
            {
                /* RELATIVE は必ずシンボルインデックス 0 */
                if (sym_index != 0) {
                    return false;
                }
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint64_t))) {
                    return false;
                }
                uint64_t res64 = (uint64_t)((int64_t)load_bias + rela->r_addend);
                safe_memcpy((void *)(uintptr_t)where_addr, &res64, sizeof(uint64_t));
            }
            continue;

        /* ---- MOVZ/MOVK: 符号なし64ビット絶対値の各16ビット窓 ---- */
        case R_AARCH64_MOVW_UABS_G0:
        case R_AARCH64_MOVW_UABS_G0_NC:
        case R_AARCH64_MOVW_UABS_G1:
        case R_AARCH64_MOVW_UABS_G1_NC:
        case R_AARCH64_MOVW_UABS_G2:
        case R_AARCH64_MOVW_UABS_G2_NC:
        case R_AARCH64_MOVW_UABS_G3:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint32_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                uint64_t value = (uint64_t)((int64_t)sym_addr + rela->r_addend);

                /* 窓の選択: G0=[15:0], G1=[31:16], G2=[47:32], G3=[63:48] */
                unsigned shift;
                switch (type) {
                case R_AARCH64_MOVW_UABS_G0:
                case R_AARCH64_MOVW_UABS_G0_NC: shift =  0; break;
                case R_AARCH64_MOVW_UABS_G1:
                case R_AARCH64_MOVW_UABS_G1_NC: shift = 16; break;
                case R_AARCH64_MOVW_UABS_G2:
                case R_AARCH64_MOVW_UABS_G2_NC: shift = 32; break;
                default:                         shift = 48; break;
                }

                /* _NC ではない場合: 上位ビットがゼロであること */
                bool nc = (type == R_AARCH64_MOVW_UABS_G0_NC ||
                           type == R_AARCH64_MOVW_UABS_G1_NC ||
                           type == R_AARCH64_MOVW_UABS_G2_NC);
                if (!nc) {
                    uint64_t upper_mask = ~((uint64_t)0xFFFFu << shift)
                                         & (shift < 48 ? ~0ULL : 0ULL);
                    /* G3 は64ビット全体をカバーするため上位チェック不要 */
                    if (shift < 48 && (value & upper_mask) != 0) {
                        return false;
                    }
                }

                uint16_t imm16 = (uint16_t)((value >> shift) & 0xFFFFu);
                if (!aarch64_patch_movzk((void *)(uintptr_t)where_addr, imm16)) {
                    return false;
                }
            }
            continue;

        /* ---- ADRP: ページ相対 21ビット ---- */
        case R_AARCH64_ADR_PREL_PG_HI21:
        case R_AARCH64_ADR_PREL_PG_HI21_NC:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint32_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                uint64_t target = (uint64_t)((int64_t)sym_addr + rela->r_addend);

                /* ページアドレス（4KiB単位）の差分 */
                int64_t page_delta = (int64_t)(target >> 12)
                                   - (int64_t)(where_addr >> 12);

                /* _NC ではない場合は範囲チェックを強制 */
                if (type == R_AARCH64_ADR_PREL_PG_HI21) {
                    if (page_delta < -(1LL << 20) || page_delta >= (1LL << 20)) {
                        return false;
                    }
                }

                if (!aarch64_patch_adrp((void *)(uintptr_t)where_addr, page_delta)) {
                    return false;
                }
            }
            continue;

        /* ---- ADD imm12: ページ内オフセット [11:0] ---- */
        case R_AARCH64_ADD_ABS_LO12_NC:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint32_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                uint64_t value = (uint64_t)((int64_t)sym_addr + rela->r_addend);
                if (!aarch64_patch_add_imm12((void *)(uintptr_t)where_addr, value)) {
                    return false;
                }
            }
            continue;

        /* ---- LDR/STR imm12: アクセス幅ごとのページ内オフセット ---- */
        case R_AARCH64_LDST8_ABS_LO12_NC:
        case R_AARCH64_LDST16_ABS_LO12_NC:
        case R_AARCH64_LDST32_ABS_LO12_NC:
        case R_AARCH64_LDST64_ABS_LO12_NC:
        case R_AARCH64_LDST128_ABS_LO12_NC:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint32_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                uint64_t value = (uint64_t)((int64_t)sym_addr + rela->r_addend);

                unsigned shift;
                switch (type) {
                case R_AARCH64_LDST8_ABS_LO12_NC:   shift = 0; break;
                case R_AARCH64_LDST16_ABS_LO12_NC:  shift = 1; break;
                case R_AARCH64_LDST32_ABS_LO12_NC:  shift = 2; break;
                case R_AARCH64_LDST64_ABS_LO12_NC:  shift = 3; break;
                default:                             shift = 4; break; /* 128bit */
                }

                if (!aarch64_patch_ldst_imm12((void *)(uintptr_t)where_addr,
                                              value, shift)) {
                    return false;
                }
            }
            continue;

        /* ---- B / BL: ±128 MiB 範囲内ブランチ ---- */
        case R_AARCH64_JUMP26:
        case R_AARCH64_CALL26:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint32_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                int64_t byte_delta = (int64_t)sym_addr
                                   + rela->r_addend
                                   - (int64_t)where_addr;
                if (!aarch64_patch_branch26((void *)(uintptr_t)where_addr,
                                            byte_delta)) {
                    return false;
                }
            }
            continue;

        /* ---- 動的コピーリロケーション: カーネルモジュールでは禁止 ---- */
        case R_AARCH64_COPY:
            return false;

        /* ---- 未知のリロケーションタイプは安全のため拒否 ---- */
        default:
            return false;
        }

#elif defined(PLATFORM_X86_64)
        /* ----------------------------------------------------------------
         * x86-64 リロケーション処理
         * ---------------------------------------------------------------- */
        switch (type) {

        /* ---- 何もしない ---- */
        case R_X86_64_NONE:
            continue;

        /* ---- 64ビット絶対値: S + A ---- */
        case R_X86_64_64:
        case R_X86_64_GLOB_DAT:   /* GOT エントリ */
        case R_X86_64_JUMP_SLOT:  /* PLT エントリ */
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint64_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                if (sym_addr == 0 && type != R_X86_64_64) {
                    /* SHN_UNDEF: ゼロを書く */
                    uint64_t zero = 0;
                    safe_memcpy((void *)(uintptr_t)where_addr, &zero, sizeof(uint64_t));
                    continue;
                }
                uint64_t res64 = (uint64_t)((int64_t)sym_addr + rela->r_addend);
                safe_memcpy((void *)(uintptr_t)where_addr, &res64, sizeof(uint64_t));
            }
            continue;

        /* ---- PIE 用 load_bias 相対: B + A ---- */
        case R_X86_64_RELATIVE:
            {
                if (sym_index != 0) {
                    return false;
                }
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint64_t))) {
                    return false;
                }
                uint64_t res64 = (uint64_t)((int64_t)load_bias + rela->r_addend);
                safe_memcpy((void *)(uintptr_t)where_addr, &res64, sizeof(uint64_t));
            }
            continue;

        /* ---- 32ビット PC相対: S + A - P ---- */
        /* R_X86_64_PLT32 は PLT アドレスを使うが、モジュール内では
         * そのまま PC相対 32ビットとして処理して問題ない              */
        case R_X86_64_PC32:
        case R_X86_64_PLT32:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint32_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                int64_t result = (int64_t)sym_addr
                               + rela->r_addend
                               - (int64_t)where_addr;
                if (result < INT32_MIN || result > INT32_MAX) {
                    return false;
                }
                uint32_t res32 = (uint32_t)(int32_t)result;
                safe_memcpy((void *)(uintptr_t)where_addr, &res32, sizeof(uint32_t));
            }
            continue;

        /* ---- 64ビット PC相対: S + A - P ---- */
        case R_X86_64_PC64:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint64_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                uint64_t res64 = (uint64_t)((int64_t)sym_addr
                                            + rela->r_addend
                                            - (int64_t)where_addr);
                safe_memcpy((void *)(uintptr_t)where_addr, &res64, sizeof(uint64_t));
            }
            continue;

        /* ---- 32ビット符号なし絶対値: S + A (上位32ビットはゼロであること) ---- */
        case R_X86_64_32:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint32_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                int64_t result = (int64_t)sym_addr + rela->r_addend;
                /* 符号なし32ビット範囲: [0, 2^32) */
                if (result < 0 || result > (int64_t)UINT32_MAX) {
                    return false;
                }
                uint32_t res32 = (uint32_t)result;
                safe_memcpy((void *)(uintptr_t)where_addr, &res32, sizeof(uint32_t));
            }
            continue;

        /* ---- 32ビット符号付き絶対値: S + A (64ビットに符号拡張して一致すること) ---- */
        case R_X86_64_32S:
            {
                uint64_t where_addr = load_bias + rela->r_offset;
                if (!runtime_range_ok(runtime_start, image_span,
                                      where_addr, sizeof(uint32_t))) {
                    return false;
                }
                uint64_t sym_addr;
                if (!sym_value_for_reloc(symbols, symbol_count,
                                         sym_index, load_bias, true, &sym_addr)) {
                    return false;
                }
                int64_t result = (int64_t)sym_addr + rela->r_addend;
                /* 符号付き32ビット範囲 */
                if (result < INT32_MIN || result > INT32_MAX) {
                    return false;
                }
                uint32_t res32 = (uint32_t)(int32_t)result;
                safe_memcpy((void *)(uintptr_t)where_addr, &res32, sizeof(uint32_t));
            }
            continue;

        /* ---- 動的コピーリロケーション: カーネルモジュールでは禁止 ---- */
        case R_X86_64_COPY:
            return false;

        /* ---- 未知のリロケーションタイプは安全のため拒否 ---- */
        default:
            return false;
        }

#endif /* PLATFORM_X86_64 */

    } /* for each relocation */

    return true;
}

bool elf_loader_load_from_path(uint64_t target_cr3,
                               const char *path,
                               const elf_load_policy_t *policy,
                               elf_loaded_image_info_t *image_out)
{
    if (target_cr3 == 0 || !path || !policy || !image_out) {
        return false;
    }

    vfs_file_t file;
    if (!vfs_find_file(path, &file)) {
        return false;
    }

    if (file.size == 0 || (uint64_t)file.size > policy->max_file_size) {
        return false;
    }

    if ((uint64_t)file.size < sizeof(Elf64_Ehdr)) {
        return false;
    }

    Elf64_Ehdr ehdr;
    if (!vfs_read_at(&file, 0u, (uint8_t *)&ehdr, sizeof(ehdr))) {
        return false;
    }

    if (!elf_validate_ehdr_arch(&ehdr, (uint64_t)file.size, true)) {
        return false;
    }

    Elf64_Phdr *phdrs = NULL;
    {
        uint64_t phdr_table_bytes = (uint64_t)ehdr.e_phnum *
                                    (uint64_t)ehdr.e_phentsize;
        if (ehdr.e_phoff > 0xFFFFFFFFULL ||
            phdr_table_bytes > 0xFFFFFFFFULL) {
            return false;
        }

        phdrs = (Elf64_Phdr *)malloc((size_t)phdr_table_bytes);
        if (!phdrs) {
            return false;
        }

        if (!vfs_read_at(&file,
                         (uint32_t)ehdr.e_phoff,
                         (uint8_t *)phdrs,
                         (uint32_t)phdr_table_bytes)) {
            free(phdrs);
            return false;
        }
    }

    int    load_segments = 0;
    uint64_t phdr_vaddr  = 0;
    uint64_t old_cr3     = paging_get_active_cr3();
    bool   failed        = false;

    for (uint16_t i = 0; i < ehdr.e_phnum; ++i) {
        Elf64_Phdr *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }

        if (ph->p_memsz < ph->p_filesz) {
            failed = true; break;
        }

        if (!in_vaddr_range(ph->p_vaddr, ph->p_memsz,
                            policy->min_vaddr, policy->max_vaddr)) {
            failed = true; break;
        }

        if (!range_in_file((uint64_t)file.size, ph->p_offset, ph->p_filesz)) {
            failed = true; break;
        }

        if (ph->p_offset > 0xFFFFFFFFULL || ph->p_filesz > 0xFFFFFFFFULL) {
            failed = true; break;
        }

        uint8_t *seg_buf = NULL;
        if (ph->p_filesz > 0) {
            seg_buf = (uint8_t *)malloc((size_t)ph->p_filesz);
            if (seg_buf == NULL) {
                failed = true; break;
            }
            if (!vfs_read_at(&file,
                             (uint32_t)ph->p_offset,
                             seg_buf,
                             (uint32_t)ph->p_filesz)) {
                free(seg_buf);
                failed = true; break;
            }
        }

        if (phdr_vaddr == 0 &&
            ehdr.e_phoff >= ph->p_offset &&
            ehdr.e_phoff < (ph->p_offset + ph->p_filesz)) {
            phdr_vaddr = ph->p_vaddr + (ehdr.e_phoff - ph->p_offset);
        }

        uint64_t seg_flags = PAGE_RW | PAGE_USER;
        if ((ph->p_flags & PF_X) == 0) {
            seg_flags |= PAGE_NX;
        }

        if (paging_map_user_range_alloc(target_cr3,
                                        ph->p_vaddr,
                                        ph->p_memsz,
                                        seg_flags) < 0) {
            if (seg_buf != NULL) {
                free(seg_buf);
            }
            failed = true; break;
        }

        paging_switch_cr3(target_cr3);

        uint8_t *dst = (uint8_t *)(uintptr_t)ph->p_vaddr;
        if (ph->p_filesz > 0) {
            memcpy(dst, seg_buf, (size_t)ph->p_filesz);
        }
        if (ph->p_memsz > ph->p_filesz) {
            memset(dst + ph->p_filesz, 0,
                   (size_t)(ph->p_memsz - ph->p_filesz));
        }

        paging_switch_cr3(old_cr3);

        if (seg_buf != NULL) {
            free(seg_buf);
        }

        ++load_segments;
    }

    if (failed || load_segments == 0) {
        free(phdrs);
        return false;
    }

    if (ehdr.e_entry < policy->min_vaddr ||
        ehdr.e_entry >= policy->max_vaddr) {
        free(phdrs);
        return false;
    }

    image_out->entry       = ehdr.e_entry;
    image_out->phdr_vaddr  = phdr_vaddr;
    image_out->phent       = ehdr.e_phentsize;
    image_out->phnum       = ehdr.e_phnum;
    free(phdrs);
    return true;
}

bool elf_loader_load_from_memory(uint64_t target_cr3,
                                 const void *file_data,
                                 uint64_t file_size,
                                 const elf_load_policy_t *policy,
                                 elf_loaded_image_info_t *image_out)
{
    if (target_cr3 == 0 || !file_data || !policy || !image_out) {
        return false;
    }

    if (file_size == 0 || file_size > policy->max_file_size) {
        return false;
    }

    if (file_size < sizeof(Elf64_Ehdr)) {
        return false;
    }

    const uint8_t  *file_bytes = (const uint8_t *)file_data;
    const Elf64_Ehdr *ehdr     = (const Elf64_Ehdr *)file_bytes;

    if (!elf_validate_ehdr_arch(ehdr, file_size, true)) {
        return false;
    }

    const Elf64_Phdr *phdrs = (const Elf64_Phdr *)(file_bytes + ehdr->e_phoff);
    int    load_segments    = 0;
    uint64_t phdr_vaddr     = 0;
    uint64_t old_cr3        = paging_get_active_cr3();
    bool   failed           = false;

    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        const Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }

        if (ph->p_memsz < ph->p_filesz) {
            failed = true; break;
        }

        if (!in_vaddr_range(ph->p_vaddr, ph->p_memsz,
                            policy->min_vaddr, policy->max_vaddr)) {
            failed = true; break;
        }

        if (!range_in_file(file_size, ph->p_offset, ph->p_filesz)) {
            failed = true; break;
        }

        if (phdr_vaddr == 0 &&
            ehdr->e_phoff >= ph->p_offset &&
            ehdr->e_phoff < (ph->p_offset + ph->p_filesz)) {
            phdr_vaddr = ph->p_vaddr + (ehdr->e_phoff - ph->p_offset);
        }

        uint64_t seg_flags = PAGE_RW | PAGE_USER;
        if ((ph->p_flags & PF_X) == 0) {
            seg_flags |= PAGE_NX;
        }

        if (paging_map_user_range_alloc(target_cr3,
                                        ph->p_vaddr,
                                        ph->p_memsz,
                                        seg_flags) < 0) {
            failed = true; break;
        }

        paging_switch_cr3(target_cr3);
        uint8_t *dst = (uint8_t *)(uintptr_t)ph->p_vaddr;
        if (ph->p_filesz > 0) {
            memcpy(dst, file_bytes + ph->p_offset, (size_t)ph->p_filesz);
        }
        if (ph->p_memsz > ph->p_filesz) {
            memset(dst + ph->p_filesz, 0,
                   (size_t)(ph->p_memsz - ph->p_filesz));
        }
        paging_switch_cr3(old_cr3);
        ++load_segments;
    }

    if (failed || load_segments == 0) {
        return false;
    }

    if (ehdr->e_entry < policy->min_vaddr ||
        ehdr->e_entry >= policy->max_vaddr) {
        return false;
    }

    image_out->entry      = ehdr->e_entry;
    image_out->phdr_vaddr = phdr_vaddr;
    image_out->phent      = ehdr->e_phentsize;
    image_out->phnum      = ehdr->e_phnum;
    return true;
}

bool elf_loader_load_module_from_memory(const void *file_data,
                                        uint64_t file_size,
                                        const elf_module_load_policy_t *policy,
                                        uint64_t *entry_out)
{
    elf_loaded_module_t module;
    if (!elf_loader_load_module_image_from_memory(file_data, file_size,
                                                  policy, &module)) {
        return false;
    }
    *entry_out = module.entry;
    return true;
}

bool elf_loader_load_module_image_from_memory(const void *file_data,
                                              uint64_t file_size,
                                              const elf_module_load_policy_t *policy,
                                              elf_loaded_module_t *module_out)
{
    if (file_data == NULL || policy == NULL || module_out == NULL) {
        return false;
    }
    if (file_size == 0 || file_size > policy->max_file_size) {
        return false;
    }
    if (file_size < sizeof(Elf64_Ehdr)) {
        return false;
    }

    const uint8_t  *image = (const uint8_t *)file_data;
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)image;

    if (!elf_validate_ehdr_arch(ehdr, file_size, false)) {
        return false;
    }

    const Elf64_Phdr *phdrs =
        (const Elf64_Phdr *)(image + ehdr->e_phoff);

    uint64_t min_vaddr        = 0;
    uint64_t max_vaddr        = 0;
    uint32_t load_segment_count = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        const Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }

        if (ph->p_memsz < ph->p_filesz) {
            return false;
        }
        if (!range_in_file(file_size, ph->p_offset, ph->p_filesz)) {
            return false;
        }

        uint64_t seg_end = ph->p_vaddr + ph->p_memsz;
        if (seg_end < ph->p_vaddr) {
            return false;
        }

        if (load_segment_count == 0) {
            min_vaddr = ph->p_vaddr;
            max_vaddr = seg_end;
        } else {
            if (ph->p_vaddr < min_vaddr) min_vaddr = ph->p_vaddr;
            if (seg_end     > max_vaddr) max_vaddr = seg_end;
        }
        ++load_segment_count;
    }

    if (load_segment_count == 0) {
        return false;
    }

    uint64_t aligned_min = align_down_u64(min_vaddr, ELF_PAGE_SIZE);
    uint64_t aligned_max = align_up_u64(max_vaddr,   ELF_PAGE_SIZE);
    if (aligned_max <= aligned_min) {
        return false;
    }

    uint64_t image_span = aligned_max - aligned_min;
    if (image_span == 0 || image_span > policy->max_image_size) {
        return false;
    }

    uint64_t page_count64 = image_span / ELF_PAGE_SIZE;
    if (page_count64 == 0 || page_count64 > UINT32_MAX) {
        return false;
    }
    uint32_t page_count = (uint32_t)page_count64;

    void *load_base_ptr = alloc_contiguous_pages(page_count, 1);
    if (load_base_ptr == NULL) {
        return false;
    }

    uint64_t runtime_start = (uint64_t)(uintptr_t)load_base_ptr;
    uint64_t load_bias     = runtime_start - aligned_min;
    memset(load_base_ptr, 0, (size_t)image_span);

    bool ok     = false;
    bool failed = false;

    for (uint16_t i = 0; i < ehdr->e_phnum && !failed; ++i) {
        const Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }

        uint64_t dst_addr = load_bias + ph->p_vaddr;
        if (!runtime_range_ok(runtime_start, image_span,
                              dst_addr, ph->p_memsz)) {
            failed = true;
            break;
        }

        uint8_t       *dst = (uint8_t *)(uintptr_t)dst_addr;
        const uint8_t *src = image + ph->p_offset;
        if (ph->p_filesz > 0) {
            memcpy(dst, src, (size_t)ph->p_filesz);
        }
        if (ph->p_memsz > ph->p_filesz) {
            memset(dst + ph->p_filesz, 0,
                   (size_t)(ph->p_memsz - ph->p_filesz));
        }
    }

    if (!failed && ehdr->e_shoff != 0 && ehdr->e_shnum != 0) {
        if (ehdr->e_shentsize != sizeof(Elf64_Shdr)) {
            failed = true;
        } else if (!range_in_file(file_size,
                                  ehdr->e_shoff,
                                  (uint64_t)ehdr->e_shnum *
                                  (uint64_t)ehdr->e_shentsize)) {
            failed = true;
        } else {
            const Elf64_Shdr *shdrs =
                (const Elf64_Shdr *)(image + ehdr->e_shoff);

            uint32_t reloc_budget = policy->max_relocation_count;

            for (uint16_t sec = 0; sec < ehdr->e_shnum && !failed; ++sec) {
                const Elf64_Shdr *rel_sec = &shdrs[sec];
                if (rel_sec->sh_type != SHT_RELA || rel_sec->sh_size == 0) {
                    continue;
                }

                if (!apply_relocations(image,
                                       file_size,
                                       rel_sec,
                                       shdrs,
                                       ehdr->e_shnum,
                                       runtime_start,
                                       image_span,
                                       load_bias,
                                       &reloc_budget)) {
                    failed = true;
                }
            }
        }
    }

    if (!failed) {
        uint64_t entry = load_bias + ehdr->e_entry;
        if (runtime_range_ok(runtime_start, image_span, entry, 1)) {
            module_out->load_base  = load_base_ptr;
            module_out->page_count = page_count;
            module_out->image_span = image_span;
            module_out->entry      = entry;
            ok = true;
        }
    }

    if (!ok) {
        free_contiguous_pages(load_base_ptr, page_count);
    }

    return ok;
}