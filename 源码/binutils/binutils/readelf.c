/* readelf.c -- display contents of an ELF format file
   trim for ARM 32-bit by request, from:
   Copyright 1998-2013 Free Software Foundation, Inc.

   Originally developed by Eric Youngdale <eric@andante.jic.com>
   Modifications by Nick Clifton <nickc@redhat.com>

   This file is part of GNU Binutils.

   This program is free software; ... GPLv3 ...  */

/* The difference between readelf and objdump: ... */

#include "sysdep.h"
#include <assert.h>
#include <time.h>
#ifdef HAVE_WCHAR_H
#include <wchar.h>
#endif

#include "bfd.h"
#include "bucomm.h"
#include "elfcomm.h"

#include "elf/common.h"
#include "elf/external.h"
#include "elf/internal.h"

/* Generate elf_arm_reloc_type().  */
#define RELOC_MACROS_GEN_FUNC
#include "elf/arm.h"

#include "getopt.h"
#include "libiberty.h"
#include "safe-ctype.h"
#include "filenames.h"

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &(((TYPE *) 0)->MEMBER))
#endif

char * program_name = "readelf";
static long archive_file_offset;
static unsigned long archive_file_size;
static unsigned long dynamic_addr;
static bfd_size_type dynamic_size;
static unsigned int dynamic_nent;
static char * dynamic_strings;
static unsigned long dynamic_strings_length;
static char * string_table;
static unsigned long string_table_length;
static unsigned long num_dynamic_syms;
static Elf_Internal_Sym * dynamic_symbols;
static Elf_Internal_Syminfo * dynamic_syminfo;
static unsigned long dynamic_syminfo_offset;
static unsigned int dynamic_syminfo_nent;
static char program_interpreter[PATH_MAX];
static bfd_vma dynamic_info[DT_ENCODING];
static bfd_vma dynamic_info_DT_GNU_HASH;
static Elf_Internal_Ehdr elf_header;
static Elf_Internal_Shdr * section_headers;
static Elf_Internal_Phdr * program_headers;
static Elf_Internal_Dyn *  dynamic_section;
static Elf_Internal_Shdr * symtab_shndx_hdr;
static int show_name;
static int do_dynamic;
static int do_syms;
static int do_dyn_syms;
static int do_reloc;
static int do_sections;
static int do_section_details;
static int do_segments;
static int do_using_dynamic;
static int do_header;
static int do_dump;
static int do_arch;
static int do_notes;
extern int do_wide;
static int is_32bit_elf;

/* Flag bits indicating particular types of dump.  */
#define HEX_DUMP	(1 << 0)	/* The -x command line switch.  */
#define STRING_DUMP     (1 << 3)	/* The -p command line switch.  */
#define RELOC_DUMP      (1 << 4)	/* The -R command line switch.  */

typedef unsigned char dump_type;

/* A linked list of the section names for which dumps were requested.  */
struct dump_list_entry
{
  char * name;
  dump_type type;
  struct dump_list_entry * next;
};
static struct dump_list_entry * dump_sects_byname;

static dump_type *   cmdline_dump_sects = NULL;
static unsigned int  num_cmdline_dump_sects = 0;
static dump_type *   dump_sects = NULL;
static unsigned int  num_dump_sects = 0;

/* How to print a vma value.  */
typedef enum print_mode
{
  HEX,
  DEC,
  DEC_5,
  UNSIGNED,
  PREFIX_HEX,
  FULL_HEX,
  LONG_HEX
}
print_mode;

#define UNKNOWN -1

#define SECTION_NAME(X)						\
  ((X) == NULL ? _("<none>")					\
   : string_table == NULL ? _("<no-name>")			\
   : ((X)->sh_name >= string_table_length ? _("<corrupt>")	\
  : string_table + (X)->sh_name))

#define VALID_DYNAMIC_NAME(offset)	((dynamic_strings != NULL) && (offset < dynamic_strings_length))
#define GET_DYNAMIC_NAME(offset)	(dynamic_strings + offset)

/* Retrieve NMEMB structures, each SIZE bytes long from FILE starting at OFFSET.
   Put the retrieved data into VAR, if it is not NULL.  Otherwise allocate a buffer
   using malloc and fill that.  In either case return the pointer to the start of
   the retrieved data or NULL if something went wrong.  */

static void *
get_data (void * var, FILE * file, long offset, size_t size, size_t nmemb,
	  const char * reason)
{
  void * mvar;

  if (size == 0 || nmemb == 0)
    return NULL;

  if (fseek (file, archive_file_offset + offset, SEEK_SET))
    {
      error (_("Unable to seek to 0x%lx for %s\n"),
	     (unsigned long) archive_file_offset + offset, reason);
      return NULL;
    }

  mvar = var;
  if (mvar == NULL)
    {
      /* Check for overflow.  */
      if (nmemb < (~(size_t) 0 - 1) / size)
	/* + 1 so that we can '\0' terminate invalid string table sections.  */
	mvar = malloc (size * nmemb + 1);

      if (mvar == NULL)
	{
	  error (_("Out of memory allocating 0x%lx bytes for %s\n"),
		 (unsigned long)(size * nmemb), reason);
	  return NULL;
	}

      ((char *) mvar)[size * nmemb] = '\0';
    }

  if (fread (mvar, size, nmemb, file) != nmemb)
    {
      error (_("Unable to read in 0x%lx bytes of %s\n"),
	     (unsigned long)(size * nmemb), reason);
      if (mvar != var)
	free (mvar);
      return NULL;
    }

  return mvar;
}

static void *
cmalloc (size_t nmemb, size_t size)
{
  if (nmemb >= ~(size_t) 0 / size)
    return NULL;
  return malloc (nmemb * size);
}

static unsigned long
read_leb128 (unsigned char *data,
	     unsigned int *length_return,
	     bfd_boolean sign,
	     const unsigned char * const end)
{
  unsigned long result = 0;
  unsigned int num_read = 0;
  unsigned int shift = 0;
  unsigned char byte = 0;

  while (data < end)
    {
      byte = *data++;
      num_read++;
      result |= ((unsigned long) (byte & 0x7f)) << shift;
      shift += 7;
      if ((byte & 0x80) == 0)
	break;
    }

  if (length_return != NULL)
    *length_return = num_read;

  /* 本工具只以 sign=FALSE 调用，此分支仅为兼容原接口保留。 */
  if (sign && (shift < 8 * sizeof (result)) && (byte & 0x40))
    result |= ((~(unsigned long) 0) << shift);

  return result;
}

/* Print a VMA value.  */

static int
print_vma (bfd_vma vma, print_mode mode)
{
  int nc = 0;

  switch (mode)
    {
    case FULL_HEX:
      nc = printf ("0x");
      /* Drop through.  */

    case LONG_HEX:
      return nc + printf ("%8.8lx", (unsigned long) vma);

    case DEC_5:
      if (vma <= 99999)
	return printf ("%5lu", (unsigned long) vma);
      /* Drop through.  */

    case PREFIX_HEX:
      nc = printf ("0x");
      /* Drop through.  */

    case HEX:
      return nc + printf ("%lx", (unsigned long) vma);

    case DEC:
      return printf ("%lu", (unsigned long) vma);

    case UNSIGNED:
      return printf ("%lu", (unsigned long) vma);
    }
  return 0;
}

/* Display a symbol on stdout. */

static unsigned int
print_symbol (int width, const char *symbol)
{
  bfd_boolean extra_padding = FALSE;
  int num_printed = 0;
#ifdef HAVE_MBSTATE_T
  mbstate_t state;
#endif
  int width_remaining;

  if (width < 0)
    {
      width = - width;
      extra_padding = TRUE;
    }

  if (do_wide)
    width_remaining = INT_MAX;
  else
    width_remaining = width;

#ifdef HAVE_MBSTATE_T
  memset (& state, 0, sizeof (state));
#endif

  while (width_remaining)
    {
      size_t  n;
      const char c = *symbol++;

      if (c == 0)
	break;

      if (ISCNTRL (c))
	{
	  if (width_remaining < 2)
	    break;

	  printf ("^%c", c + 0x40);
	  width_remaining -= 2;
	  num_printed += 2;
	}
      else if (ISPRINT (c))
	{
	  putchar (c);
	  width_remaining --;
	  num_printed ++;
	}
      else
	{
#ifdef HAVE_MBSTATE_T
	  wchar_t w;
#endif
	  printf ("%.1s", symbol - 1);
	  width_remaining --;
	  num_printed ++;

#ifdef HAVE_MBSTATE_T
	  n = mbrtowc (& w, symbol - 1, MB_CUR_MAX, & state);
#else
	  n = 1;
#endif
	  if (n != (size_t) -1 && n != (size_t) -2 && n > 0)
	    symbol += (n - 1);
	}
    }

  if (extra_padding && num_printed < width)
    {
      printf ("%-*s", width - num_printed, " ");
      num_printed = width;
    }

  return num_printed;
}

/* Return a pointer to section NAME, or NULL if no such section exists.  */

static Elf_Internal_Shdr *
find_section (const char * name)
{
  unsigned int i;

  for (i = 0; i < elf_header.e_shnum; i++)
    if (streq (SECTION_NAME (section_headers + i), name))
      return section_headers + i;

  return NULL;
}

/* Read an unsigned LEB128 encoded value from p.  Set *PLEN to the number of
   bytes read.  */

static inline unsigned long
read_uleb128 (unsigned char *data,
	      unsigned int *length_return,
	      const unsigned char * const end)
{
  return read_leb128 (data, length_return, FALSE, end);
}

/* Guess the relocation size commonly used by the specific machines.  */

static int
guess_is_rela (unsigned int e_machine)
{
  if (e_machine == EM_ARM)
    return FALSE;

  warn (_("Don't know about relocations on this machine architecture\n"));
  return FALSE;
}

static int
slurp_rela_relocs (FILE * file,
		   unsigned long rel_offset,
		   unsigned long rel_size,
		   Elf_Internal_Rela ** relasp,
		   unsigned long * nrelasp)
{
  Elf_Internal_Rela * relas;
  unsigned long nrelas;
  unsigned int i;
  Elf32_External_Rela * erelas;

  erelas = (Elf32_External_Rela *) get_data (NULL, file, rel_offset, 1,
                                             rel_size, _("32-bit relocation data"));
  if (!erelas)
    return 0;

  nrelas = rel_size / sizeof (Elf32_External_Rela);

  relas = (Elf_Internal_Rela *) cmalloc (nrelas,
                                         sizeof (Elf_Internal_Rela));

  if (relas == NULL)
    {
      free (erelas);
      error (_("out of memory parsing relocs\n"));
      return 0;
    }

  for (i = 0; i < nrelas; i++)
    {
      relas[i].r_offset = BYTE_GET (erelas[i].r_offset);
      relas[i].r_info   = BYTE_GET (erelas[i].r_info);
      relas[i].r_addend = BYTE_GET_SIGNED (erelas[i].r_addend);
    }

  free (erelas);

  *relasp = relas;
  *nrelasp = nrelas;
  return 1;
}

static int
slurp_rel_relocs (FILE * file,
		  unsigned long rel_offset,
		  unsigned long rel_size,
		  Elf_Internal_Rela ** relsp,
		  unsigned long * nrelsp)
{
  Elf_Internal_Rela * rels;
  unsigned long nrels;
  unsigned int i;
  Elf32_External_Rel * erels;

  erels = (Elf32_External_Rel *) get_data (NULL, file, rel_offset, 1,
                                           rel_size, _("32-bit relocation data"));
  if (!erels)
    return 0;

  nrels = rel_size / sizeof (Elf32_External_Rel);

  rels = (Elf_Internal_Rela *) cmalloc (nrels, sizeof (Elf_Internal_Rela));

  if (rels == NULL)
    {
      free (erels);
      error (_("out of memory parsing relocs\n"));
      return 0;
    }

  for (i = 0; i < nrels; i++)
    {
      rels[i].r_offset = BYTE_GET (erels[i].r_offset);
      rels[i].r_info   = BYTE_GET (erels[i].r_info);
      rels[i].r_addend = 0;
    }

  free (erels);

  *relsp = rels;
  *nrelsp = nrels;
  return 1;
}

/* Returns the reloc type extracted from the reloc info field.  */

static unsigned int
get_reloc_type (bfd_vma reloc_info)
{
  return ELF32_R_TYPE (reloc_info);
}

/* Return the symbol index extracted from the reloc info field.  */

static bfd_vma
get_reloc_symindex (bfd_vma reloc_info)
{
  return ELF32_R_SYM (reloc_info);
}

/* Display the contents of the relocation data found at the specified
   offset.  */

static void
dump_relocations (FILE * file,
		  unsigned long rel_offset,
		  unsigned long rel_size,
		  Elf_Internal_Sym * symtab,
		  unsigned long nsyms,
		  char * strtab,
		  unsigned long strtablen,
		  int is_rela)
{
  unsigned int i;
  Elf_Internal_Rela * rels;

  if (is_rela == UNKNOWN)
    is_rela = guess_is_rela (elf_header.e_machine);

  if (is_rela)
    {
      if (!slurp_rela_relocs (file, rel_offset, rel_size, &rels, &rel_size))
	return;
    }
  else
    {
      if (!slurp_rel_relocs (file, rel_offset, rel_size, &rels, &rel_size))
	return;
    }

  if (is_rela)
    {
      if (do_wide)
	printf (_(" Offset     Info    Type                Sym. Value  Symbol's Name + Addend\n"));
      else
	printf (_(" Offset     Info    Type            Sym.Value  Sym. Name + Addend\n"));
    }
  else
    {
      if (do_wide)
	printf (_(" Offset     Info    Type                Sym. Value  Symbol's Name\n"));
      else
	printf (_(" Offset     Info    Type            Sym.Value  Sym. Name\n"));
    }

  for (i = 0; i < rel_size; i++)
    {
      const char * rtype;
      bfd_vma offset;
      bfd_vma inf;
      bfd_vma symtab_index;
      bfd_vma type;

      offset = rels[i].r_offset;
      inf    = rels[i].r_info;

      type = ELF32_R_TYPE (inf);
      symtab_index = ELF32_R_SYM (inf);

      printf ("%8.8lx  %8.8lx ",
	      (unsigned long) offset & 0xffffffff,
	      (unsigned long) inf & 0xffffffff);

      switch (elf_header.e_machine)
	{
	case EM_ARM:
	  rtype = elf_arm_reloc_type (type);
	  break;

	default:
	  rtype = NULL;
	  break;
	}

      if (rtype == NULL)
	printf (_("unrecognized: %-7lx"), (unsigned long) type & 0xffffffff);
      else
	printf (do_wide ? "%-22.22s" : "%-17.17s", rtype);

      if (symtab_index)
	{
	  if (symtab == NULL || symtab_index >= nsyms)
	    printf (_(" bad symbol index: %08lx"), (unsigned long) symtab_index);
	  else
	    {
	      Elf_Internal_Sym * psym;

	      psym = symtab + symtab_index;

	      printf (" ");

	      if (ELF_ST_TYPE (psym->st_info) == STT_GNU_IFUNC)
		{
		  const char * name;
		  unsigned int len;
		  unsigned int width = 8;

		  if (strtab == NULL
		      || psym->st_name == 0
		      || psym->st_name >= strtablen)
		    name = "??";
		  else
		    name = strtab + psym->st_name;

		  len = print_symbol (width, name);
		  printf ("()%-*s", len <= width ? (width + 1) - len : 1, " ");
		}
	      else
		{
		  print_vma (psym->st_value, LONG_HEX);

		  printf ("   ");
		}

	      if (psym->st_name == 0)
		{
		  const char * sec_name = "<null>";
		  char name_buf[40];

		  if (ELF_ST_TYPE (psym->st_info) == STT_SECTION)
		    {
		      if (psym->st_shndx < elf_header.e_shnum)
			sec_name
			  = SECTION_NAME (section_headers + psym->st_shndx);
		      else if (psym->st_shndx == SHN_ABS)
			sec_name = "ABS";
		      else if (psym->st_shndx == SHN_COMMON)
			sec_name = "COMMON";
		      else
			{
			  sprintf (name_buf, "<section 0x%x>",
				   (unsigned int) psym->st_shndx);
			  sec_name = name_buf;
			}
		    }
		  print_symbol (22, sec_name);
		}
	      else if (strtab == NULL)
		printf (_("<string table index: %3ld>"), psym->st_name);
	      else if (psym->st_name >= strtablen)
		printf (_("<corrupt string table index: %3ld>"), psym->st_name);
	      else
		print_symbol (22, strtab + psym->st_name);

	      if (is_rela)
		{
		  bfd_signed_vma off = rels[i].r_addend;

		  if (off < 0)
		    printf (" - %lx", (unsigned long) - off);
		  else
		    printf (" + %lx", (unsigned long) off);
		}
	    }
	}
      else if (is_rela)
	{
	  bfd_signed_vma off = rels[i].r_addend;

	  printf ("%*c", 12, ' ');
	  if (off < 0)
	    printf ("-%lx", (unsigned long) - off);
	  else
	    printf ("%lx", (unsigned long) off);
	}

      putchar ('\n');
    }

  free (rels);
}

static const char *
get_dynamic_type (unsigned long type)
{
  static char buff[64];

  switch (type)
    {
    case DT_NULL:	return "NULL";
    case DT_NEEDED:	return "NEEDED";
    case DT_PLTRELSZ:	return "PLTRELSZ";
    case DT_PLTGOT:	return "PLTGOT";
    case DT_HASH:	return "HASH";
    case DT_STRTAB:	return "STRTAB";
    case DT_SYMTAB:	return "SYMTAB";
    case DT_RELA:	return "RELA";
    case DT_RELASZ:	return "RELASZ";
    case DT_RELAENT:	return "RELAENT";
    case DT_STRSZ:	return "STRSZ";
    case DT_SYMENT:	return "SYMENT";
    case DT_INIT:	return "INIT";
    case DT_FINI:	return "FINI";
    case DT_SONAME:	return "SONAME";
    case DT_RPATH:	return "RPATH";
    case DT_SYMBOLIC:	return "SYMBOLIC";
    case DT_REL:	return "REL";
    case DT_RELSZ:	return "RELSZ";
    case DT_RELENT:	return "RELENT";
    case DT_PLTREL:	return "PLTREL";
    case DT_DEBUG:	return "DEBUG";
    case DT_TEXTREL:	return "TEXTREL";
    case DT_JMPREL:	return "JMPREL";
    case DT_BIND_NOW:   return "BIND_NOW";
    case DT_INIT_ARRAY: return "INIT_ARRAY";
    case DT_FINI_ARRAY: return "FINI_ARRAY";
    case DT_INIT_ARRAYSZ: return "INIT_ARRAYSZ";
    case DT_FINI_ARRAYSZ: return "FINI_ARRAYSZ";
    case DT_RUNPATH:    return "RUNPATH";
    case DT_FLAGS:      return "FLAGS";

    case DT_PREINIT_ARRAY: return "PREINIT_ARRAY";
    case DT_PREINIT_ARRAYSZ: return "PREINIT_ARRAYSZ";

    case DT_CHECKSUM:	return "CHECKSUM";
    case DT_PLTPADSZ:	return "PLTPADSZ";
    case DT_MOVEENT:	return "MOVEENT";
    case DT_MOVESZ:	return "MOVESZ";
    case DT_FEATURE:	return "FEATURE";
    case DT_POSFLAG_1:	return "POSFLAG_1";
    case DT_SYMINSZ:	return "SYMINSZ";
    case DT_SYMINENT:	return "SYMINENT";

    case DT_ADDRRNGLO:  return "ADDRRNGLO";
    case DT_CONFIG:	return "CONFIG";
    case DT_DEPAUDIT:	return "DEPAUDIT";
    case DT_AUDIT:	return "AUDIT";
    case DT_PLTPAD:	return "PLTPAD";
    case DT_MOVETAB:	return "MOVETAB";
    case DT_SYMINFO:	return "SYMINFO";

    case DT_VERSYM:	return "VERSYM";

    case DT_TLSDESC_GOT: return "TLSDESC_GOT";
    case DT_TLSDESC_PLT: return "TLSDESC_PLT";
    case DT_RELACOUNT:	return "RELACOUNT";
    case DT_RELCOUNT:	return "RELCOUNT";
    case DT_FLAGS_1:	return "FLAGS_1";
    case DT_VERDEF:	return "VERDEF";
    case DT_VERDEFNUM:	return "VERDEFNUM";
    case DT_VERNEED:	return "VERNEED";
    case DT_VERNEEDNUM:	return "VERNEEDNUM";

    case DT_AUXILIARY:	return "AUXILIARY";
    case DT_USED:	return "USED";
    case DT_FILTER:	return "FILTER";

    case DT_GNU_PRELINKED: return "GNU_PRELINKED";
    case DT_GNU_CONFLICT: return "GNU_CONFLICT";
    case DT_GNU_CONFLICTSZ: return "GNU_CONFLICTSZ";
    case DT_GNU_LIBLIST: return "GNU_LIBLIST";
    case DT_GNU_LIBLISTSZ: return "GNU_LIBLISTSZ";
    case DT_GNU_HASH:	return "GNU_HASH";

    default:
      if ((type >= DT_LOPROC) && (type <= DT_HIPROC))
	snprintf (buff, sizeof (buff), _("Processor Specific: %lx"), type);
      else if ((type >= DT_LOOS) && (type <= DT_HIOS))
	snprintf (buff, sizeof (buff), _("Operating System specific: %lx"),
		  type);
      else
	snprintf (buff, sizeof (buff), _("<unknown>: %lx"), type);

      return buff;
    }
}

static char *
get_file_type (unsigned e_type)
{
  static char buff[32];

  switch (e_type)
    {
    case ET_NONE:	return _("NONE (None)");
    case ET_REL:	return _("REL (Relocatable file)");
    case ET_EXEC:	return _("EXEC (Executable file)");
    case ET_DYN:	return _("DYN (Shared object file)");
    case ET_CORE:	return _("CORE (Core file)");

    default:
      if ((e_type >= ET_LOPROC) && (e_type <= ET_HIPROC))
	snprintf (buff, sizeof (buff), _("Processor Specific: (%x)"), e_type);
      else if ((e_type >= ET_LOOS) && (e_type <= ET_HIOS))
	snprintf (buff, sizeof (buff), _("OS Specific: (%x)"), e_type);
      else
	snprintf (buff, sizeof (buff), _("<unknown>: %x"), e_type);
      return buff;
    }
}

static char *
get_machine_name (unsigned e_machine)
{
  static char buff[64];

  switch (e_machine)
    {
    case EM_ARM:		return "ARM";
    default:
      snprintf (buff, sizeof (buff), _("<unknown>: 0x%x"), e_machine);
      return buff;
    }
}

static void
decode_ARM_machine_flags (unsigned e_flags, char buf[])
{
  unsigned eabi;
  int unknown = 0;

  eabi = EF_ARM_EABI_VERSION (e_flags);
  e_flags &= ~ EF_ARM_EABIMASK;

  /* Handle "generic" ARM flags.  */
  if (e_flags & EF_ARM_RELEXEC)
    {
      strcat (buf, ", relocatable executable");
      e_flags &= ~ EF_ARM_RELEXEC;
    }

  if (e_flags & EF_ARM_HASENTRY)
    {
      strcat (buf, ", has entry point");
      e_flags &= ~ EF_ARM_HASENTRY;
    }

  switch (eabi)
    {
    default:
      strcat (buf, ", <unrecognized EABI>");
      if (e_flags)
	unknown = 1;
      break;

    case EF_ARM_EABI_VER1:
      strcat (buf, ", Version1 EABI");
      while (e_flags)
	{
	  unsigned flag;

	  flag = e_flags & - e_flags;
	  e_flags &= ~ flag;

	  switch (flag)
	    {
	    case EF_ARM_SYMSARESORTED:
	      strcat (buf, ", sorted symbol tables");
	      break;

	    default:
	      unknown = 1;
	      break;
	    }
	}
      break;

    case EF_ARM_EABI_VER2:
      strcat (buf, ", Version2 EABI");
      while (e_flags)
	{
	  unsigned flag;

	  flag = e_flags & - e_flags;
	  e_flags &= ~ flag;

	  switch (flag)
	    {
	    case EF_ARM_SYMSARESORTED:
	      strcat (buf, ", sorted symbol tables");
	      break;

	    case EF_ARM_DYNSYMSUSESEGIDX:
	      strcat (buf, ", dynamic symbols use segment index");
	      break;

	    case EF_ARM_MAPSYMSFIRST:
	      strcat (buf, ", mapping symbols precede others");
	      break;

	    default:
	      unknown = 1;
	      break;
	    }
	}
      break;

    case EF_ARM_EABI_VER3:
      strcat (buf, ", Version3 EABI");
      break;

    case EF_ARM_EABI_VER4:
      strcat (buf, ", Version4 EABI");
      while (e_flags)
	{
	  unsigned flag;

	  flag = e_flags & - e_flags;
	  e_flags &= ~ flag;

	  switch (flag)
	    {
	    case EF_ARM_BE8:
	      strcat (buf, ", BE8");
	      break;

	    case EF_ARM_LE8:
	      strcat (buf, ", LE8");
	      break;

	    default:
	      unknown = 1;
	      break;
	    }
      break;
	}
      break;

    case EF_ARM_EABI_VER5:
      strcat (buf, ", Version5 EABI");
      while (e_flags)
	{
	  unsigned flag;

	  flag = e_flags & - e_flags;
	  e_flags &= ~ flag;

	  switch (flag)
	    {
	    case EF_ARM_BE8:
	      strcat (buf, ", BE8");
	      break;

	    case EF_ARM_LE8:
	      strcat (buf, ", LE8");
	      break;

	    case EF_ARM_ABI_FLOAT_SOFT:
	      strcat (buf, ", soft-float ABI");
	      break;

	    case EF_ARM_ABI_FLOAT_HARD:
	      strcat (buf, ", hard-float ABI");
	      break;

	    default:
	      unknown = 1;
	      break;
	    }
	}
      break;

    case EF_ARM_EABI_UNKNOWN:
      strcat (buf, ", GNU EABI");
      while (e_flags)
	{
	  unsigned flag;

	  flag = e_flags & - e_flags;
	  e_flags &= ~ flag;

	  switch (flag)
	    {
	    case EF_ARM_INTERWORK:
	      strcat (buf, ", interworking enabled");
	      break;

	    case EF_ARM_APCS_26:
	      strcat (buf, ", uses APCS/26");
	      break;

	    case EF_ARM_APCS_FLOAT:
	      strcat (buf, ", uses APCS/float");
	      break;

	    case EF_ARM_PIC:
	      strcat (buf, ", position independent");
	      break;

	    case EF_ARM_ALIGN8:
	      strcat (buf, ", 8 bit structure alignment");
	      break;

	    case EF_ARM_NEW_ABI:
	      strcat (buf, ", uses new ABI");
	      break;

	    case EF_ARM_OLD_ABI:
	      strcat (buf, ", uses old ABI");
	      break;

	    case EF_ARM_SOFT_FLOAT:
	      strcat (buf, ", software FP");
	      break;

	    case EF_ARM_VFP_FLOAT:
	      strcat (buf, ", VFP");
	      break;

	    case EF_ARM_MAVERICK_FLOAT:
	      strcat (buf, ", Maverick FP");
	      break;

	    default:
	      unknown = 1;
	      break;
	    }
	}
    }

  if (unknown)
    strcat (buf,_(", <unknown>"));
}

static char *
get_machine_flags (unsigned e_flags, unsigned e_machine)
{
  static char buf[1024];

  buf[0] = '\0';

  if (e_flags)
    {
      switch (e_machine)
	{
	case EM_ARM:
	  decode_ARM_machine_flags (e_flags, buf);
	  break;

	default:
	  break;
	}
    }

  return buf;
}

static const char *
get_osabi_name (unsigned int osabi)
{
  static char buff[32];

  switch (osabi)
    {
    case ELFOSABI_NONE:		return "UNIX - System V";
    case ELFOSABI_HPUX:		return "UNIX - HP-UX";
    case ELFOSABI_NETBSD:	return "UNIX - NetBSD";
    case ELFOSABI_GNU:		return "UNIX - GNU";
    case ELFOSABI_SOLARIS:	return "UNIX - Solaris";
    case ELFOSABI_AIX:		return "UNIX - AIX";
    case ELFOSABI_IRIX:		return "UNIX - IRIX";
    case ELFOSABI_FREEBSD:	return "UNIX - FreeBSD";
    case ELFOSABI_TRU64:	return "UNIX - TRU64";
    case ELFOSABI_MODESTO:	return "Novell - Modesto";
    case ELFOSABI_OPENBSD:	return "UNIX - OpenBSD";
    case ELFOSABI_OPENVMS:	return "VMS - OpenVMS";
    case ELFOSABI_NSK:		return "HP - Non-Stop Kernel";
    case ELFOSABI_AROS:		return "AROS";
    case ELFOSABI_FENIXOS:	return "FenixOS";
    default:
      if (osabi >= 64)
	switch (elf_header.e_machine)
	  {
	  case EM_ARM:
	    switch (osabi)
	      {
	      case ELFOSABI_ARM:	return "ARM";
	      default:
		break;
	      }
	    break;

	  default:
	    break;
	  }
      snprintf (buff, sizeof (buff), _("<unknown: %x>"), osabi);
      return buff;
    }
}

static const char *
get_arm_segment_type (unsigned long type)
{
  switch (type)
    {
    case PT_ARM_EXIDX:
      return "EXIDX";
    default:
      break;
    }

  return NULL;
}

static const char *
get_segment_type (unsigned long p_type)
{
  static char buff[32];

  switch (p_type)
    {
    case PT_NULL:	return "NULL";
    case PT_LOAD:	return "LOAD";
    case PT_DYNAMIC:	return "DYNAMIC";
    case PT_INTERP:	return "INTERP";
    case PT_NOTE:	return "NOTE";
    case PT_SHLIB:	return "SHLIB";
    case PT_PHDR:	return "PHDR";
    case PT_TLS:	return "TLS";

    case PT_GNU_EH_FRAME:
			return "GNU_EH_FRAME";
    case PT_GNU_STACK:	return "GNU_STACK";
    case PT_GNU_RELRO:  return "GNU_RELRO";

    default:
      if ((p_type >= PT_LOPROC) && (p_type <= PT_HIPROC))
	{
	  const char * result;

	  switch (elf_header.e_machine)
	    {
	    case EM_ARM:
	      result = get_arm_segment_type (p_type);
	      break;
	    default:
	      result = NULL;
	      break;
	    }

	  if (result != NULL)
	    return result;

	  sprintf (buff, "LOPROC+%lx", p_type - PT_LOPROC);
	}
      else if ((p_type >= PT_LOOS) && (p_type <= PT_HIOS))
	sprintf (buff, "LOOS+%lx", p_type - PT_LOOS);
      else
	snprintf (buff, sizeof (buff), _("<unknown>: %lx"), p_type);

      return buff;
    }
}

static const char *
get_arm_section_type_name (unsigned int sh_type)
{
  switch (sh_type)
    {
    case SHT_ARM_EXIDX:           return "ARM_EXIDX";
    case SHT_ARM_PREEMPTMAP:      return "ARM_PREEMPTMAP";
    case SHT_ARM_ATTRIBUTES:      return "ARM_ATTRIBUTES";
    case SHT_ARM_DEBUGOVERLAY:    return "ARM_DEBUGOVERLAY";
    case SHT_ARM_OVERLAYSECTION:  return "ARM_OVERLAYSECTION";
    default:
      break;
    }
  return NULL;
}

static const char *
get_section_type_name (unsigned int sh_type)
{
  static char buff[32];

  switch (sh_type)
    {
    case SHT_NULL:		return "NULL";
    case SHT_PROGBITS:		return "PROGBITS";
    case SHT_SYMTAB:		return "SYMTAB";
    case SHT_STRTAB:		return "STRTAB";
    case SHT_RELA:		return "RELA";
    case SHT_HASH:		return "HASH";
    case SHT_DYNAMIC:		return "DYNAMIC";
    case SHT_NOTE:		return "NOTE";
    case SHT_NOBITS:		return "NOBITS";
    case SHT_REL:		return "REL";
    case SHT_SHLIB:		return "SHLIB";
    case SHT_DYNSYM:		return "DYNSYM";
    case SHT_INIT_ARRAY:	return "INIT_ARRAY";
    case SHT_FINI_ARRAY:	return "FINI_ARRAY";
    case SHT_PREINIT_ARRAY:	return "PREINIT_ARRAY";
    case SHT_GNU_HASH:		return "GNU_HASH";
    case SHT_GROUP:		return "GROUP";
    case SHT_SYMTAB_SHNDX:	return "SYMTAB SECTION INDICIES";
    case SHT_GNU_verdef:	return "VERDEF";
    case SHT_GNU_verneed:	return "VERNEED";
    case SHT_GNU_versym:	return "VERSYM";
    case 0x6ffffff0:		return "VERSYM";
    case 0x6ffffffc:		return "VERDEF";
    case 0x7ffffffd:		return "AUXILIARY";
    case 0x7fffffff:		return "FILTER";
    case SHT_GNU_LIBLIST:	return "GNU_LIBLIST";

    default:
      if ((sh_type >= SHT_LOPROC) && (sh_type <= SHT_HIPROC))
	{
	  const char * result;

	  switch (elf_header.e_machine)
	    {
	    case EM_ARM:
	      result = get_arm_section_type_name (sh_type);
	      break;
	    default:
	      result = NULL;
	      break;
	    }

	  if (result != NULL)
	    return result;

	  sprintf (buff, "LOPROC+%x", sh_type - SHT_LOPROC);
	}
      else if ((sh_type >= SHT_LOOS) && (sh_type <= SHT_HIOS))
	sprintf (buff, "LOOS+%x", sh_type - SHT_LOOS);
      else if ((sh_type >= SHT_LOUSER) && (sh_type <= SHT_HIUSER))
	sprintf (buff, "LOUSER+%x", sh_type - SHT_LOUSER);
      else
	/* This message is probably going to be displayed in a 15
	   character wide field, so put the hex value first.  */
	snprintf (buff, sizeof (buff), _("%08x: <unknown>"), sh_type);

      return buff;
    }
}

#define OPTION_DYN_SYMS	513

static struct option options[] =
{
  {"all",	       no_argument, 0, 'a'},
  {"file-header",      no_argument, 0, 'h'},
  {"program-headers",  no_argument, 0, 'l'},
  {"segments",	       no_argument, 0, 'l'},
  {"sections",	       no_argument, 0, 'S'},
  {"section-headers",  no_argument, 0, 'S'},
  {"section-details",  no_argument, 0, 't'},
  {"symbols",	       no_argument, 0, 's'},
  {"syms",	       no_argument, 0, 's'},
  {"dyn-syms",	       no_argument, 0, OPTION_DYN_SYMS},
  {"relocs",	       no_argument, 0, 'r'},
  {"notes",	       no_argument, 0, 'n'},
  {"dynamic",	       no_argument, 0, 'd'},
  {"arch-specific",    no_argument, 0, 'A'},
  {"use-dynamic",      no_argument, 0, 'D'},
  {"hex-dump",	       required_argument, 0, 'x'},
  {"relocated-dump",   required_argument, 0, 'R'},
  {"string-dump",      required_argument, 0, 'p'},
  {"version",	       no_argument, 0, 'v'},
  {"wide",	       no_argument, 0, 'W'},
  {"help",	       no_argument, 0, 'H'},
  {0,		       no_argument, 0, 0}
};

static void
usage (FILE * stream)
{
  fprintf (stream, _("Usage: readelf <option(s)> elf-file(s)\n"));
  fprintf (stream, _(" Display information about the contents of ELF format files\n"));
  fprintf (stream, _(" Options are:\n\
  -a --all               Equivalent to: -h -l -S -s -r -d -A\n\
  -h --file-header       Display the ELF file header\n\
  -l --program-headers   Display the program headers\n\
     --segments          An alias for --program-headers\n\
  -S --section-headers   Display the sections' header\n\
     --sections          An alias for --section-headers\n\
  -t --section-details   Display the section details\n\
  -s --syms              Display the symbol table\n\
     --symbols           An alias for --syms\n\
  --dyn-syms             Display the dynamic symbol table\n\
  -n --notes             Display the core notes (if present)\n\
  -r --relocs            Display the relocations (if present)\n\
  -d --dynamic           Display the dynamic section (if present)\n\
  -A --arch-specific     Display architecture specific information (if any)\n\
  -D --use-dynamic       Use the dynamic section info when displaying symbols\n\
  -x --hex-dump=<number|name>\n\
                         Dump the contents of section <number|name> as bytes\n\
  -p --string-dump=<number|name>\n\
                         Dump the contents of section <number|name> as strings\n\
  -R --relocated-dump=<number|name>\n\
                         Dump the contents of section <number|name> as relocated bytes\n\
  -W --wide              Allow output width to exceed 80 characters\n\
  @<file>                Read options from <file>\n\
  -H --help              Display this information\n\
  -v --version           Display the version number of readelf\n"));

  if (REPORT_BUGS_TO[0] && stream == stdout)
    fprintf (stdout, _("Report bugs to %s\n"), REPORT_BUGS_TO);

  exit (stream == stdout ? 0 : 1);
}

/* Record the fact that the user wants the contents of section number
   SECTION to be displayed using the method(s) encoded as flags bits
   in TYPE.  */

static void
request_dump_bynumber (unsigned int section, dump_type type)
{
  if (section >= num_dump_sects)
    {
      dump_type * new_dump_sects;

      new_dump_sects = (dump_type *) calloc (section + 1,
                                             sizeof (* dump_sects));

      if (new_dump_sects == NULL)
	error (_("Out of memory allocating dump request table.\n"));
      else
	{
	  memcpy (new_dump_sects, dump_sects, num_dump_sects * sizeof (* dump_sects));

	  free (dump_sects);

	  dump_sects = new_dump_sects;
	  num_dump_sects = section + 1;
	}
    }

  if (dump_sects)
    dump_sects[section] |= type;

  return;
}

/* Request a dump by section name.  */

static void
request_dump_byname (const char * section, dump_type type)
{
  struct dump_list_entry * new_request;

  new_request = (struct dump_list_entry *)
      malloc (sizeof (struct dump_list_entry));
  if (!new_request)
    error (_("Out of memory allocating dump request table.\n"));

  new_request->name = strdup (section);
  if (!new_request->name)
    error (_("Out of memory allocating dump request table.\n"));

  new_request->type = type;

  new_request->next = dump_sects_byname;
  dump_sects_byname = new_request;
}

static inline void
request_dump (dump_type type)
{
  int section;
  char * cp;

  do_dump++;
  section = strtoul (optarg, & cp, 0);

  if (! *cp && section >= 0)
    request_dump_bynumber (section, type);
  else
    request_dump_byname (optarg, type);
}

static void
parse_args (int argc, char ** argv)
{
  int c;

  if (argc < 2)
    usage (stderr);

  while ((c = getopt_long
	  (argc, argv, "aADHx:p:R:hltSsrdnWv", options, NULL)) != EOF)
    {
      switch (c)
	{
	case 0:
	  /* Long options.  */
	  break;
	case 'H':
	  usage (stdout);
	  break;
	  
	case 'a':
	  do_syms++;
	  do_reloc++;
	  do_dynamic++;
	  do_header++;
	  do_sections++;
	  do_segments++;
	  do_arch++;
	  do_notes++;
	  break;

	case 'A':
	  do_arch++;
	  break;
	case 'D':
	  do_using_dynamic++;
	  break;
	case 'r':
	  do_reloc++;
	  break;
	case 'h':
	  do_header++;
	  break;
	case 'l':
	  do_segments++;
	  break;
	case 's':
	  do_syms++;
	  break;
	case 'S':
	  do_sections++;
	  break;
	case OPTION_DYN_SYMS:
	  do_dyn_syms++;
	  break;
	case 't':
	  do_sections++;
	  do_section_details++;
	  break;
	case 'd':
	  do_dynamic++;
	  break;
	case 'n':
	  do_notes++;
	  break;
	case 'x':
	  request_dump (HEX_DUMP);
	  break;
	case 'p':
	  request_dump (STRING_DUMP);
	  break;
	case 'R':
	  request_dump (RELOC_DUMP);
	  break;
	case 'v':
	  /* Display the version number of readelf.  */
	  printf ("GNU readelf (GNU Binutils) 2.24\n");
	  exit (0);
	case 'W':
	  do_wide++;
	  break;
	default:
	  /* xgettext:c-format */
	  error (_("Invalid option '-%c'\n"), c);
	  /* Drop through.  */
	case '?':
	  usage (stderr);
	}
    }

  if (!do_dynamic && !do_syms && !do_dyn_syms && !do_reloc && !do_sections
      && !do_segments && !do_header && !do_dump
      && !do_arch && !do_notes)
    usage (stderr);
  else if (argc < 3)
    {
      warn (_("Nothing to do.\n"));
      usage (stderr);
    }
}

static const char *
get_elf_class (unsigned int elf_class)
{
  static char buff[32];

  switch (elf_class)
    {
    case ELFCLASSNONE: return _("none");
    case ELFCLASS32:   return "ELF32";
    case ELFCLASS64:   return "ELF64";
    default:
      snprintf (buff, sizeof (buff), _("<unknown: %x>"), elf_class);
      return buff;
    }
}

static const char *
get_data_encoding (unsigned int encoding)
{
  static char buff[32];

  switch (encoding)
    {
    case ELFDATANONE: return _("none");
    case ELFDATA2LSB: return _("2's complement, little endian");
    case ELFDATA2MSB: return _("2's complement, big endian");
    default:
      snprintf (buff, sizeof (buff), _("<unknown: %x>"), encoding);
      return buff;
    }
}

/* Decode the data held in 'elf_header'.  */

static int
process_file_header (void)
{
  if (   elf_header.e_ident[EI_MAG0] != ELFMAG0
      || elf_header.e_ident[EI_MAG1] != ELFMAG1
      || elf_header.e_ident[EI_MAG2] != ELFMAG2
      || elf_header.e_ident[EI_MAG3] != ELFMAG3)
    {
      error
	(_("Not an ELF file - it has the wrong magic bytes at the start\n"));
      return 0;
    }

  if (do_header)
    {
      int i;

      printf (_("ELF Header:\n"));
      printf (_("  Magic:   "));
      for (i = 0; i < EI_NIDENT; i++)
	printf ("%2.2x ", elf_header.e_ident[i]);
      printf ("\n");
      printf (_("  Class:                             %s\n"),
	      get_elf_class (elf_header.e_ident[EI_CLASS]));
      printf (_("  Data:                              %s\n"),
	      get_data_encoding (elf_header.e_ident[EI_DATA]));
      printf (_("  Version:                           %d %s\n"),
	      elf_header.e_ident[EI_VERSION],
	      (elf_header.e_ident[EI_VERSION] == EV_CURRENT
	       ? "(current)"
	       : (elf_header.e_ident[EI_VERSION] != EV_NONE
		  ? _("<unknown: %lx>")
		  : "")));
      printf (_("  OS/ABI:                            %s\n"),
	      get_osabi_name (elf_header.e_ident[EI_OSABI]));
      printf (_("  ABI Version:                       %d\n"),
	      elf_header.e_ident[EI_ABIVERSION]);
      printf (_("  Type:                              %s\n"),
	      get_file_type (elf_header.e_type));
      printf (_("  Machine:                           %s\n"),
	      get_machine_name (elf_header.e_machine));
      printf (_("  Version:                           0x%lx\n"),
	      (unsigned long) elf_header.e_version);

      printf (_("  Entry point address:               "));
      print_vma ((bfd_vma) elf_header.e_entry, PREFIX_HEX);
      printf (_("\n  Start of program headers:          "));
      print_vma ((bfd_vma) elf_header.e_phoff, DEC);
      printf (_(" (bytes into file)\n  Start of section headers:          "));
      print_vma ((bfd_vma) elf_header.e_shoff, DEC);
      printf (_(" (bytes into file)\n"));

      printf (_("  Flags:                             0x%lx%s\n"),
	      (unsigned long) elf_header.e_flags,
	      get_machine_flags (elf_header.e_flags, elf_header.e_machine));
      printf (_("  Size of this header:               %ld (bytes)\n"),
	      (long) elf_header.e_ehsize);
      printf (_("  Size of program headers:           %ld (bytes)\n"),
	      (long) elf_header.e_phentsize);
      printf (_("  Number of program headers:         %ld"),
	      (long) elf_header.e_phnum);
      if (section_headers != NULL
	  && elf_header.e_phnum == PN_XNUM
	  && section_headers[0].sh_info != 0)
	printf (" (%ld)", (long) section_headers[0].sh_info);
      putc ('\n', stdout);
      printf (_("  Size of section headers:           %ld (bytes)\n"),
	      (long) elf_header.e_shentsize);
      printf (_("  Number of section headers:         %ld"),
	      (long) elf_header.e_shnum);
      if (section_headers != NULL && elf_header.e_shnum == SHN_UNDEF)
	printf (" (%ld)", (long) section_headers[0].sh_size);
      putc ('\n', stdout);
      printf (_("  Section header string table index: %ld"),
	      (long) elf_header.e_shstrndx);
      if (section_headers != NULL
	  && elf_header.e_shstrndx == (SHN_XINDEX & 0xffff))
	printf (" (%u)", section_headers[0].sh_link);
      else if (elf_header.e_shstrndx != SHN_UNDEF
	       && elf_header.e_shstrndx >= elf_header.e_shnum)
	printf (_(" <corrupt: out of range>"));
      putc ('\n', stdout);
    }

  if (section_headers != NULL)
    {
      if (elf_header.e_phnum == PN_XNUM
	  && section_headers[0].sh_info != 0)
	elf_header.e_phnum = section_headers[0].sh_info;
      if (elf_header.e_shnum == SHN_UNDEF)
	elf_header.e_shnum = section_headers[0].sh_size;
      if (elf_header.e_shstrndx == (SHN_XINDEX & 0xffff))
	elf_header.e_shstrndx = section_headers[0].sh_link;
      else if (elf_header.e_shstrndx >= elf_header.e_shnum)
	elf_header.e_shstrndx = SHN_UNDEF;
      free (section_headers);
      section_headers = NULL;
    }

  return 1;
}

static int
get_32bit_program_headers (FILE * file, Elf_Internal_Phdr * pheaders)
{
  Elf32_External_Phdr * phdrs;
  Elf32_External_Phdr * external;
  Elf_Internal_Phdr *   internal;
  unsigned int i;

  phdrs = (Elf32_External_Phdr *) get_data (NULL, file, elf_header.e_phoff,
                                            elf_header.e_phentsize,
                                            elf_header.e_phnum,
                                            _("program headers"));
  if (!phdrs)
    return 0;

  for (i = 0, internal = pheaders, external = phdrs;
       i < elf_header.e_phnum;
       i++, internal++, external++)
    {
      internal->p_type   = BYTE_GET (external->p_type);
      internal->p_offset = BYTE_GET (external->p_offset);
      internal->p_vaddr  = BYTE_GET (external->p_vaddr);
      internal->p_paddr  = BYTE_GET (external->p_paddr);
      internal->p_filesz = BYTE_GET (external->p_filesz);
      internal->p_memsz  = BYTE_GET (external->p_memsz);
      internal->p_flags  = BYTE_GET (external->p_flags);
      internal->p_align  = BYTE_GET (external->p_align);
    }

  free (phdrs);

  return 1;
}

/* Returns 1 if the program headers were read into `program_headers'.  */

static int
get_program_headers (FILE * file)
{
  Elf_Internal_Phdr * phdrs;

  /* Check cache of prior read.  */
  if (program_headers != NULL)
    return 1;

  phdrs = (Elf_Internal_Phdr *) cmalloc (elf_header.e_phnum,
                                         sizeof (Elf_Internal_Phdr));

  if (phdrs == NULL)
    {
      error (_("Out of memory\n"));
      return 0;
    }

  if (get_32bit_program_headers (file, phdrs))
    {
      program_headers = phdrs;
      return 1;
    }

  free (phdrs);
  return 0;
}

/* Returns 1 if the program headers were loaded.  */

static int
process_program_headers (FILE * file)
{
  Elf_Internal_Phdr * segment;
  unsigned int i;

  if (elf_header.e_phnum == 0)
    {
      /* PR binutils/12467.  */
      if (elf_header.e_phoff != 0)
	warn (_("possibly corrupt ELF header - it has a non-zero program"
		" header offset, but no program headers"));
      else if (do_segments)
	printf (_("\nThere are no program headers in this file.\n"));
      return 0;
    }

  if (do_segments && !do_header)
    {
      printf (_("\nElf file type is %s\n"), get_file_type (elf_header.e_type));
      printf (_("Entry point "));
      print_vma ((bfd_vma) elf_header.e_entry, PREFIX_HEX);
      printf (_("\nThere are %d program headers, starting at offset "),
	      elf_header.e_phnum);
      print_vma ((bfd_vma) elf_header.e_phoff, DEC);
      printf ("\n");
    }

  if (! get_program_headers (file))
      return 0;

  if (do_segments)
    {
      if (elf_header.e_phnum > 1)
	printf (_("\nProgram Headers:\n"));
      else
	printf (_("\nProgram Headers:\n"));

      printf
	(_("  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align\n"));
    }

  dynamic_addr = 0;
  dynamic_size = 0;

  for (i = 0, segment = program_headers;
       i < elf_header.e_phnum;
       i++, segment++)
    {
      if (do_segments)
	{
	  printf ("  %-14.14s ", get_segment_type (segment->p_type));

	  printf ("0x%6.6lx ", (unsigned long) segment->p_offset);
	  printf ("0x%8.8lx ", (unsigned long) segment->p_vaddr);
	  printf ("0x%8.8lx ", (unsigned long) segment->p_paddr);
	  printf ("0x%5.5lx ", (unsigned long) segment->p_filesz);
	  printf ("0x%5.5lx ", (unsigned long) segment->p_memsz);
	  printf ("%c%c%c ",
		  (segment->p_flags & PF_R ? 'R' : ' '),
		  (segment->p_flags & PF_W ? 'W' : ' '),
		  (segment->p_flags & PF_X ? 'E' : ' '));
	  printf ("%#lx", (unsigned long) segment->p_align);
	}

      switch (segment->p_type)
	{
	case PT_DYNAMIC:
	  if (dynamic_addr)
	    error (_("more than one dynamic segment\n"));

	  /* By default, assume that the .dynamic section is the first
	     section in the DYNAMIC segment.  */
	  dynamic_addr = segment->p_offset;
	  dynamic_size = segment->p_filesz;

	  /* Try to locate the .dynamic section. If there is
	     a section header table, we can easily locate it.  */
	  if (section_headers != NULL)
	    {
	      Elf_Internal_Shdr * sec;

	      sec = find_section (".dynamic");
	      if (sec == NULL || sec->sh_size == 0)
		{
                  error (_("no .dynamic section in the dynamic segment\n"));
		  break;
		}

	      if (sec->sh_type == SHT_NOBITS)
		{
		  dynamic_size = 0;
		  break;
		}

	      dynamic_addr = sec->sh_offset;
	      dynamic_size = sec->sh_size;

	      if (dynamic_addr < segment->p_offset
		  || dynamic_addr > segment->p_offset + segment->p_filesz)
		warn (_("the .dynamic section is not contained"
			" within the dynamic segment\n"));
	      else if (dynamic_addr > segment->p_offset)
		warn (_("the .dynamic section is not the first section"
			" in the dynamic segment.\n"));
	    }
	  break;

	case PT_INTERP:
	  if (fseek (file, archive_file_offset + (long) segment->p_offset,
		     SEEK_SET))
	    error (_("Unable to find program interpreter name\n"));
	  else
	    {
	      char fmt [32];
	      int ret = snprintf (fmt, sizeof (fmt), "%%%ds", PATH_MAX);

	      if (ret >= (int) sizeof (fmt) || ret < 0)
		error (_("Internal error: failed to create format string to display program interpreter\n"));

	      program_interpreter[0] = 0;
	      if (fscanf (file, fmt, program_interpreter) <= 0)
		error (_("Unable to read program interpreter name\n"));

	      if (do_segments)
		printf (_("\n      [Requesting program interpreter: %s]"),
		    program_interpreter);
	    }
	  break;
	}

      if (do_segments)
	putc ('\n', stdout);
    }

  if (do_segments && section_headers != NULL && string_table != NULL)
    {
      printf (_("\n Section to Segment mapping:\n"));
      printf (_("  Segment Sections...\n"));

      for (i = 0; i < elf_header.e_phnum; i++)
	{
	  unsigned int j;
	  Elf_Internal_Shdr * section;

	  segment = program_headers + i;
	  section = section_headers + 1;

	  printf ("   %2.2d     ", i);

	  for (j = 1; j < elf_header.e_shnum; j++, section++)
	    {
	      if (!ELF_TBSS_SPECIAL (section, segment)
		  && ELF_SECTION_IN_SEGMENT_STRICT (section, segment))
		printf ("%s ", SECTION_NAME (section));
	    }

	  putc ('\n',stdout);
	}
    }

  return 1;
}

/* Find the file offset corresponding to VMA by using the program headers.  */

static long
offset_from_vma (FILE * file, bfd_vma vma, bfd_size_type size)
{
  Elf_Internal_Phdr * seg;

  if (! get_program_headers (file))
    {
      warn (_("Cannot interpret virtual addresses without program headers.\n"));
      return (long) vma;
    }

  for (seg = program_headers;
       seg < program_headers + elf_header.e_phnum;
       ++seg)
    {
      if (seg->p_type != PT_LOAD)
	continue;

      if (vma >= (seg->p_vaddr & -seg->p_align)
	  && vma + size <= seg->p_vaddr + seg->p_filesz)
	return vma - seg->p_vaddr + seg->p_offset;
    }

  warn (_("Virtual address 0x%lx not located in any PT_LOAD segment.\n"),
	(unsigned long) vma);
  return (long) vma;
}

static int
get_32bit_section_headers (FILE * file, unsigned int num)
{
  Elf32_External_Shdr * shdrs;
  Elf_Internal_Shdr *   internal;
  unsigned int i;

  shdrs = (Elf32_External_Shdr *) get_data (NULL, file, elf_header.e_shoff,
                                            elf_header.e_shentsize, num,
                                            _("section headers"));
  if (!shdrs)
    return 0;

  section_headers = (Elf_Internal_Shdr *) cmalloc (num,
                                                   sizeof (Elf_Internal_Shdr));

  if (section_headers == NULL)
    {
      error (_("Out of memory\n"));
      return 0;
    }

  for (i = 0, internal = section_headers;
       i < num;
       i++, internal++)
    {
      internal->sh_name      = BYTE_GET (shdrs[i].sh_name);
      internal->sh_type      = BYTE_GET (shdrs[i].sh_type);
      internal->sh_flags     = BYTE_GET (shdrs[i].sh_flags);
      internal->sh_addr      = BYTE_GET (shdrs[i].sh_addr);
      internal->sh_offset    = BYTE_GET (shdrs[i].sh_offset);
      internal->sh_size      = BYTE_GET (shdrs[i].sh_size);
      internal->sh_link      = BYTE_GET (shdrs[i].sh_link);
      internal->sh_info      = BYTE_GET (shdrs[i].sh_info);
      internal->sh_addralign = BYTE_GET (shdrs[i].sh_addralign);
      internal->sh_entsize   = BYTE_GET (shdrs[i].sh_entsize);
    }

  free (shdrs);

  return 1;
}

static Elf_Internal_Sym *
get_32bit_elf_symbols (FILE * file,
		       Elf_Internal_Shdr * section,
		       unsigned long * num_syms_return)
{
  unsigned long number = 0;
  Elf32_External_Sym * esyms = NULL;
  Elf_External_Sym_Shndx * shndx = NULL;
  Elf_Internal_Sym * isyms = NULL;
  Elf_Internal_Sym * psym;
  unsigned int j;

  /* Run some sanity checks first.  */
  if (section->sh_entsize == 0)
    {
      error (_("sh_entsize is zero\n"));
      goto exit_point;
    }

  number = section->sh_size / section->sh_entsize;

  if (number * sizeof (Elf32_External_Sym) > section->sh_size + 1)
    {
      error (_("Invalid sh_entsize\n"));
      goto exit_point;
    }

  esyms = (Elf32_External_Sym *) get_data (NULL, file, section->sh_offset, 1,
                                           section->sh_size, _("symbols"));
  if (esyms == NULL)
    goto exit_point;

  shndx = NULL;
  if (symtab_shndx_hdr != NULL
      && (symtab_shndx_hdr->sh_link
	  == (unsigned long) (section - section_headers)))
    {
      shndx = (Elf_External_Sym_Shndx *) get_data (NULL, file,
                                                   symtab_shndx_hdr->sh_offset,
                                                   1, symtab_shndx_hdr->sh_size,
                                                   _("symbol table section indicies"));
      if (shndx == NULL)
	goto exit_point;
    }

  isyms = (Elf_Internal_Sym *) cmalloc (number, sizeof (Elf_Internal_Sym));

  if (isyms == NULL)
    {
      error (_("Out of memory\n"));
      goto exit_point;
    }

  for (j = 0, psym = isyms; j < number; j++, psym++)
    {
      psym->st_name  = BYTE_GET (esyms[j].st_name);
      psym->st_value = BYTE_GET (esyms[j].st_value);
      psym->st_size  = BYTE_GET (esyms[j].st_size);
      psym->st_shndx = BYTE_GET (esyms[j].st_shndx);
      if (psym->st_shndx == (SHN_XINDEX & 0xffff) && shndx != NULL)
	psym->st_shndx
	  = byte_get ((unsigned char *) &shndx[j], sizeof (shndx[j]));
      else if (psym->st_shndx >= (SHN_LORESERVE & 0xffff))
	psym->st_shndx += SHN_LORESERVE - (SHN_LORESERVE & 0xffff);
      psym->st_info  = BYTE_GET (esyms[j].st_info);
      psym->st_other = BYTE_GET (esyms[j].st_other);
    }

 exit_point:
  if (shndx != NULL)
    free (shndx);
  if (esyms != NULL)
    free (esyms);

  if (num_syms_return != NULL)
    * num_syms_return = isyms == NULL ? 0 : number;

  return isyms;
}

static const char *
get_elf_section_flags (bfd_vma sh_flags)
{
  static char buff[1024];
  char * p = buff;
  int field_size = is_32bit_elf ? 8 : 16;
  int sindex;
  int size = sizeof (buff) - (field_size + 4 + 1);
  bfd_vma os_flags = 0;
  bfd_vma proc_flags = 0;
  bfd_vma unknown_flags = 0;
  static const struct
    {
      const char * str;
      int len;
    }
  flags [] =
    {
      /*  0 */ { STRING_COMMA_LEN ("WRITE") },
      /*  1 */ { STRING_COMMA_LEN ("ALLOC") },
      /*  2 */ { STRING_COMMA_LEN ("EXEC") },
      /*  3 */ { STRING_COMMA_LEN ("MERGE") },
      /*  4 */ { STRING_COMMA_LEN ("STRINGS") },
      /*  5 */ { STRING_COMMA_LEN ("INFO LINK") },
      /*  6 */ { STRING_COMMA_LEN ("LINK ORDER") },
      /*  7 */ { STRING_COMMA_LEN ("OS NONCONF") },
      /*  8 */ { STRING_COMMA_LEN ("GROUP") },
      /*  9 */ { STRING_COMMA_LEN ("TLS") },
      /* Generic.  */
      /* 10 */ { STRING_COMMA_LEN ("EXCLUDE") }
    };

  if (do_section_details)
    {
      sprintf (buff, "[%*.*lx]: ",
	       field_size, field_size, (unsigned long) sh_flags);
      p += field_size + 4;
    }

  while (sh_flags)
    {
      bfd_vma flag;

      flag = sh_flags & - sh_flags;
      sh_flags &= ~ flag;

      if (do_section_details)
	{
	  switch (flag)
	    {
	    case SHF_WRITE:		sindex = 0; break;
	    case SHF_ALLOC:		sindex = 1; break;
	    case SHF_EXECINSTR:		sindex = 2; break;
	    case SHF_MERGE:		sindex = 3; break;
	    case SHF_STRINGS:		sindex = 4; break;
	    case SHF_INFO_LINK:		sindex = 5; break;
	    case SHF_LINK_ORDER:	sindex = 6; break;
	    case SHF_OS_NONCONFORMING:	sindex = 7; break;
	    case SHF_GROUP:		sindex = 8; break;
	    case SHF_TLS:		sindex = 9; break;
	    case SHF_EXCLUDE:		sindex = 10; break;

	    default:
	      sindex = -1;
	      break;
	    }

	  if (sindex != -1)
	    {
	      if (p != buff + field_size + 4)
		{
		  if (size < (10 + 2))
		    abort ();
		  size -= 2;
		  *p++ = ',';
		  *p++ = ' ';
		}

	      size -= flags [sindex].len;
	      p = stpcpy (p, flags [sindex].str);
	    }
	  else if (flag & SHF_MASKOS)
	    os_flags |= flag;
	  else if (flag & SHF_MASKPROC)
	    proc_flags |= flag;
	  else
	    unknown_flags |= flag;
	}
      else
	{
	  switch (flag)
	    {
	    case SHF_WRITE:		*p = 'W'; break;
	    case SHF_ALLOC:		*p = 'A'; break;
	    case SHF_EXECINSTR:		*p = 'X'; break;
	    case SHF_MERGE:		*p = 'M'; break;
	    case SHF_STRINGS:		*p = 'S'; break;
	    case SHF_INFO_LINK:		*p = 'I'; break;
	    case SHF_LINK_ORDER:	*p = 'L'; break;
	    case SHF_OS_NONCONFORMING:	*p = 'O'; break;
	    case SHF_GROUP:		*p = 'G'; break;
	    case SHF_TLS:		*p = 'T'; break;
	    case SHF_EXCLUDE:		*p = 'E'; break;

	    default:
	      if (flag & SHF_MASKOS)
		{
		  *p = 'o';
		  sh_flags &= ~ SHF_MASKOS;
		}
	      else if (flag & SHF_MASKPROC)
		{
		  *p = 'p';
		  sh_flags &= ~ SHF_MASKPROC;
		}
	      else
		*p = 'x';
	      break;
	    }
	  p++;
	}
    }

  if (do_section_details)
    {
      if (os_flags)
	{
	  size -= 5 + field_size;
	  if (p != buff + field_size + 4)
	    {
	      if (size < (2 + 1))
		abort ();
	      size -= 2;
	      *p++ = ',';
	      *p++ = ' ';
	    }
	  sprintf (p, "OS (%*.*lx)", field_size, field_size,
		   (unsigned long) os_flags);
	  p += 5 + field_size;
	}
      if (proc_flags)
	{
	  size -= 7 + field_size;
	  if (p != buff + field_size + 4)
	    {
	      if (size < (2 + 1))
		abort ();
	      size -= 2;
	      *p++ = ',';
	      *p++ = ' ';
	    }
	  sprintf (p, "PROC (%*.*lx)", field_size, field_size,
		   (unsigned long) proc_flags);
	  p += 7 + field_size;
	}
      if (unknown_flags)
	{
	  size -= 10 + field_size;
	  if (p != buff + field_size + 4)
	    {
	      if (size < (2 + 1))
		abort ();
	      size -= 2;
	      *p++ = ',';
	      *p++ = ' ';
	    }
	  sprintf (p, _("UNKNOWN (%*.*lx)"), field_size, field_size,
		   (unsigned long) unknown_flags);
	  p += 10 + field_size;
	}
    }

  *p = '\0';
  return buff;
}

static int
process_section_headers (FILE * file)
{
  Elf_Internal_Shdr * section;
  unsigned int i;

  section_headers = NULL;

  if (elf_header.e_shnum == 0)
    {
      /* PR binutils/12467.  */
      if (elf_header.e_shoff != 0)
	warn (_("possibly corrupt ELF file header - it has a non-zero"
		" section header offset, but no section headers\n"));
      else if (do_sections)
	printf (_("\nThere are no sections in this file.\n"));

      return 1;
    }

  if (do_sections && !do_header)
    printf (_("There are %d section headers, starting at offset 0x%lx:\n"),
	    elf_header.e_shnum, (unsigned long) elf_header.e_shoff);

  if (! get_32bit_section_headers (file, elf_header.e_shnum))
    return 0;

  /* Read in the string table, so that we have names to display.  */
  if (elf_header.e_shstrndx != SHN_UNDEF
       && elf_header.e_shstrndx < elf_header.e_shnum)
    {
      section = section_headers + elf_header.e_shstrndx;

      if (section->sh_size != 0)
	{
	  string_table = (char *) get_data (NULL, file, section->sh_offset,
                                            1, section->sh_size,
                                            _("string table"));

	  string_table_length = string_table != NULL ? section->sh_size : 0;
	}
    }

  dynamic_symbols = NULL;
  dynamic_strings = NULL;
  dynamic_syminfo = NULL;
  symtab_shndx_hdr = NULL;

#define CHECK_ENTSIZE(section, i, type)					\
  do									\
    {									\
      bfd_size_type expected_entsize = sizeof (Elf32_External_##type);	\
      if (section->sh_entsize != expected_entsize)			\
	{								\
	  error (_("Section %d has invalid sh_entsize of %" BFD_VMA_FMT "x\n"), \
		 i, section->sh_entsize);				\
	  error (_("(Using the expected size of %d for the rest of this dump)\n"), \
		 (int) expected_entsize);				\
	  section->sh_entsize = expected_entsize;			\
	}								\
    }									\
  while (0)

  for (i = 0, section = section_headers;
       i < elf_header.e_shnum;
       i++, section++)
    {
      char * name = SECTION_NAME (section);

      if (section->sh_type == SHT_DYNSYM)
	{
	  if (dynamic_symbols != NULL)
	    {
	      error (_("File contains multiple dynamic symbol tables\n"));
	      continue;
	    }

	  CHECK_ENTSIZE (section, i, Sym);
	  dynamic_symbols = get_32bit_elf_symbols (file, section, & num_dynamic_syms);
	}
      else if (section->sh_type == SHT_STRTAB
	       && streq (name, ".dynstr"))
	{
	  if (dynamic_strings != NULL)
	    {
	      error (_("File contains multiple dynamic string tables\n"));
	      continue;
	    }

	  dynamic_strings = (char *) get_data (NULL, file, section->sh_offset,
                                               1, section->sh_size,
                                               _("dynamic strings"));
	  dynamic_strings_length = dynamic_strings == NULL ? 0 : section->sh_size;
	}
      else if (section->sh_type == SHT_SYMTAB_SHNDX)
	{
	  if (symtab_shndx_hdr != NULL)
	    {
	      error (_("File contains multiple symtab shndx tables\n"));
	      continue;
	    }
	  symtab_shndx_hdr = section;
	}
      else if (section->sh_type == SHT_SYMTAB)
	CHECK_ENTSIZE (section, i, Sym);
      else if (section->sh_type == SHT_REL)
	CHECK_ENTSIZE (section, i, Rel);
      else if (section->sh_type == SHT_RELA)
	CHECK_ENTSIZE (section, i, Rela);
    }

  if (! do_sections)
    return 1;

  if (elf_header.e_shnum > 1)
    printf (_("\nSection Headers:\n"));
  else
    printf (_("\nSection Header:\n"));

  if (do_section_details)
    {
      printf (_("  [Nr] Name\n"));
      printf (_("       Type            Addr     Off    Size   ES   Lk Inf Al\n"));
    }
  else
    printf
      (_("  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al\n"));

  if (do_section_details)
    printf (_("       Flags\n"));

  for (i = 0, section = section_headers;
       i < elf_header.e_shnum;
       i++, section++)
    {
      printf ("  [%2u] ", i);
      if (do_section_details)
	{
	  print_symbol (INT_MAX, SECTION_NAME (section));
	  printf ("\n      ");
	}
      else
	{
	  print_symbol (-17, SECTION_NAME (section));
	}

      printf (" %-15s ", get_section_type_name (section->sh_type));

      print_vma (section->sh_addr, LONG_HEX);

      printf ( " %6.6lx %6.6lx %2.2lx",
	       (unsigned long) section->sh_offset,
	       (unsigned long) section->sh_size,
	       (unsigned long) section->sh_entsize);

      if (do_section_details)
	fputs ("  ", stdout);
      else
	printf (" %3s ", get_elf_section_flags (section->sh_flags));

      printf ("%2u %3u %2lu\n",
	      section->sh_link,
	      section->sh_info,
	      (unsigned long) section->sh_addralign);

      if (do_section_details)
	printf ("       %s\n", get_elf_section_flags (section->sh_flags));
    }

  if (!do_section_details)
    printf (_("Key to Flags:\n\
  W (write), A (alloc), X (execute), M (merge), S (strings)\n\
  I (info), L (link order), G (group), T (TLS), E (exclude), x (unknown)\n\
  O (extra OS processing required) o (OS specific), p (processor specific)\n"));

  return 1;
}

static struct
{
  const char * name;
  int reloc;
  int size;
  int rela;
} dynamic_relocations [] =
{
    { "REL", DT_REL, DT_RELSZ, FALSE },
    { "RELA", DT_RELA, DT_RELASZ, TRUE },
    { "PLT", DT_JMPREL, DT_PLTRELSZ, UNKNOWN }
};

/* Process the reloc section.  */

static int
process_relocs (FILE * file)
{
  unsigned long rel_size;
  unsigned long rel_offset;

  if (!do_reloc)
    return 1;

  if (do_using_dynamic)
    {
      int is_rela;
      const char * name;
      int has_dynamic_reloc;
      unsigned int i;

      has_dynamic_reloc = 0;

      for (i = 0; i < ARRAY_SIZE (dynamic_relocations); i++)
	{
	  is_rela = dynamic_relocations [i].rela;
	  name = dynamic_relocations [i].name;
	  rel_size = dynamic_info [dynamic_relocations [i].size];
	  rel_offset = dynamic_info [dynamic_relocations [i].reloc];

	  has_dynamic_reloc |= rel_size;

	  if (is_rela == UNKNOWN)
	    {
	      if (dynamic_relocations [i].reloc == DT_JMPREL)
		switch (dynamic_info[DT_PLTREL])
		  {
		  case DT_REL:
		    is_rela = FALSE;
		    break;
		  case DT_RELA:
		    is_rela = TRUE;
		    break;
		  }
	    }

	  if (rel_size)
	    {
	      printf
		(_("\n'%s' relocation section at offset 0x%lx contains %ld bytes:\n"),
		 name, rel_offset, rel_size);

	      dump_relocations (file,
				offset_from_vma (file, rel_offset, rel_size),
				rel_size,
				dynamic_symbols, num_dynamic_syms,
				dynamic_strings, dynamic_strings_length, is_rela);
	    }
	}

      if (! has_dynamic_reloc)
	printf (_("\nThere are no dynamic relocations in this file.\n"));
    }
  else
    {
      Elf_Internal_Shdr * section;
      unsigned long i;
      int found = 0;

      for (i = 0, section = section_headers;
	   i < elf_header.e_shnum;
	   i++, section++)
	{
	  if (   section->sh_type != SHT_RELA
	      && section->sh_type != SHT_REL)
	    continue;

	  rel_offset = section->sh_offset;
	  rel_size   = section->sh_size;

	  if (rel_size)
	    {
	      Elf_Internal_Shdr * strsec;
	      int is_rela;

	      printf (_("\nRelocation section "));

	      if (string_table == NULL)
		printf ("%d", section->sh_name);
	      else
		printf ("'%s'", SECTION_NAME (section));

	      printf (_(" at offset 0x%lx contains %lu entries:\n"),
		 rel_offset, (unsigned long) (rel_size / section->sh_entsize));

	      is_rela = section->sh_type == SHT_RELA;

	      if (section->sh_link != 0
		  && section->sh_link < elf_header.e_shnum)
		{
		  Elf_Internal_Shdr * symsec;
		  Elf_Internal_Sym *  symtab;
		  unsigned long nsyms;
		  unsigned long strtablen = 0;
		  char * strtab = NULL;

		  symsec = section_headers + section->sh_link;
		  if (symsec->sh_type != SHT_SYMTAB
		      && symsec->sh_type != SHT_DYNSYM)
                    continue;

		  symtab = get_32bit_elf_symbols (file, symsec, & nsyms);

		  if (symtab == NULL)
		    continue;

		  if (symsec->sh_link != 0
		      && symsec->sh_link < elf_header.e_shnum)
		    {
		      strsec = section_headers + symsec->sh_link;

		      strtab = (char *) get_data (NULL, file, strsec->sh_offset,
                                                  1, strsec->sh_size,
                                                  _("string table"));
		      strtablen = strtab == NULL ? 0 : strsec->sh_size;
		    }

		  dump_relocations (file, rel_offset, rel_size,
				    symtab, nsyms, strtab, strtablen, is_rela);
		  if (strtab)
		    free (strtab);
		  free (symtab);
		}
	      else
		dump_relocations (file, rel_offset, rel_size,
				  NULL, 0, NULL, 0, is_rela);

	      found = 1;
	    }
	}

      if (! found)
	printf (_("\nThere are no relocations in this file.\n"));
    }

  return 1;
}

static int
get_32bit_dynamic_section (FILE * file)
{
  Elf32_External_Dyn * edyn;
  Elf32_External_Dyn * ext;
  Elf_Internal_Dyn * entry;

  edyn = (Elf32_External_Dyn *) get_data (NULL, file, dynamic_addr, 1,
                                          dynamic_size, _("dynamic section"));
  if (!edyn)
    return 0;

/* SGI's ELF has more than one section in the DYNAMIC segment, and we
   might not have the luxury of section headers.  Look for the DT_NULL
   terminator to determine the number of entries.  */
  for (ext = edyn, dynamic_nent = 0;
       (char *) ext < (char *) edyn + dynamic_size;
       ext++)
    {
      dynamic_nent++;
      if (BYTE_GET (ext->d_tag) == DT_NULL)
	break;
    }

  dynamic_section = (Elf_Internal_Dyn *) cmalloc (dynamic_nent,
                                                  sizeof (* entry));
  if (dynamic_section == NULL)
    {
      error (_("Out of memory\n"));
      free (edyn);
      return 0;
    }

  for (ext = edyn, entry = dynamic_section;
       entry < dynamic_section + dynamic_nent;
       ext++, entry++)
    {
      entry->d_tag      = BYTE_GET (ext->d_tag);
      entry->d_un.d_val = BYTE_GET (ext->d_un.d_val);
    }

  free (edyn);

  return 1;
}

static void
print_dynamic_flags (bfd_vma flags)
{
  int first = 1;

  while (flags)
    {
      bfd_vma flag;

      flag = flags & - flags;
      flags &= ~ flag;

      if (first)
	first = 0;
      else
	putc (' ', stdout);

      switch (flag)
	{
	case DF_ORIGIN:		fputs ("ORIGIN", stdout); break;
	case DF_SYMBOLIC:	fputs ("SYMBOLIC", stdout); break;
	case DF_TEXTREL:	fputs ("TEXTREL", stdout); break;
	case DF_BIND_NOW:	fputs ("BIND_NOW", stdout); break;
	case DF_STATIC_TLS:	fputs ("STATIC_TLS", stdout); break;
	default:		fputs (_("unknown"), stdout); break;
	}
    }
  puts ("");
}

/* Parse and display the contents of the dynamic section.  */

static int
process_dynamic_section (FILE * file)
{
  Elf_Internal_Dyn * entry;

  if (dynamic_size == 0)
    {
      if (do_dynamic)
	printf (_("\nThere is no dynamic section in this file.\n"));

      return 1;
    }

  if (! get_32bit_dynamic_section (file))
    return 0;

  /* Find the appropriate symbol table.  */
  if (dynamic_symbols == NULL)
    {
      for (entry = dynamic_section;
	   entry < dynamic_section + dynamic_nent;
	   ++entry)
	{
	  Elf_Internal_Shdr section;

	  if (entry->d_tag != DT_SYMTAB)
	    continue;

	  dynamic_info[DT_SYMTAB] = entry->d_un.d_val;

	  /* Since we do not know how big the symbol table is,
	     we default to reading in the entire file (!) and
	     processing that.  This is overkill, I know, but it
	     should work.  */
	  section.sh_offset = offset_from_vma (file, entry->d_un.d_val, 0);

	  if (archive_file_offset != 0)
	    section.sh_size = archive_file_size - section.sh_offset;
	  else
	    {
	      if (fseek (file, 0, SEEK_END))
		error (_("Unable to seek to end of file!\n"));

	      section.sh_size = ftell (file) - section.sh_offset;
	    }

	  section.sh_entsize = sizeof (Elf32_External_Sym);

	  dynamic_symbols = get_32bit_elf_symbols (file, &section, & num_dynamic_syms);
	  if (num_dynamic_syms < 1)
	    {
	      error (_("Unable to determine the number of symbols to load\n"));
	      continue;
	    }
	}
    }

  /* Similarly find a string table.  */
  if (dynamic_strings == NULL)
    {
      for (entry = dynamic_section;
	   entry < dynamic_section + dynamic_nent;
	   ++entry)
	{
	  unsigned long offset;
	  long str_tab_len;

	  if (entry->d_tag != DT_STRTAB)
	    continue;

	  dynamic_info[DT_STRTAB] = entry->d_un.d_val;

	  /* Since we do not know how big the string table is,
	     we default to reading in the entire file (!) and
	     processing that.  This is overkill, I know, but it
	     should work.  */

	  offset = offset_from_vma (file, entry->d_un.d_val, 0);

	  if (archive_file_offset != 0)
	    str_tab_len = archive_file_size - offset;
	  else
	    {
	      if (fseek (file, 0, SEEK_END))
		error (_("Unable to seek to end of file\n"));
	      str_tab_len = ftell (file) - offset;
	    }

	  if (str_tab_len < 1)
	    {
	      error
		(_("Unable to determine the length of the dynamic string table\n"));
	      continue;
	    }

	  dynamic_strings = (char *) get_data (NULL, file, offset, 1,
                                               str_tab_len,
                                               _("dynamic string table"));
	  dynamic_strings_length = dynamic_strings == NULL ? 0 : str_tab_len;
	  break;
	}
    }

  /* And find the syminfo section if available.  */
  if (dynamic_syminfo == NULL)
    {
      unsigned long syminsz = 0;

      for (entry = dynamic_section;
	   entry < dynamic_section + dynamic_nent;
	   ++entry)
	{
	  if (entry->d_tag == DT_SYMINENT)
	    {
	      /* Note: these braces are necessary to avoid a syntax
		 error from the SunOS4 C compiler.  */
	      assert (sizeof (Elf_External_Syminfo) == entry->d_un.d_val);
	    }
	  else if (entry->d_tag == DT_SYMINSZ)
	    syminsz = entry->d_un.d_val;
	  else if (entry->d_tag == DT_SYMINFO)
	    dynamic_syminfo_offset = offset_from_vma (file, entry->d_un.d_val,
						      syminsz);
	}

      if (dynamic_syminfo_offset != 0 && syminsz != 0)
	{
	  Elf_External_Syminfo * extsyminfo;
	  Elf_External_Syminfo * extsym;
	  Elf_Internal_Syminfo * syminfo;

	  /* There is a syminfo section.  Read the data.  */
	  extsyminfo = (Elf_External_Syminfo *)
              get_data (NULL, file, dynamic_syminfo_offset, 1, syminsz,
                        _("symbol information"));
	  if (!extsyminfo)
	    return 0;

	  dynamic_syminfo = (Elf_Internal_Syminfo *) malloc (syminsz);
	  if (dynamic_syminfo == NULL)
	    {
	      error (_("Out of memory\n"));
	      return 0;
	    }

	  dynamic_syminfo_nent = syminsz / sizeof (Elf_External_Syminfo);
	  for (syminfo = dynamic_syminfo, extsym = extsyminfo;
	       syminfo < dynamic_syminfo + dynamic_syminfo_nent;
	       ++syminfo, ++extsym)
	    {
	      syminfo->si_boundto = BYTE_GET (extsym->si_boundto);
	      syminfo->si_flags = BYTE_GET (extsym->si_flags);
	    }

	  free (extsyminfo);
	}
    }

  if (do_dynamic && dynamic_addr)
    printf (_("\nDynamic section at offset 0x%lx contains %u entries:\n"),
	    dynamic_addr, dynamic_nent);
  if (do_dynamic)
    printf (_("  Tag        Type                         Name/Value\n"));

  for (entry = dynamic_section;
       entry < dynamic_section + dynamic_nent;
       entry++)
    {
      if (do_dynamic)
	{
	  const char * dtype;

	  putchar (' ');
	  print_vma (entry->d_tag, FULL_HEX);
	  dtype = get_dynamic_type (entry->d_tag);
	  printf (" (%s)%*s", dtype,
		  (27 - (int) strlen (dtype)),
		  " ");
	}

      switch (entry->d_tag)
	{
	case DT_FLAGS:
	  if (do_dynamic)
	    print_dynamic_flags (entry->d_un.d_val);
	  break;

	case DT_AUXILIARY:
	case DT_FILTER:
	case DT_CONFIG:
	case DT_DEPAUDIT:
	case DT_AUDIT:
	  if (do_dynamic)
	    {
	      switch (entry->d_tag)
		{
		case DT_AUXILIARY:
		  printf (_("Auxiliary library"));
		  break;

		case DT_FILTER:
		  printf (_("Filter library"));
		  break;

		case DT_CONFIG:
		  printf (_("Configuration file"));
		  break;

		case DT_DEPAUDIT:
		  printf (_("Dependency audit library"));
		  break;

		case DT_AUDIT:
		  printf (_("Audit library"));
		  break;
		}

	      if (VALID_DYNAMIC_NAME (entry->d_un.d_val))
		printf (": [%s]\n", GET_DYNAMIC_NAME (entry->d_un.d_val));
	      else
		{
		  printf (": ");
		  print_vma (entry->d_un.d_val, PREFIX_HEX);
		  putchar ('\n');
		}
	    }
	  break;

	case DT_FEATURE:
	  if (do_dynamic)
	    {
	      printf (_("Flags:"));

	      if (entry->d_un.d_val == 0)
		printf (_(" None\n"));
	      else
		{
		  unsigned long int val = entry->d_un.d_val;

		  if (val & DTF_1_PARINIT)
		    {
		      printf (" PARINIT");
		      val ^= DTF_1_PARINIT;
		    }
		  if (val & DTF_1_CONFEXP)
		    {
		      printf (" CONFEXP");
		      val ^= DTF_1_CONFEXP;
		    }
		  if (val != 0)
		    printf (" %lx", val);
		  puts ("");
		}
	    }
	  break;

	case DT_POSFLAG_1:
	  if (do_dynamic)
	    {
	      printf (_("Flags:"));

	      if (entry->d_un.d_val == 0)
		printf (_(" None\n"));
	      else
		{
		  unsigned long int val = entry->d_un.d_val;

		  if (val & DF_P1_LAZYLOAD)
		    {
		      printf (" LAZYLOAD");
		      val ^= DF_P1_LAZYLOAD;
		    }
		  if (val & DF_P1_GROUPPERM)
		    {
		      printf (" GROUPPERM");
		      val ^= DF_P1_GROUPPERM;
		    }
		  if (val != 0)
		    printf (" %lx", val);
		  puts ("");
		}
	    }
	  break;

	case DT_FLAGS_1:
	  if (do_dynamic)
	    {
	      printf (_("Flags:"));
	      if (entry->d_un.d_val == 0)
		printf (_(" None\n"));
	      else
		{
		  unsigned long int val = entry->d_un.d_val;

		  if (val & DF_1_NOW)
		    {
		      printf (" NOW");
		      val ^= DF_1_NOW;
		    }
		  if (val & DF_1_GLOBAL)
		    {
		      printf (" GLOBAL");
		      val ^= DF_1_GLOBAL;
		    }
		  if (val & DF_1_GROUP)
		    {
		      printf (" GROUP");
		      val ^= DF_1_GROUP;
		    }
		  if (val & DF_1_NODELETE)
		    {
		      printf (" NODELETE");
		      val ^= DF_1_NODELETE;
		    }
		  if (val & DF_1_LOADFLTR)
		    {
		      printf (" LOADFLTR");
		      val ^= DF_1_LOADFLTR;
		    }
		  if (val & DF_1_INITFIRST)
		    {
		      printf (" INITFIRST");
		      val ^= DF_1_INITFIRST;
		    }
		  if (val & DF_1_NOOPEN)
		    {
		      printf (" NOOPEN");
		      val ^= DF_1_NOOPEN;
		    }
		  if (val & DF_1_ORIGIN)
		    {
		      printf (" ORIGIN");
		      val ^= DF_1_ORIGIN;
		    }
		  if (val & DF_1_DIRECT)
		    {
		      printf (" DIRECT");
		      val ^= DF_1_DIRECT;
		    }
		  if (val & DF_1_TRANS)
		    {
		      printf (" TRANS");
		      val ^= DF_1_TRANS;
		    }
		  if (val & DF_1_INTERPOSE)
		    {
		      printf (" INTERPOSE");
		      val ^= DF_1_INTERPOSE;
		    }
		  if (val & DF_1_NODEFLIB)
		    {
		      printf (" NODEFLIB");
		      val ^= DF_1_NODEFLIB;
		    }
		  if (val & DF_1_NODUMP)
		    {
		      printf (" NODUMP");
		      val ^= DF_1_NODUMP;
		    }
		  if (val & DF_1_CONFALT)
		    {
		      printf (" CONFALT");
		      val ^= DF_1_CONFALT;
		    }
		  if (val & DF_1_ENDFILTEE)
		    {
		      printf (" ENDFILTEE");
		      val ^= DF_1_ENDFILTEE;
		    }
		  if (val & DF_1_DISPRELDNE)
		    {
		      printf (" DISPRELDNE");
		      val ^= DF_1_DISPRELDNE;
		    }
		  if (val & DF_1_DISPRELPND)
		    {
		      printf (" DISPRELPND");
		      val ^= DF_1_DISPRELPND;
		    }
		  if (val & DF_1_NODIRECT)
		    {
		      printf (" NODIRECT");
		      val ^= DF_1_NODIRECT;
		    }
		  if (val & DF_1_IGNMULDEF)
		    {
		      printf (" IGNMULDEF");
		      val ^= DF_1_IGNMULDEF;
		    }
		  if (val & DF_1_NOKSYMS)
		    {
		      printf (" NOKSYMS");
		      val ^= DF_1_NOKSYMS;
		    }
		  if (val & DF_1_NOHDR)
		    {
		      printf (" NOHDR");
		      val ^= DF_1_NOHDR;
		    }
		  if (val & DF_1_EDITED)
		    {
		      printf (" EDITED");
		      val ^= DF_1_EDITED;
		    }
		  if (val & DF_1_NORELOC)
		    {
		      printf (" NORELOC");
		      val ^= DF_1_NORELOC;
		    }
		  if (val & DF_1_SYMINTPOSE)
		    {
		      printf (" SYMINTPOSE");
		      val ^= DF_1_SYMINTPOSE;
		    }
		  if (val & DF_1_GLOBAUDIT)
		    {
		      printf (" GLOBAUDIT");
		      val ^= DF_1_GLOBAUDIT;
		    }
		  if (val & DF_1_SINGLETON)
		    {
		      printf (" SINGLETON");
		      val ^= DF_1_SINGLETON;
		    }
		  if (val != 0)
		    printf (" %lx", val);
		  puts ("");
		}
	    }
	  break;

	case DT_PLTREL:
	  dynamic_info[entry->d_tag] = entry->d_un.d_val;
	  if (do_dynamic)
	    puts (get_dynamic_type (entry->d_un.d_val));
	  break;

	case DT_NULL	:
	case DT_NEEDED	:
	case DT_PLTGOT	:
	case DT_HASH	:
	case DT_STRTAB	:
	case DT_SYMTAB	:
	case DT_RELA	:
	case DT_INIT	:
	case DT_FINI	:
	case DT_SONAME	:
	case DT_RPATH	:
	case DT_SYMBOLIC:
	case DT_REL	:
	case DT_DEBUG	:
	case DT_TEXTREL	:
	case DT_JMPREL	:
	case DT_RUNPATH	:
	  dynamic_info[entry->d_tag] = entry->d_un.d_val;

	  if (do_dynamic)
	    {
	      char * name;

	      if (VALID_DYNAMIC_NAME (entry->d_un.d_val))
		name = GET_DYNAMIC_NAME (entry->d_un.d_val);
	      else
		name = NULL;

	      if (name)
		{
		  switch (entry->d_tag)
		    {
		    case DT_NEEDED:
		      printf (_("Shared library: [%s]"), name);

		      if (streq (name, program_interpreter))
			printf (_(" program interpreter"));
		      break;

		    case DT_SONAME:
		      printf (_("Library soname: [%s]"), name);
		      break;

		    case DT_RPATH:
		      printf (_("Library rpath: [%s]"), name);
		      break;

		    case DT_RUNPATH:
		      printf (_("Library runpath: [%s]"), name);
		      break;

		    default:
		      print_vma (entry->d_un.d_val, PREFIX_HEX);
		      break;
		    }
		}
	      else
		print_vma (entry->d_un.d_val, PREFIX_HEX);

	      putchar ('\n');
	    }
	  break;

	case DT_PLTRELSZ:
	case DT_RELASZ	:
	case DT_STRSZ	:
	case DT_RELSZ	:
	case DT_RELAENT	:
	case DT_SYMENT	:
	case DT_RELENT	:
	  dynamic_info[entry->d_tag] = entry->d_un.d_val;
	case DT_PLTPADSZ:
	case DT_MOVEENT	:
	case DT_MOVESZ	:
	case DT_INIT_ARRAYSZ:
	case DT_FINI_ARRAYSZ:
	case DT_GNU_CONFLICTSZ:
	case DT_GNU_LIBLISTSZ:
	  if (do_dynamic)
	    {
	      print_vma (entry->d_un.d_val, UNSIGNED);
	      printf (_(" (bytes)\n"));
	    }
	  break;

	case DT_VERDEFNUM:
	case DT_VERNEEDNUM:
	case DT_RELACOUNT:
	case DT_RELCOUNT:
	  if (do_dynamic)
	    {
	      print_vma (entry->d_un.d_val, UNSIGNED);
	      putchar ('\n');
	    }
	  break;

	case DT_SYMINSZ:
	case DT_SYMINENT:
	case DT_SYMINFO:
	case DT_USED:
	case DT_INIT_ARRAY:
	case DT_FINI_ARRAY:
	  if (do_dynamic)
	    {
	      if (entry->d_tag == DT_USED
		  && VALID_DYNAMIC_NAME (entry->d_un.d_val))
		{
		  char * name = GET_DYNAMIC_NAME (entry->d_un.d_val);

		  if (*name)
		    {
		      printf (_("Not needed object: [%s]\n"), name);
		      break;
		    }
		}

	      print_vma (entry->d_un.d_val, PREFIX_HEX);
	      putchar ('\n');
	    }
	  break;

	case DT_BIND_NOW:
	  /* The value of this entry is ignored.  */
	  if (do_dynamic)
	    putchar ('\n');
	  break;

	case DT_GNU_PRELINKED:
	  if (do_dynamic)
	    {
	      struct tm * tmp;
	      time_t atime = entry->d_un.d_val;

	      tmp = gmtime (&atime);
	      printf ("%04u-%02u-%02uT%02u:%02u:%02u\n",
		      tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday,
		      tmp->tm_hour, tmp->tm_min, tmp->tm_sec);

	    }
	  break;

	case DT_GNU_HASH:
	  dynamic_info_DT_GNU_HASH = entry->d_un.d_val;
	  if (do_dynamic)
	    {
	      print_vma (entry->d_un.d_val, PREFIX_HEX);
	      putchar ('\n');
	    }
	  break;

	default:
	  if (do_dynamic)
	    {
	      print_vma (entry->d_un.d_val, PREFIX_HEX);
	      putchar ('\n');
	    }
	  break;
	}
    }

  return 1;
}

static const char *
get_symbol_binding (unsigned int binding)
{
  static char buff[32];

  switch (binding)
    {
    case STB_LOCAL:	return "LOCAL";
    case STB_GLOBAL:	return "GLOBAL";
    case STB_WEAK:	return "WEAK";
    default:
      if (binding >= STB_LOPROC && binding <= STB_HIPROC)
	snprintf (buff, sizeof (buff), _("<processor specific>: %d"),
		  binding);
      else if (binding >= STB_LOOS && binding <= STB_HIOS)
	{
	  if (binding == STB_GNU_UNIQUE
	      && (elf_header.e_ident[EI_OSABI] == ELFOSABI_GNU
		  /* GNU is still using the default value 0.  */
		  || elf_header.e_ident[EI_OSABI] == ELFOSABI_NONE))
	    return "UNIQUE";
	  snprintf (buff, sizeof (buff), _("<OS specific>: %d"), binding);
	}
      else
	snprintf (buff, sizeof (buff), _("<unknown>: %d"), binding);
      return buff;
    }
}

static const char *
get_symbol_type (unsigned int type)
{
  static char buff[32];

  switch (type)
    {
    case STT_NOTYPE:	return "NOTYPE";
    case STT_OBJECT:	return "OBJECT";
    case STT_FUNC:	return "FUNC";
    case STT_SECTION:	return "SECTION";
    case STT_FILE:	return "FILE";
    case STT_COMMON:	return "COMMON";
    case STT_TLS:	return "TLS";
    case STT_RELC:      return "RELC";
    case STT_SRELC:     return "SRELC";
    default:
      if (type >= STT_LOPROC && type <= STT_HIPROC)
	{
	  if (elf_header.e_machine == EM_ARM)
	    {
	      if (type == STT_ARM_TFUNC)
		return "THUMB_FUNC";
	      if (type == STT_ARM_16BIT)
		return "THUMB_LABEL";
	    }

	  snprintf (buff, sizeof (buff), _("<processor specific>: %d"), type);
	}
      else if (type >= STT_LOOS && type <= STT_HIOS)
	{
	  if (type == STT_GNU_IFUNC
	      && (elf_header.e_ident[EI_OSABI] == ELFOSABI_GNU
		  || elf_header.e_ident[EI_OSABI] == ELFOSABI_FREEBSD
		  /* GNU is still using the default value 0.  */
		  || elf_header.e_ident[EI_OSABI] == ELFOSABI_NONE))
	    return "IFUNC";

	  snprintf (buff, sizeof (buff), _("<OS specific>: %d"), type);
	}
      else
	snprintf (buff, sizeof (buff), _("<unknown>: %d"), type);
      return buff;
    }
}

static const char *
get_symbol_visibility (unsigned int visibility)
{
  switch (visibility)
    {
    case STV_DEFAULT:	return "DEFAULT";
    case STV_INTERNAL:	return "INTERNAL";
    case STV_HIDDEN:	return "HIDDEN";
    case STV_PROTECTED: return "PROTECTED";
    default: abort ();
    }
}

static const char *
get_symbol_other (unsigned int other)
{
  static char buff [32];

  if (other == 0)
    return "";

  snprintf (buff, sizeof buff, _("<other>: %x"), other);
  return buff;
}

static const char *
get_symbol_index_type (unsigned int type)
{
  static char buff[32];

  switch (type)
    {
    case SHN_UNDEF:	return "UND";
    case SHN_ABS:	return "ABS";
    case SHN_COMMON:	return "COM";
    default:
      if (type >= SHN_LOPROC && type <= SHN_HIPROC)
	sprintf (buff, "PRC[0x%04x]", type & 0xffff);
      else if (type >= SHN_LOOS && type <= SHN_HIOS)
	sprintf (buff, "OS [0x%04x]", type & 0xffff);
      else if (type >= SHN_LORESERVE)
	sprintf (buff, "RSV[0x%04x]", type & 0xffff);
      else if (type >= elf_header.e_shnum)
	sprintf (buff, "bad section index[%3d]", type);
      else
	sprintf (buff, "%3d", type);
      break;
    }

  return buff;
}

static bfd_vma *
get_dynamic_data (FILE * file, unsigned int number, unsigned int ent_size)
{
  unsigned char * e_data;
  bfd_vma * i_data;

  e_data = (unsigned char *) cmalloc (number, ent_size);

  if (e_data == NULL)
    {
      error (_("Out of memory\n"));
      return NULL;
    }

  if (fread (e_data, ent_size, number, file) != number)
    {
      error (_("Unable to read in dynamic data\n"));
      return NULL;
    }

  i_data = (bfd_vma *) cmalloc (number, sizeof (*i_data));

  if (i_data == NULL)
    {
      error (_("Out of memory\n"));
      free (e_data);
      return NULL;
    }

  while (number--)
    i_data[number] = byte_get (e_data + number * ent_size, ent_size);

  free (e_data);

  return i_data;
}

static void
print_dynamic_symbol (bfd_vma si, unsigned long hn)
{
  Elf_Internal_Sym * psym;
  int n;

  psym = dynamic_symbols + si;

  n = print_vma (si, DEC_5);
  if (n < 5)
    fputs (&"     "[n], stdout);
  printf (" %3lu: ", hn);
  print_vma (psym->st_value, LONG_HEX);
  putchar (' ');
  print_vma (psym->st_size, DEC_5);

  printf (" %-7s", get_symbol_type (ELF_ST_TYPE (psym->st_info)));
  printf (" %-6s",  get_symbol_binding (ELF_ST_BIND (psym->st_info)));
  printf (" %-7s",  get_symbol_visibility (ELF_ST_VISIBILITY (psym->st_other)));
  if (psym->st_other ^ ELF_ST_VISIBILITY (psym->st_other))
    printf (" [%s] ", get_symbol_other (psym->st_other ^ ELF_ST_VISIBILITY (psym->st_other)));
  printf (" %3.3s ", get_symbol_index_type (psym->st_shndx));
  if (VALID_DYNAMIC_NAME (psym->st_name))
    print_symbol (25, GET_DYNAMIC_NAME (psym->st_name));
  else
    printf (_(" <corrupt: %14ld>"), psym->st_name);
  putchar ('\n');
}

/* Dump the symbol table.  */
static int
process_symbol_table (FILE * file)
{
  Elf_Internal_Shdr * section;
  bfd_vma nbuckets = 0;
  bfd_vma nchains = 0;
  bfd_vma * buckets = NULL;
  bfd_vma * chains = NULL;
  bfd_vma ngnubuckets = 0;
  bfd_vma * gnubuckets = NULL;
  bfd_vma * gnuchains = NULL;
  bfd_vma gnusymidx = 0;

  if (!do_syms && !do_dyn_syms)
    return 1;

  if (dynamic_info[DT_HASH]
      && do_using_dynamic
      && dynamic_strings != NULL)
    {
      unsigned char nb[8];
      unsigned char nc[8];

      if (fseek (file,
		 (archive_file_offset
		  + offset_from_vma (file, dynamic_info[DT_HASH],
				     sizeof nb + sizeof nc)),
		 SEEK_SET))
	{
	  error (_("Unable to seek to start of dynamic information\n"));
	  goto no_hash;
	}

      if (fread (nb, 4, 1, file) != 1)
	{
	  error (_("Failed to read in number of buckets\n"));
	  goto no_hash;
	}

      if (fread (nc, 4, 1, file) != 1)
	{
	  error (_("Failed to read in number of chains\n"));
	  goto no_hash;
	}

      nbuckets = byte_get (nb, 4);
      nchains  = byte_get (nc, 4);

      buckets = get_dynamic_data (file, nbuckets, 4);
      chains  = get_dynamic_data (file, nchains, 4);

    no_hash:
      if (buckets == NULL || chains == NULL)
	{
	  if (do_using_dynamic)
	    return 0;
	  free (buckets);
	  free (chains);
	  buckets = NULL;
	  chains = NULL;
	  nbuckets = 0;
	  nchains = 0;
	}
    }

  if (dynamic_info_DT_GNU_HASH
      && (do_using_dynamic
	  && dynamic_strings != NULL))
    {
      unsigned char nb[16];
      bfd_vma i, maxchain = 0xffffffff, bitmaskwords;
      bfd_vma buckets_vma;

      if (fseek (file,
		 (archive_file_offset
		  + offset_from_vma (file, dynamic_info_DT_GNU_HASH,
				     sizeof nb)),
		 SEEK_SET))
	{
	  error (_("Unable to seek to start of dynamic information\n"));
	  goto no_gnu_hash;
	}

      if (fread (nb, 16, 1, file) != 1)
	{
	  error (_("Failed to read in number of buckets\n"));
	  goto no_gnu_hash;
	}

      ngnubuckets = byte_get (nb, 4);
      gnusymidx = byte_get (nb + 4, 4);
      bitmaskwords = byte_get (nb + 8, 4);
      buckets_vma = dynamic_info_DT_GNU_HASH + 16;
      buckets_vma += bitmaskwords * 4;

      if (fseek (file,
		 (archive_file_offset
		  + offset_from_vma (file, buckets_vma, 4)),
		 SEEK_SET))
	{
	  error (_("Unable to seek to start of dynamic information\n"));
	  goto no_gnu_hash;
	}

      gnubuckets = get_dynamic_data (file, ngnubuckets, 4);

      if (gnubuckets == NULL)
	goto no_gnu_hash;

      for (i = 0; i < ngnubuckets; i++)
	if (gnubuckets[i] != 0)
	  {
	    if (gnubuckets[i] < gnusymidx)
	      return 0;

	    if (maxchain == 0xffffffff || gnubuckets[i] > maxchain)
	      maxchain = gnubuckets[i];
	  }

      if (maxchain == 0xffffffff)
	goto no_gnu_hash;

      maxchain -= gnusymidx;

      if (fseek (file,
		 (archive_file_offset
		  + offset_from_vma (file, buckets_vma
					   + 4 * (ngnubuckets + maxchain), 4)),
		 SEEK_SET))
	{
	  error (_("Unable to seek to start of dynamic information\n"));
	  goto no_gnu_hash;
	}

      do
	{
	  if (fread (nb, 4, 1, file) != 1)
	    {
	      error (_("Failed to determine last chain length\n"));
	      goto no_gnu_hash;
	    }

	  if (maxchain + 1 == 0)
	    goto no_gnu_hash;

	  ++maxchain;
	}
      while ((byte_get (nb, 4) & 1) == 0);

      if (fseek (file,
		 (archive_file_offset
		  + offset_from_vma (file, buckets_vma + 4 * ngnubuckets, 4)),
		 SEEK_SET))
	{
	  error (_("Unable to seek to start of dynamic information\n"));
	  goto no_gnu_hash;
	}

      gnuchains = get_dynamic_data (file, maxchain, 4);

    no_gnu_hash:
      if (gnuchains == NULL)
	{
	  free (gnubuckets);
	  gnubuckets = NULL;
	  ngnubuckets = 0;
	  if (do_using_dynamic)
	    return 0;
	}
    }

  if ((dynamic_info[DT_HASH] || dynamic_info_DT_GNU_HASH)
      && do_syms
      && do_using_dynamic
      && dynamic_strings != NULL)
    {
      unsigned long hn;

      if (dynamic_info[DT_HASH])
	{
	  bfd_vma si;

	  printf (_("\nSymbol table for image:\n"));
	  printf (_("  Num Buc:    Value  Size   Type   Bind Vis      Ndx Name\n"));

	  for (hn = 0; hn < nbuckets; hn++)
	    {
	      if (! buckets[hn])
		continue;

	      for (si = buckets[hn]; si < nchains && si > 0; si = chains[si])
		print_dynamic_symbol (si, hn);
	    }
	}

      if (dynamic_info_DT_GNU_HASH)
	{
	  printf (_("\nSymbol table of `.gnu.hash' for image:\n"));
	  printf (_("  Num Buc:    Value  Size   Type   Bind Vis      Ndx Name\n"));

	  for (hn = 0; hn < ngnubuckets; ++hn)
	    if (gnubuckets[hn] != 0)
	      {
		bfd_vma si = gnubuckets[hn];
		bfd_vma off = si - gnusymidx;

		do
		  {
		    print_dynamic_symbol (si, hn);
		    si++;
		  }
		while ((gnuchains[off++] & 1) == 0);
	      }
	}
    }
  else if (do_dyn_syms || (do_syms && !do_using_dynamic))
    {
      unsigned int i;

      for (i = 0, section = section_headers;
	   i < elf_header.e_shnum;
	   i++, section++)
	{
	  unsigned int si;
	  char * strtab = NULL;
	  unsigned long int strtab_size = 0;
	  Elf_Internal_Sym * symtab;
	  Elf_Internal_Sym * psym;
	  unsigned long num_syms;

	  if ((section->sh_type != SHT_SYMTAB
	       && section->sh_type != SHT_DYNSYM)
	       || (!do_syms && section->sh_type == SHT_SYMTAB))
	    continue;

	  if (section->sh_entsize == 0)
	    {
	      printf (_("\nSymbol table '%s' has a sh_entsize of zero!\n"),
		      SECTION_NAME (section));
	      continue;
	    }

	  printf (_("\nSymbol table '%s' contains %lu entries:\n"),
		  SECTION_NAME (section),
		  (unsigned long) (section->sh_size / section->sh_entsize));

	  printf (_("   Num:    Value  Size Type    Bind   Vis      Ndx Name\n"));

	  symtab = get_32bit_elf_symbols (file, section, & num_syms);
	  if (symtab == NULL)
	    continue;

	  if (section->sh_link == elf_header.e_shstrndx)
	    {
	      strtab = string_table;
	      strtab_size = string_table_length;
	    }
	  else if (section->sh_link < elf_header.e_shnum)
	    {
	      Elf_Internal_Shdr * string_sec;

	      string_sec = section_headers + section->sh_link;

	      strtab = (char *) get_data (NULL, file, string_sec->sh_offset,
                                          1, string_sec->sh_size,
                                          _("string table"));
	      strtab_size = strtab != NULL ? string_sec->sh_size : 0;
	    }

	  for (si = 0, psym = symtab; si < num_syms; si++, psym++)
	    {
	      printf ("%6d: ", si);
	      print_vma (psym->st_value, LONG_HEX);
	      putchar (' ');
	      print_vma (psym->st_size, DEC_5);
	      printf (" %-7s", get_symbol_type (ELF_ST_TYPE (psym->st_info)));
	      printf (" %-6s", get_symbol_binding (ELF_ST_BIND (psym->st_info)));
	      printf (" %-7s", get_symbol_visibility (ELF_ST_VISIBILITY (psym->st_other)));
	      /* Check to see if any other bits in the st_other field are set.  */
	      if (psym->st_other ^ ELF_ST_VISIBILITY (psym->st_other))
		printf (" [%s] ", get_symbol_other (psym->st_other ^ ELF_ST_VISIBILITY (psym->st_other)));
	      printf (" %4s ", get_symbol_index_type (psym->st_shndx));
	      print_symbol (25, psym->st_name < strtab_size
			    ? strtab + psym->st_name : _("<corrupt>"));

	      putchar ('\n');
	    }

	  free (symtab);
	  if (strtab != string_table)
	    free (strtab);
	}
    }
  else if (do_syms)
    printf
      (_("\nDynamic symbol information is not available for displaying symbols.\n"));

  if (buckets != NULL)
    {
      free (buckets);
      free (chains);
    }

  if (gnubuckets != NULL)
    {
      free (gnubuckets);
      free (gnuchains);
    }

  return 1;
}

static int
process_syminfo (FILE * file ATTRIBUTE_UNUSED)
{
  unsigned int i;

  if (dynamic_syminfo == NULL
      || !do_dynamic)
    /* No syminfo, this is ok.  */
    return 1;

  /* There better should be a dynamic symbol section.  */
  if (dynamic_symbols == NULL || dynamic_strings == NULL)
    return 0;

  if (dynamic_addr)
    printf (_("\nDynamic info segment at offset 0x%lx contains %d entries:\n"),
	    dynamic_syminfo_offset, dynamic_syminfo_nent);

  printf (_(" Num: Name                           BoundTo     Flags\n"));
  for (i = 0; i < dynamic_syminfo_nent; ++i)
    {
      unsigned short int flags = dynamic_syminfo[i].si_flags;

      printf ("%4d: ", i);
      if (VALID_DYNAMIC_NAME (dynamic_symbols[i].st_name))
	print_symbol (30, GET_DYNAMIC_NAME (dynamic_symbols[i].st_name));
      else
	printf (_("<corrupt: %19ld>"), dynamic_symbols[i].st_name);
      putchar (' ');

      switch (dynamic_syminfo[i].si_boundto)
	{
	case SYMINFO_BT_SELF:
	  fputs ("SELF       ", stdout);
	  break;
	case SYMINFO_BT_PARENT:
	  fputs ("PARENT     ", stdout);
	  break;
	default:
	  if (dynamic_syminfo[i].si_boundto > 0
	      && dynamic_syminfo[i].si_boundto < dynamic_nent
	      && VALID_DYNAMIC_NAME (dynamic_section[dynamic_syminfo[i].si_boundto].d_un.d_val))
	    {
	      print_symbol (10, GET_DYNAMIC_NAME (dynamic_section[dynamic_syminfo[i].si_boundto].d_un.d_val));
	      putchar (' ' );
	    }
	  else
	    printf ("%-10d ", dynamic_syminfo[i].si_boundto);
	  break;
	}

      if (flags & SYMINFO_FLG_DIRECT)
	printf (" DIRECT");
      if (flags & SYMINFO_FLG_PASSTHRU)
	printf (" PASSTHRU");
      if (flags & SYMINFO_FLG_COPY)
	printf (" COPY");
      if (flags & SYMINFO_FLG_LAZYLOAD)
	printf (" LAZYLOAD");

      puts ("");
    }

  return 1;
}

/* Returns TRUE iff RELOC_TYPE is a NONE relocation used for discarded
   relocation entries.  */

static bfd_boolean
is_none_reloc (unsigned int reloc_type)
{
  switch (elf_header.e_machine)
    {
    case EM_ARM:     /* R_ARM_NONE.  */
      return reloc_type == 0;
    default:
      return FALSE;
    }
}

/* Returns TRUE iff RELOC_TYPE is a 32-bit absolute relocation used in
   DWARF debug sections.  */

static bfd_boolean
is_32bit_abs_reloc (unsigned int reloc_type)
{
  switch (elf_header.e_machine)
    {
    case EM_ARM:
      return reloc_type == 2; /* R_ARM_ABS32 */
    default:
      return FALSE;
    }
}

/* Like is_32bit_abs_reloc except that it returns TRUE iff RELOC_TYPE is
   a 32-bit pc-relative relocation used in DWARF debug sections.  */

static bfd_boolean
is_32bit_pcrel_reloc (unsigned int reloc_type)
{
  switch (elf_header.e_machine)
    {
    case EM_ARM:
      return reloc_type == 3;  /* R_ARM_REL32 */
    default:
      return FALSE;
    }
}

/* Apply relocations to a section.
   Note: So far support has been added only for those relocations
   which can be found in debug sections.  */

static void
apply_relocations (void * file,
		   Elf_Internal_Shdr * section,
		   unsigned char * start)
{
  Elf_Internal_Shdr * relsec;
  unsigned char * end = start + section->sh_size;

  if (elf_header.e_type != ET_REL)
    return;

  /* Find the reloc section associated with the section.  */
  for (relsec = section_headers;
       relsec < section_headers + elf_header.e_shnum;
       ++relsec)
    {
      bfd_boolean is_rela;
      unsigned long num_relocs;
      Elf_Internal_Rela * relocs;
      Elf_Internal_Rela * rp;
      Elf_Internal_Shdr * symsec;
      Elf_Internal_Sym * symtab;
      unsigned long num_syms;
      Elf_Internal_Sym * sym;

      if ((relsec->sh_type != SHT_RELA && relsec->sh_type != SHT_REL)
	  || relsec->sh_info >= elf_header.e_shnum
	  || section_headers + relsec->sh_info != section
	  || relsec->sh_size == 0
	  || relsec->sh_link >= elf_header.e_shnum)
	continue;

      is_rela = relsec->sh_type == SHT_RELA;

      if (is_rela)
	{
	  if (!slurp_rela_relocs ((FILE *) file, relsec->sh_offset,
                                  relsec->sh_size, & relocs, & num_relocs))
	    return;
	}
      else
	{
	  if (!slurp_rel_relocs ((FILE *) file, relsec->sh_offset,
                                 relsec->sh_size, & relocs, & num_relocs))
	    return;
	}

      symsec = section_headers + relsec->sh_link;
      symtab = get_32bit_elf_symbols ((FILE *) file, symsec, & num_syms);

      for (rp = relocs; rp < relocs + num_relocs; ++rp)
	{
	  bfd_vma         addend;
	  unsigned int    reloc_type;
	  unsigned int    reloc_size;
	  unsigned char * rloc;
	  unsigned long   sym_index;

	  reloc_type = get_reloc_type (rp->r_info);

	  if (is_none_reloc (reloc_type))
	    continue;
	  else if (is_32bit_abs_reloc (reloc_type)
		   || is_32bit_pcrel_reloc (reloc_type))
	    reloc_size = 4;
	  else
	    {
	      warn (_("unable to apply unsupported reloc type %d to section %s\n"),
		    reloc_type, SECTION_NAME (section));
	      continue;
	    }

	  rloc = start + rp->r_offset;
	  if ((rloc + reloc_size) > end || (rloc < start))
	    {
	      warn (_("skipping invalid relocation offset 0x%lx in section %s\n"),
		    (unsigned long) rp->r_offset,
		    SECTION_NAME (section));
	      continue;
	    }

	  sym_index = (unsigned long) get_reloc_symindex (rp->r_info);
	  if (sym_index >= num_syms)
	    {
	      warn (_("skipping invalid relocation symbol index 0x%lx in section %s\n"),
		    sym_index, SECTION_NAME (section));
	      continue;
	    }
	  sym = symtab + sym_index;

	  /* If the reloc has a symbol associated with it,
	     make sure that it is of an appropriate type.  */
	  if (sym != symtab
	      && ELF_ST_TYPE (sym->st_info) > STT_SECTION)
	    {
	      warn (_("skipping unexpected symbol type %s in %ld'th relocation in section %s\n"),
		    get_symbol_type (ELF_ST_TYPE (sym->st_info)),
		    (long int)(rp - relocs),
		    SECTION_NAME (relsec));
	      continue;
	    }

	  addend = 0;
	  if (is_rela)
	    addend += rp->r_addend;
	  if (!is_rela)
	    addend += byte_get (rloc, reloc_size);

	  if (is_32bit_pcrel_reloc (reloc_type))
	    byte_put (rloc, (addend + sym->st_value) - rp->r_offset,
		      reloc_size);
	  else
	    byte_put (rloc, addend + sym->st_value, reloc_size);
	}

      free (symtab);
      free (relocs);
      break;
    }
}

/* Reads in the contents of SECTION from FILE, returning a pointer
   to a malloc'ed buffer or NULL if something went wrong.  */

static char *
get_section_contents (Elf_Internal_Shdr * section, FILE * file)
{
  bfd_size_type num_bytes;

  num_bytes = section->sh_size;

  if (num_bytes == 0 || section->sh_type == SHT_NOBITS)
    {
      printf (_("\nSection '%s' has no data to dump.\n"),
	      SECTION_NAME (section));
      return NULL;
    }

  return  (char *) get_data (NULL, file, section->sh_offset, 1, num_bytes,
                             _("section contents"));
}

static void
dump_section_as_strings (Elf_Internal_Shdr * section, FILE * file)
{
  Elf_Internal_Shdr * relsec;
  bfd_size_type num_bytes;
  char * data;
  char * end;
  char * start;
  char * name = SECTION_NAME (section);
  bfd_boolean some_strings_shown;

  start = get_section_contents (section, file);
  if (start == NULL)
    return;

  printf (_("\nString dump of section '%s':\n"), name);

  /* If the section being dumped has relocations against it the user might
     be expecting these relocations to have been applied.  */
  for (relsec = section_headers;
       relsec < section_headers + elf_header.e_shnum;
       ++relsec)
    {
      if ((relsec->sh_type != SHT_RELA && relsec->sh_type != SHT_REL)
	  || relsec->sh_info >= elf_header.e_shnum
	  || section_headers + relsec->sh_info != section
	  || relsec->sh_size == 0
	  || relsec->sh_link >= elf_header.e_shnum)
	continue;

      printf (_("  Note: This section has relocations against it, but these have NOT been applied to this dump.\n"));
      break;
    }

  num_bytes = section->sh_size;
  data = start;
  end  = start + num_bytes;
  some_strings_shown = FALSE;

  while (data < end)
    {
      while (!ISPRINT (* data))
	if (++ data >= end)
	  break;

      if (data < end)
	{
#ifndef __MSVCRT__
	  printf ("  [%6tx]  ", data - start);
	  printf ("%s\n", data);
#else
	  printf ("  [%6Ix]  %s\n", (size_t) (data - start), data);
#endif
	  data += strlen (data);
	  some_strings_shown = TRUE;
	}
    }

  if (! some_strings_shown)
    printf (_("  No strings found in this section."));

  free (start);

  putchar ('\n');
}

static void
dump_section_as_bytes (Elf_Internal_Shdr * section,
		       FILE * file,
		       bfd_boolean relocate)
{
  Elf_Internal_Shdr * relsec;
  bfd_size_type bytes;
  bfd_vma addr;
  unsigned char * data;
  unsigned char * start;

  start = (unsigned char *) get_section_contents (section, file);
  if (start == NULL)
    return;

  printf (_("\nHex dump of section '%s':\n"), SECTION_NAME (section));

  if (relocate)
    {
      apply_relocations (file, section, start);
    }
  else
    {
      for (relsec = section_headers;
	   relsec < section_headers + elf_header.e_shnum;
	   ++relsec)
	{
	  if ((relsec->sh_type != SHT_RELA && relsec->sh_type != SHT_REL)
	      || relsec->sh_info >= elf_header.e_shnum
	      || section_headers + relsec->sh_info != section
	      || relsec->sh_size == 0
	      || relsec->sh_link >= elf_header.e_shnum)
	    continue;

	  printf (_(" NOTE: This section has relocations against it, but these have NOT been applied to this dump.\n"));
	  break;
	}
    }

  addr = section->sh_addr;
  bytes = section->sh_size;
  data = start;

  while (bytes)
    {
      int j;
      int k;
      int lbytes;

      lbytes = (bytes > 16 ? 16 : bytes);

      printf ("  0x%8.8lx ", (unsigned long) addr);

      for (j = 0; j < 16; j++)
	{
	  if (j < lbytes)
	    printf ("%2.2x", data[j]);
	  else
	    printf ("  ");

	  if ((j & 3) == 3)
	    printf (" ");
	}

      for (j = 0; j < lbytes; j++)
	{
	  k = data[j];
	  if (k >= ' ' && k < 0x7f)
	    printf ("%c", k);
	  else
	    printf (".");
	}

      putchar ('\n');

      data  += lbytes;
      addr  += lbytes;
      bytes -= lbytes;
    }

  free (start);

  putchar ('\n');
}

/* Set DUMP_SECTS for all sections where dumps were requested
   based on section name.  */

static void
initialise_dumps_byname (void)
{
  struct dump_list_entry * cur;

  for (cur = dump_sects_byname; cur; cur = cur->next)
    {
      unsigned int i;
      int any;

      for (i = 0, any = 0; i < elf_header.e_shnum; i++)
	if (streq (SECTION_NAME (section_headers + i), cur->name))
	  {
	    request_dump_bynumber (i, cur->type);
	    any = 1;
	  }

      if (!any)
	warn (_("Section '%s' was not dumped because it does not exist!\n"),
	      cur->name);
    }
}

static void
process_section_contents (FILE * file)
{
  Elf_Internal_Shdr * section;
  unsigned int i;

  if (! do_dump)
    return;

  initialise_dumps_byname ();

  for (i = 0, section = section_headers;
       i < elf_header.e_shnum && i < num_dump_sects;
       i++, section++)
    {
      if (dump_sects[i] & HEX_DUMP)
	dump_section_as_bytes (section, file, FALSE);

      if (dump_sects[i] & RELOC_DUMP)
	dump_section_as_bytes (section, file, TRUE);

      if (dump_sects[i] & STRING_DUMP)
	dump_section_as_strings (section, file);
    }

  /* Check to see if the user requested a
     dump of a section that does not exist.  */
  while (i++ < num_dump_sects)
    if (dump_sects[i])
      warn (_("Section %d was not dumped because it does not exist!\n"), i);
}

/* Display's the value of TAG at location P.  */

static unsigned char *
display_tag_value (int tag,
		   unsigned char * p,
		   const unsigned char * const end)
{
  unsigned long val;

  if (tag > 0)
    printf ("  Tag_unknown_%d: ", tag);

  if (p >= end)
    {
      warn (_("corrupt tag\n"));
    }
  else if (tag & 1)
    {
      printf ("\"%s\"\n", p);
      p += strlen ((char *) p) + 1;
    }
  else
    {
      unsigned int len;

      val = read_uleb128 (p, &len, end);
      p += len;
      printf ("%ld (0x%lx)\n", val, val);
    }

  return p;
}

/* ARM EABI attributes section.  */
typedef struct
{
  int tag;
  const char * name;
  /* 0 = special, 1 = string, 2 = uleb123, > 0x80 == table lookup.  */
  int type;
  const char ** table;
} arm_attr_public_tag;

static const char * arm_attr_tag_CPU_arch[] =
  {"Pre-v4", "v4", "v4T", "v5T", "v5TE", "v5TEJ", "v6", "v6KZ", "v6T2",
   "v6K", "v7", "v6-M", "v6S-M", "v7E-M", "v8"};
static const char * arm_attr_tag_ARM_ISA_use[] = {"No", "Yes"};
static const char * arm_attr_tag_THUMB_ISA_use[] =
  {"No", "Thumb-1", "Thumb-2"};
static const char * arm_attr_tag_FP_arch[] =
  {"No", "VFPv1", "VFPv2", "VFPv3", "VFPv3-D16", "VFPv4", "VFPv4-D16",
   "FP for ARMv8"};
static const char * arm_attr_tag_WMMX_arch[] = {"No", "WMMXv1", "WMMXv2"};
static const char * arm_attr_tag_Advanced_SIMD_arch[] =
  {"No", "NEONv1", "NEONv1 with Fused-MAC", "NEON for ARMv8"};
static const char * arm_attr_tag_PCS_config[] =
  {"None", "Bare platform", "Linux application", "Linux DSO", "PalmOS 2004",
   "PalmOS (reserved)", "SymbianOS 2004", "SymbianOS (reserved)"};
static const char * arm_attr_tag_ABI_PCS_R9_use[] =
  {"V6", "SB", "TLS", "Unused"};
static const char * arm_attr_tag_ABI_PCS_RW_data[] =
  {"Absolute", "PC-relative", "SB-relative", "None"};
static const char * arm_attr_tag_ABI_PCS_RO_data[] =
  {"Absolute", "PC-relative", "None"};
static const char * arm_attr_tag_ABI_PCS_GOT_use[] =
  {"None", "direct", "GOT-indirect"};
static const char * arm_attr_tag_ABI_PCS_wchar_t[] =
  {"None", "??? 1", "2", "??? 3", "4"};
static const char * arm_attr_tag_ABI_FP_rounding[] = {"Unused", "Needed"};
static const char * arm_attr_tag_ABI_FP_denormal[] =
  {"Unused", "Needed", "Sign only"};
static const char * arm_attr_tag_ABI_FP_exceptions[] = {"Unused", "Needed"};
static const char * arm_attr_tag_ABI_FP_user_exceptions[] = {"Unused", "Needed"};
static const char * arm_attr_tag_ABI_FP_number_model[] =
  {"Unused", "Finite", "RTABI", "IEEE 754"};
static const char * arm_attr_tag_ABI_enum_size[] =
  {"Unused", "small", "int", "forced to int"};
static const char * arm_attr_tag_ABI_HardFP_use[] =
  {"As Tag_FP_arch", "SP only", "DP only", "SP and DP"};
static const char * arm_attr_tag_ABI_VFP_args[] =
  {"AAPCS", "VFP registers", "custom"};
static const char * arm_attr_tag_ABI_WMMX_args[] =
  {"AAPCS", "WMMX registers", "custom"};
static const char * arm_attr_tag_ABI_optimization_goals[] =
  {"None", "Prefer Speed", "Aggressive Speed", "Prefer Size",
    "Aggressive Size", "Prefer Debug", "Aggressive Debug"};
static const char * arm_attr_tag_ABI_FP_optimization_goals[] =
  {"None", "Prefer Speed", "Aggressive Speed", "Prefer Size",
    "Aggressive Size", "Prefer Accuracy", "Aggressive Accuracy"};
static const char * arm_attr_tag_CPU_unaligned_access[] = {"None", "v6"};
static const char * arm_attr_tag_FP_HP_extension[] =
  {"Not Allowed", "Allowed"};
static const char * arm_attr_tag_ABI_FP_16bit_format[] =
  {"None", "IEEE 754", "Alternative Format"};
static const char * arm_attr_tag_MPextension_use[] =
  {"Not Allowed", "Allowed"};
static const char * arm_attr_tag_DIV_use[] =
  {"Allowed in Thumb-ISA, v7-R or v7-M", "Not allowed",
    "Allowed in v7-A with integer division extension"};
static const char * arm_attr_tag_T2EE_use[] = {"Not Allowed", "Allowed"};
static const char * arm_attr_tag_Virtualization_use[] =
  {"Not Allowed", "TrustZone", "Virtualization Extensions",
    "TrustZone and Virtualization Extensions"};
static const char * arm_attr_tag_MPextension_use_legacy[] =
  {"Not Allowed", "Allowed"};

#define LOOKUP(id, name) \
  {id, #name, 0x80 | ARRAY_SIZE(arm_attr_tag_##name), arm_attr_tag_##name}
static arm_attr_public_tag arm_attr_public_tags[] =
{
  {4, "CPU_raw_name", 1, NULL},
  {5, "CPU_name", 1, NULL},
  LOOKUP(6, CPU_arch),
  {7, "CPU_arch_profile", 0, NULL},
  LOOKUP(8, ARM_ISA_use),
  LOOKUP(9, THUMB_ISA_use),
  LOOKUP(10, FP_arch),
  LOOKUP(11, WMMX_arch),
  LOOKUP(12, Advanced_SIMD_arch),
  LOOKUP(13, PCS_config),
  LOOKUP(14, ABI_PCS_R9_use),
  LOOKUP(15, ABI_PCS_RW_data),
  LOOKUP(16, ABI_PCS_RO_data),
  LOOKUP(17, ABI_PCS_GOT_use),
  LOOKUP(18, ABI_PCS_wchar_t),
  LOOKUP(19, ABI_FP_rounding),
  LOOKUP(20, ABI_FP_denormal),
  LOOKUP(21, ABI_FP_exceptions),
  LOOKUP(22, ABI_FP_user_exceptions),
  LOOKUP(23, ABI_FP_number_model),
  {24, "ABI_align_needed", 0, NULL},
  {25, "ABI_align_preserved", 0, NULL},
  LOOKUP(26, ABI_enum_size),
  LOOKUP(27, ABI_HardFP_use),
  LOOKUP(28, ABI_VFP_args),
  LOOKUP(29, ABI_WMMX_args),
  LOOKUP(30, ABI_optimization_goals),
  LOOKUP(31, ABI_FP_optimization_goals),
  {32, "compatibility", 0, NULL},
  LOOKUP(34, CPU_unaligned_access),
  LOOKUP(36, FP_HP_extension),
  LOOKUP(38, ABI_FP_16bit_format),
  LOOKUP(42, MPextension_use),
  LOOKUP(44, DIV_use),
  {64, "nodefaults", 0, NULL},
  {65, "also_compatible_with", 0, NULL},
  LOOKUP(66, T2EE_use),
  {67, "conformance", 1, NULL},
  LOOKUP(68, Virtualization_use),
  LOOKUP(70, MPextension_use_legacy)
};
#undef LOOKUP

static unsigned char *
display_arm_attribute (unsigned char * p,
		       const unsigned char * const end)
{
  int tag;
  unsigned int len;
  int val;
  arm_attr_public_tag * attr;
  unsigned i;
  int type;

  tag = read_uleb128 (p, &len, end);
  p += len;
  attr = NULL;
  for (i = 0; i < ARRAY_SIZE (arm_attr_public_tags); i++)
    {
      if (arm_attr_public_tags[i].tag == tag)
	{
	  attr = &arm_attr_public_tags[i];
	  break;
	}
    }

  if (attr)
    {
      printf ("  Tag_%s: ", attr->name);
      switch (attr->type)
	{
	case 0:
	  switch (tag)
	    {
	    case 7: /* Tag_CPU_arch_profile.  */
	      val = read_uleb128 (p, &len, end);
	      p += len;
	      switch (val)
		{
		case 0: printf (_("None\n")); break;
		case 'A': printf (_("Application\n")); break;
		case 'R': printf (_("Realtime\n")); break;
		case 'M': printf (_("Microcontroller\n")); break;
		case 'S': printf (_("Application or Realtime\n")); break;
		default: printf ("??? (%d)\n", val); break;
		}
	      break;

	    case 24: /* Tag_align_needed.  */
	      val = read_uleb128 (p, &len, end);
	      p += len;
	      switch (val)
		{
		case 0: printf (_("None\n")); break;
		case 1: printf (_("8-byte\n")); break;
		case 2: printf (_("4-byte\n")); break;
		case 3: printf ("??? 3\n"); break;
		default:
		  if (val <= 12)
		    printf (_("8-byte and up to %d-byte extended\n"),
			    1 << val);
		  else
		    printf ("??? (%d)\n", val);
		  break;
		}
	      break;

	    case 25: /* Tag_align_preserved.  */
	      val = read_uleb128 (p, &len, end);
	      p += len;
	      switch (val)
		{
		case 0: printf (_("None\n")); break;
		case 1: printf (_("8-byte, except leaf SP\n")); break;
		case 2: printf (_("8-byte\n")); break;
		case 3: printf ("??? 3\n"); break;
		default:
		  if (val <= 12)
		    printf (_("8-byte and up to %d-byte extended\n"),
			    1 << val);
		  else
		    printf ("??? (%d)\n", val);
		  break;
		}
	      break;

	    case 32: /* Tag_compatibility.  */
	      val = read_uleb128 (p, &len, end);
	      p += len;
	      printf (_("flag = %d, vendor = %s\n"), val, p);
	      p += strlen ((char *) p) + 1;
	      break;

	    case 64: /* Tag_nodefaults.  */
	      p++;
	      printf (_("True\n"));
	      break;

	    case 65: /* Tag_also_compatible_with.  */
	      val = read_uleb128 (p, &len, end);
	      p += len;
	      if (val == 6 /* Tag_CPU_arch.  */)
		{
		  val = read_uleb128 (p, &len, end);
		  p += len;
		  if ((unsigned int)val >= ARRAY_SIZE (arm_attr_tag_CPU_arch))
		    printf ("??? (%d)\n", val);
		  else
		    printf ("%s\n", arm_attr_tag_CPU_arch[val]);
		}
	      else
		printf ("???\n");
	      while (*(p++) != '\0' /* NUL terminator.  */);
	      break;

	    default:
	      abort ();
	    }
	  return p;

	case 1:
	  return display_tag_value (-1, p, end);
	case 2:
	  return display_tag_value (0, p, end);

	default:
	  assert (attr->type & 0x80);
	  val = read_uleb128 (p, &len, end);
	  p += len;
	  type = attr->type & 0x7f;
	  if (val >= type)
	    printf ("??? (%d)\n", val);
	  else
	    printf ("%s\n", attr->table[val]);
	  return p;
	}
    }

  return display_tag_value (tag, p, end);
}

static unsigned char *
display_gnu_attribute (unsigned char * p,
		       unsigned char * (* display_proc_gnu_attribute) (unsigned char *, int, const unsigned char * const),
		       const unsigned char * const end)
{
  int tag;
  unsigned int len;
  int val;

  tag = read_uleb128 (p, &len, end);
  p += len;

  /* Tag_compatibility is the only generic GNU attribute defined at
     present.  */
  if (tag == 32)
    {
      val = read_uleb128 (p, &len, end);
      p += len;
      if (p == end)
	{
	  printf (_("flag = %d, vendor = <corrupt>\n"), val);
	  warn (_("corrupt vendor attribute\n"));
	}
      else
	{
	  printf (_("flag = %d, vendor = %s\n"), val, p);
	  p += strlen ((char *) p) + 1;
	}
      return p;
    }

  if ((tag & 2) == 0 && display_proc_gnu_attribute)
    return display_proc_gnu_attribute (p, tag, end);

  return display_tag_value (tag, p, end);
}

static void
display_raw_attribute (unsigned char * p, unsigned char * end)
{
  unsigned long addr = 0;
  size_t bytes = end - p;

  while (bytes)
    {
      int j;
      int k;
      int lbytes = (bytes > 16 ? 16 : bytes);

      printf ("  0x%8.8lx ", addr);

      for (j = 0; j < 16; j++)
	{
	  if (j < lbytes)
	    printf ("%2.2x", p[j]);
	  else
	    printf ("  ");

	  if ((j & 3) == 3)
	    printf (" ");
	}

      for (j = 0; j < lbytes; j++)
	{
	  k = p[j];
	  if (k >= ' ' && k < 0x7f)
	    printf ("%c", k);
	  else
	    printf (".");
	}

      putchar ('\n');

      p  += lbytes;
      bytes -= lbytes;
      addr += lbytes;
    }

  putchar ('\n');
}

static int
process_attributes (FILE * file,
		    const char * public_name,
		    unsigned int proc_type,
		    unsigned char * (* display_pub_attribute) (unsigned char *, const unsigned char * const),
		    unsigned char * (* display_proc_gnu_attribute) (unsigned char *, int, const unsigned char * const))
{
  Elf_Internal_Shdr * sect;
  unsigned char * contents;
  unsigned char * p;
  unsigned char * end;
  bfd_vma section_len;
  bfd_vma len;
  unsigned i;

  /* Find the section header so that we get the size.  */
  for (i = 0, sect = section_headers;
       i < elf_header.e_shnum;
       i++, sect++)
    {
      if (sect->sh_type != proc_type && sect->sh_type != SHT_GNU_ATTRIBUTES)
	continue;

      contents = (unsigned char *) get_data (NULL, file, sect->sh_offset, 1,
                                             sect->sh_size, _("attributes"));
      if (contents == NULL)
	continue;

      p = contents;
      if (*p == 'A')
	{
	  len = sect->sh_size - 1;
	  p++;

	  while (len > 0)
	    {
	      int namelen;
	      bfd_boolean public_section;
	      bfd_boolean gnu_section;

	      section_len = byte_get (p, 4);
	      p += 4;

	      if (section_len > len)
		{
		  printf (_("ERROR: Bad section length (%d > %d)\n"),
			  (int) section_len, (int) len);
		  section_len = len;
		}

	      len -= section_len;
	      printf (_("Attribute Section: %s\n"), p);

	      if (public_name && streq ((char *) p, public_name))
		public_section = TRUE;
	      else
		public_section = FALSE;

	      if (streq ((char *) p, "gnu"))
		gnu_section = TRUE;
	      else
		gnu_section = FALSE;

	      namelen = strlen ((char *) p) + 1;
	      p += namelen;
	      section_len -= namelen + 4;

	      while (section_len > 0)
		{
		  int tag = *(p++);
		  int val;
		  bfd_vma size;

		  size = byte_get (p, 4);
		  if (size > section_len)
		    {
		      printf (_("ERROR: Bad subsection length (%d > %d)\n"),
			      (int) size, (int) section_len);
		      size = section_len;
		    }

		  section_len -= size;
		  end = p + size - 1;
		  p += 4;

		  switch (tag)
		    {
		    case 1:
		      printf (_("File Attributes\n"));
		      break;
		    case 2:
		      printf (_("Section Attributes:"));
		      goto do_numlist;
		    case 3:
		      printf (_("Symbol Attributes:"));
		    do_numlist:
		      for (;;)
			{
			  unsigned int j;

			  val = read_uleb128 (p, &j, end);
			  p += j;
			  if (val == 0)
			    break;
			  printf (" %d", val);
			}
		      printf ("\n");
		      break;
		    default:
		      printf (_("Unknown tag: %d\n"), tag);
		      public_section = FALSE;
		      break;
		    }

		  if (public_section)
		    {
		      while (p < end)
			p = display_pub_attribute (p, end);
		    }
		  else if (gnu_section)
		    {
		      while (p < end)
			p = display_gnu_attribute (p,
						   display_proc_gnu_attribute,
						   end);
		    }
		  else
		    {
		      printf (_("  Unknown section contexts\n"));
		      display_raw_attribute (p, end);
		      p = end;
		    }
		}
	    }
	}
      else
	printf (_("Unknown format '%c'\n"), *p);

      free (contents);
    }
  return 1;
}

static int
process_arm_specific (FILE * file)
{
  return process_attributes (file, "aeabi", SHT_ARM_ATTRIBUTES,
			     display_arm_attribute, NULL);
}

static const char *
get_note_type (unsigned e_type)
{
  static char buff[64];

  if (elf_header.e_type == ET_CORE)
    switch (e_type)
      {
      case NT_AUXV:
	return _("NT_AUXV (auxiliary vector)");
      case NT_PRSTATUS:
	return _("NT_PRSTATUS (prstatus structure)");
      case NT_FPREGSET:
	return _("NT_FPREGSET (floating point registers)");
      case NT_PRPSINFO:
	return _("NT_PRPSINFO (prpsinfo structure)");
      case NT_TASKSTRUCT:
	return _("NT_TASKSTRUCT (task structure)");
      case NT_PRXFPREG:
	return _("NT_PRXFPREG (user_xfpregs structure)");
      case NT_PPC_VMX:
	return _("NT_PPC_VMX (ppc Altivec registers)");
      case NT_PPC_VSX:
	return _("NT_PPC_VSX (ppc VSX registers)");
      case NT_386_TLS:
	return _("NT_386_TLS (x86 TLS information)");
      case NT_386_IOPERM:
	return _("NT_386_IOPERM (x86 I/O permissions)");
      case NT_X86_XSTATE:
	return _("NT_X86_XSTATE (x86 XSAVE extended state)");
      case NT_S390_HIGH_GPRS:
	return _("NT_S390_HIGH_GPRS (s390 upper register halves)");
      case NT_S390_TIMER:
	return _("NT_S390_TIMER (s390 timer register)");
      case NT_S390_TODCMP:
	return _("NT_S390_TODCMP (s390 TOD comparator register)");
      case NT_S390_TODPREG:
	return _("NT_S390_TODPREG (s390 TOD programmable register)");
      case NT_S390_CTRS:
	return _("NT_S390_CTRS (s390 control registers)");
      case NT_S390_PREFIX:
	return _("NT_S390_PREFIX (s390 prefix register)");
      case NT_S390_LAST_BREAK:
	return _("NT_S390_LAST_BREAK (s390 last breaking event address)");
      case NT_S390_SYSTEM_CALL:
	return _("NT_S390_SYSTEM_CALL (s390 system call restart data)");
      case NT_S390_TDB:
	return _("NT_S390_TDB (s390 transaction diagnostic block)");
      case NT_ARM_VFP:
	return _("NT_ARM_VFP (arm VFP registers)");
      case NT_ARM_TLS:
	return _("NT_ARM_TLS (AArch TLS registers)");
      case NT_ARM_HW_BREAK:
	return _("NT_ARM_HW_BREAK (AArch hardware breakpoint registers)");
      case NT_ARM_HW_WATCH:
	return _("NT_ARM_HW_WATCH (AArch hardware watchpoint registers)");
      case NT_PSTATUS:
	return _("NT_PSTATUS (pstatus structure)");
      case NT_FPREGS:
	return _("NT_FPREGS (floating point registers)");
      case NT_PSINFO:
	return _("NT_PSINFO (psinfo structure)");
      case NT_LWPSTATUS:
	return _("NT_LWPSTATUS (lwpstatus_t structure)");
      case NT_LWPSINFO:
	return _("NT_LWPSINFO (lwpsinfo_t structure)");
      case NT_WIN32PSTATUS:
	return _("NT_WIN32PSTATUS (win32_pstatus structure)");
      case NT_SIGINFO:
	return _("NT_SIGINFO (siginfo_t data)");
      case NT_FILE:
	return _("NT_FILE (mapped files)");
      default:
	break;
      }
  else
    switch (e_type)
      {
      case NT_VERSION:
	return _("NT_VERSION (version)");
      case NT_ARCH:
	return _("NT_ARCH (architecture)");
      default:
	break;
      }

  snprintf (buff, sizeof (buff), _("Unknown note type: (0x%08x)"), e_type);
  return buff;
}

static int
print_core_note (Elf_Internal_Note *pnote)
{
  unsigned int addr_size = is_32bit_elf ? 4 : 8;
  bfd_vma count, page_size;
  unsigned char *descdata, *filenames, *descend;

  if (pnote->type != NT_FILE)
    return 1;

  if (pnote->descsz < 2 * addr_size)
    {
      printf (_("    Malformed note - too short for header\n"));
      return 0;
    }

  descdata = (unsigned char *) pnote->descdata;
  descend = descdata + pnote->descsz;

  if (descdata[pnote->descsz - 1] != '\0')
    {
      printf (_("    Malformed note - does not end with \\0\n"));
      return 0;
    }

  count = byte_get (descdata, addr_size);
  descdata += addr_size;

  page_size = byte_get (descdata, addr_size);
  descdata += addr_size;

  if (pnote->descsz < 2 * addr_size + count * 3 * addr_size)
    {
      printf (_("    Malformed note - too short for supplied file count\n"));
      return 0;
    }

  printf (_("    Page size: "));
  print_vma (page_size, DEC);
  printf ("\n");

  printf (_("    %*s%*s%*s\n"),
	  (int) (2 + 2 * addr_size), _("Start"),
	  (int) (4 + 2 * addr_size), _("End"),
	  (int) (4 + 2 * addr_size), _("Page Offset"));
  filenames = descdata + count * 3 * addr_size;
  while (--count > 0)
    {
      bfd_vma start, end, file_ofs;

      if (filenames == descend)
	{
	  printf (_("    Malformed note - filenames end too early\n"));
	  return 0;
	}

      start = byte_get (descdata, addr_size);
      descdata += addr_size;
      end = byte_get (descdata, addr_size);
      descdata += addr_size;
      file_ofs = byte_get (descdata, addr_size);
      descdata += addr_size;

      printf ("    ");
      print_vma (start, FULL_HEX);
      printf ("  ");
      print_vma (end, FULL_HEX);
      printf ("  ");
      print_vma (file_ofs, FULL_HEX);
      printf ("\n        %s\n", filenames);

      filenames += 1 + strlen ((char *) filenames);
    }

  return 1;
}

static const char *
get_gnu_elf_note_type (unsigned e_type)
{
  static char buff[64];

  switch (e_type)
    {
    case NT_GNU_ABI_TAG:
      return _("NT_GNU_ABI_TAG (ABI version tag)");
    case NT_GNU_HWCAP:
      return _("NT_GNU_HWCAP (DSO-supplied software HWCAP info)");
    case NT_GNU_BUILD_ID:
      return _("NT_GNU_BUILD_ID (unique build ID bitstring)");
    case NT_GNU_GOLD_VERSION:
      return _("NT_GNU_GOLD_VERSION (gold version)");
    default:
      break;
    }

  snprintf (buff, sizeof (buff), _("Unknown note type: (0x%08x)"), e_type);
  return buff;
}

static int
print_gnu_note (Elf_Internal_Note *pnote)
{
  switch (pnote->type)
    {
    case NT_GNU_BUILD_ID:
      {
	unsigned long i;

	printf (_("    Build ID: "));
	for (i = 0; i < pnote->descsz; ++i)
	  printf ("%02x", pnote->descdata[i] & 0xff);
	printf ("\n");
      }
      break;

    case NT_GNU_ABI_TAG:
      {
	unsigned long os, major, minor, subminor;
	const char *osname;

	os = byte_get ((unsigned char *) pnote->descdata, 4);
	major = byte_get ((unsigned char *) pnote->descdata + 4, 4);
	minor = byte_get ((unsigned char *) pnote->descdata + 8, 4);
	subminor = byte_get ((unsigned char *) pnote->descdata + 12, 4);

	switch (os)
	  {
	  case GNU_ABI_TAG_LINUX:
	    osname = "Linux";
	    break;
	  case GNU_ABI_TAG_HURD:
	    osname = "Hurd";
	    break;
	  case GNU_ABI_TAG_SOLARIS:
	    osname = "Solaris";
	    break;
	  case GNU_ABI_TAG_FREEBSD:
	    osname = "FreeBSD";
	    break;
	  case GNU_ABI_TAG_NETBSD:
	    osname = "NetBSD";
	    break;
	  default:
	    osname = "Unknown";
	    break;
	  }

	printf (_("    OS: %s, ABI: %ld.%ld.%ld\n"), osname,
		major, minor, subminor);
      }
      break;
    }

  return 1;
}

static const char *
get_netbsd_elfcore_note_type (unsigned e_type)
{
  static char buff[64];

  if (e_type == NT_NETBSDCORE_PROCINFO)
    {
      /* NetBSD core "procinfo" structure.  */
      return _("NetBSD procinfo structure");
    }

  if (e_type < NT_NETBSDCORE_FIRSTMACH)
    {
      snprintf (buff, sizeof (buff), _("Unknown note type: (0x%08x)"), e_type);
      return buff;
    }

  switch (elf_header.e_machine)
    {
    case EM_OLD_ALPHA:
    case EM_ALPHA:
    case EM_SPARC:
    case EM_SPARC32PLUS:
    case EM_SPARCV9:
      switch (e_type)
	{
	case NT_NETBSDCORE_FIRSTMACH + 0:
	  return _("PT_GETREGS (reg structure)");
	case NT_NETBSDCORE_FIRSTMACH + 2:
	  return _("PT_GETFPREGS (fpreg structure)");
	default:
	  break;
	}
      break;

    default:
      switch (e_type)
	{
	case NT_NETBSDCORE_FIRSTMACH + 1:
	  return _("PT_GETREGS (reg structure)");
	case NT_NETBSDCORE_FIRSTMACH + 3:
	  return _("PT_GETFPREGS (fpreg structure)");
	default:
	  break;
	}
    }

  snprintf (buff, sizeof (buff), "PT_FIRSTMACH+%d",
	    e_type - NT_NETBSDCORE_FIRSTMACH);
  return buff;
}

static const char *
get_stapsdt_note_type (unsigned e_type)
{
  static char buff[64];

  switch (e_type)
    {
    case NT_STAPSDT:
      return _("NT_STAPSDT (SystemTap probe descriptors)");

    default:
      break;
    }

  snprintf (buff, sizeof (buff), _("Unknown note type: (0x%08x)"), e_type);
  return buff;
}

static int
print_stapsdt_note (Elf_Internal_Note *pnote)
{
  int addr_size = is_32bit_elf ? 4 : 8;
  char *data = pnote->descdata;
  char *data_end = pnote->descdata + pnote->descsz;
  bfd_vma pc, base_addr, semaphore;
  char *provider, *probe, *arg_fmt;

  pc = byte_get ((unsigned char *) data, addr_size);
  data += addr_size;
  base_addr = byte_get ((unsigned char *) data, addr_size);
  data += addr_size;
  semaphore = byte_get ((unsigned char *) data, addr_size);
  data += addr_size;

  provider = data;
  data += strlen (data) + 1;
  probe = data;
  data += strlen (data) + 1;
  arg_fmt = data;
  data += strlen (data) + 1;

  printf (_("    Provider: %s\n"), provider);
  printf (_("    Name: %s\n"), probe);
  printf (_("    Location: "));
  print_vma (pc, FULL_HEX);
  printf (_(", Base: "));
  print_vma (base_addr, FULL_HEX);
  printf (_(", Semaphore: "));
  print_vma (semaphore, FULL_HEX);
  printf ("\n");
  printf (_("    Arguments: %s\n"), arg_fmt);

  return data == data_end;
}

/* Note that by the ELF standard, the name field is already null byte
   terminated, and namesz includes the terminating null byte.  */
static int
process_note (Elf_Internal_Note * pnote)
{
  const char * name = pnote->namesz ? pnote->namedata : "(NONE)";
  const char * nt;

  if (pnote->namesz == 0)
    nt = get_note_type (pnote->type);

  else if (const_strneq (pnote->namedata, "GNU"))
    nt = get_gnu_elf_note_type (pnote->type);

  else if (const_strneq (pnote->namedata, "NetBSD-CORE"))
    nt = get_netbsd_elfcore_note_type (pnote->type);

  else if (strneq (pnote->namedata, "SPU/", 4))
    {
      nt = pnote->namedata + 4;
      name = "SPU";
    }

  else if (const_strneq (pnote->namedata, "stapsdt"))
    nt = get_stapsdt_note_type (pnote->type);

  else
    nt = get_note_type (pnote->type);

  printf ("  %-20s 0x%08lx\t%s\n", name, pnote->descsz, nt);

  if (const_strneq (pnote->namedata, "GNU"))
    return print_gnu_note (pnote);
  else if (const_strneq (pnote->namedata, "stapsdt"))
    return print_stapsdt_note (pnote);
  else if (const_strneq (pnote->namedata, "CORE"))
    return print_core_note (pnote);
  else
    return 1;
}

static int
process_corefile_note_segment (FILE * file, bfd_vma offset, bfd_vma length)
{
  Elf_External_Note * pnotes;
  Elf_External_Note * external;
  int res = 1;

  if (length <= 0)
    return 0;

  pnotes = (Elf_External_Note *) get_data (NULL, file, offset, 1, length,
					   _("notes"));
  if (pnotes == NULL)
    return 0;

  external = pnotes;

  printf (_("\nDisplaying notes found at file offset 0x%08lx with length 0x%08lx:\n"),
	  (unsigned long) offset, (unsigned long) length);
  printf (_("  %-20s %10s\tDescription\n"), _("Owner"), _("Data size"));

  while ((char *) external < (char *) pnotes + length)
    {
      Elf_Internal_Note inote;
      size_t min_notesz;
      char *next;
      char * temp = NULL;
      size_t data_remaining = ((char *) pnotes + length) - (char *) external;

      /* PR binutils/15191
	 Make sure that there is enough data to read.  */
      min_notesz = offsetof (Elf_External_Note, name);
      if (data_remaining < min_notesz)
	{
	  warn (_("Corrupt note: only %d bytes remain, not enough for a full note\n"),
		(int) data_remaining);
	  break;
	}
      inote.type     = BYTE_GET (external->type);
      inote.namesz   = BYTE_GET (external->namesz);
      inote.namedata = external->name;
      inote.descsz   = BYTE_GET (external->descsz);
      inote.descdata = inote.namedata + align_power (inote.namesz, 2);
      inote.descpos  = offset + (inote.descdata - (char *) pnotes);
      next = inote.descdata + align_power (inote.descsz, 2);

      if (inote.descdata < (char *) external + min_notesz
	  || next < (char *) external + min_notesz
	  || data_remaining < (size_t)(next - (char *) external))
	{
	  warn (_("note with invalid namesz and/or descsz found at offset 0x%lx\n"),
		(unsigned long) ((char *) external - (char *) pnotes));
	  warn (_(" type: 0x%lx, namesize: 0x%08lx, descsize: 0x%08lx\n"),
		inote.type, inote.namesz, inote.descsz);
	  break;
	}

      external = (Elf_External_Note *) next;

      /* Verify that name is null terminated.  */
      if (inote.namedata[inote.namesz - 1] != '\0')
	{
	  temp = (char *) malloc (inote.namesz + 1);

	  if (temp == NULL)
	    {
	      error (_("Out of memory\n"));
	      res = 0;
	      break;
	    }

	  strncpy (temp, inote.namedata, inote.namesz);
	  temp[inote.namesz] = 0;

	  inote.namedata = temp;
	}

      res &= process_note (& inote);

      if (temp != NULL)
	{
	  free (temp);
	  temp = NULL;
	}
    }

  free (pnotes);

  return res;
}

static int
process_corefile_note_segments (FILE * file)
{
  Elf_Internal_Phdr * segment;
  unsigned int i;
  int res = 1;

  if (! get_program_headers (file))
      return 0;

  for (i = 0, segment = program_headers;
       i < elf_header.e_phnum;
       i++, segment++)
    {
      if (segment->p_type == PT_NOTE)
	res &= process_corefile_note_segment (file,
					      (bfd_vma) segment->p_offset,
					      (bfd_vma) segment->p_filesz);
    }

  return res;
}

static int
process_note_sections (FILE * file)
{
  Elf_Internal_Shdr * section;
  unsigned long i;
  int res = 1;

  for (i = 0, section = section_headers;
       i < elf_header.e_shnum && section != NULL;
       i++, section++)
    if (section->sh_type == SHT_NOTE)
      res &= process_corefile_note_segment (file,
					    (bfd_vma) section->sh_offset,
					    (bfd_vma) section->sh_size);

  return res;
}

static int
process_notes (FILE * file)
{
  /* If we have not been asked to display the notes then do nothing.  */
  if (! do_notes)
    return 1;

  if (elf_header.e_type != ET_CORE)
    return process_note_sections (file);

  /* No program headers means no NOTE segment.  */
  if (elf_header.e_phnum > 0)
    return process_corefile_note_segments (file);

  printf (_("No note segments present in the core file.\n"));
  return 1;
}

static int
process_arch_specific (FILE * file)
{
  if (! do_arch)
    return 1;

  switch (elf_header.e_machine)
    {
    case EM_ARM:
      return process_arm_specific (file);
    default:
      break;
    }
  return 1;
}

static int
get_file_header (FILE * file)
{
  /* Read in the identity array.  */
  if (fread (elf_header.e_ident, EI_NIDENT, 1, file) != 1)
    return 0;

  /* Determine how to read the rest of the header.  */
  switch (elf_header.e_ident[EI_DATA])
    {
    default: /* fall through */
    case ELFDATANONE: /* fall through */
    case ELFDATA2LSB:
      byte_get = byte_get_little_endian;
      byte_put = byte_put_little_endian;
      break;
    case ELFDATA2MSB:
      byte_get = byte_get_big_endian;
      byte_put = byte_put_big_endian;
      break;
    }

  /* This build only supports 32 bit ELF files.  */
  if (elf_header.e_ident[EI_CLASS] == ELFCLASS64)
    {
      error (_("This instance of readelf has been built without support for a\n\
64 bit data type and so it cannot read 64 bit ELF files.\n"));
      return 0;
    }

  is_32bit_elf = 1;

  {
    Elf32_External_Ehdr ehdr32;

    if (fread (ehdr32.e_type, sizeof (ehdr32) - EI_NIDENT, 1, file) != 1)
      return 0;

    elf_header.e_type      = BYTE_GET (ehdr32.e_type);
    elf_header.e_machine   = BYTE_GET (ehdr32.e_machine);
    elf_header.e_version   = BYTE_GET (ehdr32.e_version);
    elf_header.e_entry     = BYTE_GET (ehdr32.e_entry);
    elf_header.e_phoff     = BYTE_GET (ehdr32.e_phoff);
    elf_header.e_shoff     = BYTE_GET (ehdr32.e_shoff);
    elf_header.e_flags     = BYTE_GET (ehdr32.e_flags);
    elf_header.e_ehsize    = BYTE_GET (ehdr32.e_ehsize);
    elf_header.e_phentsize = BYTE_GET (ehdr32.e_phentsize);
    elf_header.e_phnum     = BYTE_GET (ehdr32.e_phnum);
    elf_header.e_shentsize = BYTE_GET (ehdr32.e_shentsize);
    elf_header.e_shnum     = BYTE_GET (ehdr32.e_shnum);
    elf_header.e_shstrndx  = BYTE_GET (ehdr32.e_shstrndx);
  }

  if (elf_header.e_shoff)
    {
      /* There may be some extensions in the first section header.  Don't
	 bomb if we can't read it.  */
      get_32bit_section_headers (file, 1);
    }

  return 1;
}

/* Process one ELF object file according to the command line options.
   This file may actually be stored in an archive.  The file is
   positioned at the start of the ELF object.  */

static int
process_object (char * file_name, FILE * file)
{
  if (! get_file_header (file))
    {
      error (_("%s: Failed to read file header\n"), file_name);
      return 1;
    }

  /* Initialise per file variables.  */
  {
    unsigned int i;

    for (i = ARRAY_SIZE (dynamic_info); i--;)
      dynamic_info[i] = 0;
    dynamic_info_DT_GNU_HASH = 0;
  }

  /* Process the file.  */
  if (show_name)
    printf (_("\nFile: %s\n"), file_name);

  /* Initialise the dump_sects array from the cmdline_dump_sects array.  */
  if (num_dump_sects > num_cmdline_dump_sects)
    memset (dump_sects, 0, num_dump_sects * sizeof (* dump_sects));

  if (num_cmdline_dump_sects > 0)
    {
      if (num_dump_sects == 0)
	/* A sneaky way of allocating the dump_sects array.  */
	request_dump_bynumber (num_cmdline_dump_sects, 0);

      assert (num_dump_sects >= num_cmdline_dump_sects);
      memcpy (dump_sects, cmdline_dump_sects,
	      num_cmdline_dump_sects * sizeof (* dump_sects));
    }

  if (! process_file_header ())
    return 1;

  if (! process_section_headers (file))
    {
      /* Without loaded section headers we cannot process lots of
	 things.  */
      do_dump = do_arch = 0;

      if (! do_using_dynamic)
	do_syms = do_dyn_syms = do_reloc = 0;
    }

  if (process_program_headers (file))
    process_dynamic_section (file);

  process_relocs (file);

  process_symbol_table (file);

  process_syminfo (file);

  process_section_contents (file);

  process_notes (file);

  process_arch_specific (file);

  if (program_headers)
    {
      free (program_headers);
      program_headers = NULL;
    }

  if (section_headers)
    {
      free (section_headers);
      section_headers = NULL;
    }

  if (string_table)
    {
      free (string_table);
      string_table = NULL;
      string_table_length = 0;
    }

  if (dynamic_strings)
    {
      free (dynamic_strings);
      dynamic_strings = NULL;
      dynamic_strings_length = 0;
    }

  if (dynamic_symbols)
    {
      free (dynamic_symbols);
      dynamic_symbols = NULL;
      num_dynamic_syms = 0;
    }

  if (dynamic_syminfo)
    {
      free (dynamic_syminfo);
      dynamic_syminfo = NULL;
    }

  if (dynamic_section)
    {
      free (dynamic_section);
      dynamic_section = NULL;
    }

  return 0;
}

/* Process an ELF archive.
   On entry the file is positioned just after the ARMAG string.  */

static int
process_archive (char * file_name, FILE * file, bfd_boolean is_thin_archive)
{
  struct archive_info arch;
  struct archive_info nested_arch;
  size_t got;
  int ret;

  show_name = 1;

  /* The ARCH structure is used to hold information about this archive.  */
  arch.file_name = NULL;
  arch.file = NULL;
  arch.index_array = NULL;
  arch.sym_table = NULL;
  arch.longnames = NULL;

  /* The NESTED_ARCH structure is used as a single-item cache of information
     about a nested archive (when members of a thin archive reside within
     another regular archive file).  */
  nested_arch.file_name = NULL;
  nested_arch.file = NULL;
  nested_arch.index_array = NULL;
  nested_arch.sym_table = NULL;
  nested_arch.longnames = NULL;

  if (setup_archive (&arch, file_name, file, is_thin_archive, 0) != 0)
    {
      ret = 1;
      goto out;
    }

  ret = 0;

  while (1)
    {
      char * name;
      size_t namelen;
      char * qualified_name;

      /* Read the next archive header.  */
      if (fseek (file, arch.next_arhdr_offset, SEEK_SET) != 0)
        {
          error (_("%s: failed to seek to next archive header\n"), file_name);
          return 1;
        }
      got = fread (&arch.arhdr, 1, sizeof arch.arhdr, file);
      if (got != sizeof arch.arhdr)
        {
          if (got == 0)
	    break;
          error (_("%s: failed to read archive header\n"), file_name);
          ret = 1;
          break;
        }
      if (memcmp (arch.arhdr.ar_fmag, ARFMAG, 2) != 0)
        {
          error (_("%s: did not find a valid archive header\n"), arch.file_name);
          ret = 1;
          break;
        }

      arch.next_arhdr_offset += sizeof arch.arhdr;

      archive_file_size = strtoul (arch.arhdr.ar_size, NULL, 10);
      if (archive_file_size & 01)
        ++archive_file_size;

      name = get_archive_member_name (&arch, &nested_arch);
      if (name == NULL)
	{
	  error (_("%s: bad archive file name\n"), file_name);
	  ret = 1;
	  break;
	}
      namelen = strlen (name);

      qualified_name = make_qualified_name (&arch, &nested_arch, name);
      if (qualified_name == NULL)
	{
	  error (_("%s: bad archive file name\n"), file_name);
	  ret = 1;
	  break;
	}

      if (is_thin_archive && arch.nested_member_origin == 0)
        {
          /* This is a proxy for an external member of a thin archive.  */
          FILE * member_file;
          char * member_file_name = adjust_relative_path (file_name, name, namelen);
          if (member_file_name == NULL)
            {
              ret = 1;
              break;
            }

          member_file = fopen (member_file_name, "rb");
          if (member_file == NULL)
            {
              error (_("Input file '%s' is not readable.\n"), member_file_name);
              free (member_file_name);
              ret = 1;
              break;
            }

          archive_file_offset = arch.nested_member_origin;

          ret |= process_object (qualified_name, member_file);

          fclose (member_file);
          free (member_file_name);
        }
      else if (is_thin_archive)
        {
	  /* PR 15140: Allow for corrupt thin archives.  */
	  if (nested_arch.file == NULL)
	    {
	      error (_("%s: contains corrupt thin archive: %s\n"),
		     file_name, name);
	      ret = 1;
	      break;
	    }

          /* This is a proxy for a member of a nested archive.  */
          archive_file_offset = arch.nested_member_origin + sizeof arch.arhdr;

          /* The nested archive file will have been opened and setup by
             get_archive_member_name.  */
          if (fseek (nested_arch.file, archive_file_offset, SEEK_SET) != 0)
            {
              error (_("%s: failed to seek to archive member.\n"), nested_arch.file_name);
              ret = 1;
              break;
            }

          ret |= process_object (qualified_name, nested_arch.file);
        }
      else
        {
          archive_file_offset = arch.next_arhdr_offset;
          arch.next_arhdr_offset += archive_file_size;

          ret |= process_object (qualified_name, file);
        }

      if (dump_sects != NULL)
	{
	  free (dump_sects);
	  dump_sects = NULL;
	  num_dump_sects = 0;
	}

      free (qualified_name);
    }

 out:
  if (nested_arch.file != NULL)
    fclose (nested_arch.file);
  release_archive (&nested_arch);
  release_archive (&arch);

  return ret;
}

static int
process_file (char * file_name)
{
  FILE * file;
  struct stat statbuf;
  char armag[SARMAG];
  int ret;

  if (stat (file_name, &statbuf) < 0)
    {
      if (errno == ENOENT)
	error (_("'%s': No such file\n"), file_name);
      else
	error (_("Could not locate '%s'.  System error message: %s\n"),
	       file_name, strerror (errno));
      return 1;
    }

  if (! S_ISREG (statbuf.st_mode))
    {
      error (_("'%s' is not an ordinary file\n"), file_name);
      return 1;
    }

  file = fopen (file_name, "rb");
  if (file == NULL)
    {
      error (_("Input file '%s' is not readable.\n"), file_name);
      return 1;
    }

  if (fread (armag, SARMAG, 1, file) != 1)
    {
      error (_("%s: Failed to read file's magic number\n"), file_name);
      fclose (file);
      return 1;
    }

  if (memcmp (armag, ARMAG, SARMAG) == 0)
    ret = process_archive (file_name, file, FALSE);
  else if (memcmp (armag, ARMAGT, SARMAG) == 0)
    ret = process_archive (file_name, file, TRUE);
  else
    {
      rewind (file);
      archive_file_size = archive_file_offset = 0;
      ret = process_object (file_name, file);
    }

  fclose (file);

  return ret;
}

int
main (int argc, char ** argv)
{
  int err;

#if defined (HAVE_SETLOCALE) && defined (HAVE_LC_MESSAGES)
  setlocale (LC_MESSAGES, "");
#endif
#if defined (HAVE_SETLOCALE)
  setlocale (LC_CTYPE, "");
#endif
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  expandargv (&argc, &argv);

  parse_args (argc, argv);

  if (num_dump_sects > 0)
    {
      /* Make a copy of the dump_sects array.  */
      cmdline_dump_sects = (dump_type *)
          malloc (num_dump_sects * sizeof (* dump_sects));
      if (cmdline_dump_sects == NULL)
	error (_("Out of memory allocating dump request table.\n"));
      else
	{
	  memcpy (cmdline_dump_sects, dump_sects,
		  num_dump_sects * sizeof (* dump_sects));
	  num_cmdline_dump_sects = num_dump_sects;
	}
    }

  if (optind < (argc - 1))
    show_name = 1;

  err = 0;
  while (optind < argc)
    err |= process_file (argv[optind++]);

  if (dump_sects != NULL)
    free (dump_sects);
  if (cmdline_dump_sects != NULL)
    free (cmdline_dump_sects);

  return err;
}
