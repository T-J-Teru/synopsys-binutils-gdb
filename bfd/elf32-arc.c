/* ARC-specific support for 32-bit ELF
   Copyright 1994, 1995, 1997, 1998, 2000, 2001, 2002, 2005, 2006, 2007, 2008, 2009
   Free Software Foundation, Inc.
   Contributed by Doug Evans (dje@cygnus.com).

   Copyright 2008-2014 Synopsys Inc.

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "elf-bfd.h"
#include "elf/arc.h"

/* Debug trace for Position independent stuff */
#if 1
#define BFD_DEBUG_PIC(x)
#else

#define BFD_DEBUG_PIC(x) (fprintf(stderr,"DEBUG: %d@%s: ", \
   __LINE__,__PRETTY_FUNCTION__),x)
#endif

/* We must define USE_RELA to get the proper fixups for PC relative
   branches to symbols defined in other object files. The addend is
   used to account for the PC having been incremented before the PC
   relative address is calculated. mlm */
#define USE_RELA

/* The size of the thread control block.  */
#define TCB_SIZE 8
#if 0 /* Get the rest of tls support working first.  */
/* Offset from thread pointer to thread control block.  We use a negative
   offset to make use of the negative part of the 9-bit signed offset range.
   The unscaled range is -256..255.  The data in the thread control block
   can be accessed with scaled addresses, thus we can place it in front.
   ??? We could actually make better use of the offset range if we could
   arrange data items that need 8/16 bit access closer to the thread pointer,
   and use scaled offsets to address other thread local data further out.  */
#define TCB_BASE_OFFSET (-256-TCB_SIZE)
#else
#define TCB_BASE_OFFSET 0
#endif

/* Handle PC relative relocation */
static bfd_reloc_status_type
arc_elf_b22_pcrel (bfd *abfd ATTRIBUTE_UNUSED,
		   arelent *reloc_entry,
		   asymbol *symbol,
		   void *data ATTRIBUTE_UNUSED,
		   asection *input_section,
		   bfd *output_bfd,
		   char **error_message ATTRIBUTE_UNUSED)
{
  /* If incremental linking, update the address of the relocation with the
     section offset */


  if (output_bfd != (bfd *) NULL)
    {
      reloc_entry->address += input_section->output_offset;
      if ((symbol->flags & BSF_SECTION_SYM) && symbol->section)
	reloc_entry->addend
	  += ((**(reloc_entry->sym_ptr_ptr)).section)->output_offset;
      return bfd_reloc_ok;
    }
  return bfd_reloc_continue;
}

#define bfd_put32(a,b,c)
static bfd_vma bfd_get_32_me (bfd *, const unsigned char *);
static void bfd_put_32_me (bfd *, bfd_vma, unsigned char *);


static bfd_reloc_status_type arcompact_elf_me_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type arc_unsupported_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type arc_elf_ignore_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_boolean arc_elf_merge_private_bfd_data (bfd *ibfd, bfd *obfd);
static reloc_howto_type * arc_elf_calculate_howto_index
  (enum elf_arc_reloc_type r_type);


#define INIT_SYM_STRING "_init"
#define FINI_SYM_STRING "_fini"

/* The default symbols representing the init and fini dyn values */
char * init_str = INIT_SYM_STRING;
char * fini_str = FINI_SYM_STRING;

/* The ARC linker needs to keep track of the number of relocs that it
   decides to copy in check_relocs for each symbol.  This is so that
   it can discard PC relative relocs if it doesn't need them when
   linking with -Bsymbolic.  We store the information in a field
   extending the regular ELF linker hash table.  */

/* This structure keeps track of the number of PC relative relocs we
   have copied for a given symbol.  */
#define bfd_elf32_bfd_link_hash_table_create \
					elf_ARC_link_hash_table_create

#define elf_backend_copy_indirect_symbol \
  elf_ARC_copy_indirect_symbol

struct elf_ARC_pcrel_relocs_copied
{
  /* Next section.  */
  struct elf_ARC_pcrel_relocs_copied *next;
  /* A section in dynobj.  */
  asection *section;
  /* Number of reloc6s copied in this section.  */
  bfd_size_type count;
};

enum tls_type_e { GOT_UNKNOWN, GOT_NORMAL, GOT_TLS_GD, GOT_TLS_IE, GOT_TLS_LE };

/* ARC ELF linker hash entry.  */

struct elf_ARC_link_hash_entry
{
  struct elf_link_hash_entry root;

  unsigned char tls_type;
  unsigned force_got : 1;
  /* Number of PC relative relocs copied for this symbol.  */
  struct elf_ARC_pcrel_relocs_copied *pcrel_relocs_copied;
  struct elf_ARC_link_hash_entry *next_deferred;
  struct elf_ARC_link_hash_entry *got_alloc;
};

/* ARC ELF linker hash table.  */

struct elf_ARC_link_hash_table
{
  struct elf_link_hash_table root;

  /* Small local sym to section mapping cache.  */
  struct sym_cache sym_cache;
  struct elf_ARC_link_hash_entry *first_deferred_got;
  struct elf_ARC_link_hash_entry *last_deferred_got;
};

/* Declare this now that the above structures are defined.  */

static bfd_boolean elf_ARC_discard_copies
  (struct elf_ARC_link_hash_entry *, void *);

/* Traverse an ARC ELF linker hash table.  */

#define elf_ARC_link_hash_traverse(table, func, info)			\
  (elf_link_hash_traverse						\
   (&(table)->root,							\
    (bfd_boolean (*) (struct elf_link_hash_entry *, void *)) (func),	\
    (info)))

/* Get the ARC ELF linker hash table from a link_info structure.  */

#define elf_ARC_hash_table(p) \
  ((struct elf_ARC_link_hash_table *) ((p)->hash))

#define elf_arc_first_deferred_got(INFO) \
  elf_ARC_hash_table (INFO)->first_deferred_got
#define elf_arc_last_deferred_got(INFO) \
  elf_ARC_hash_table (INFO)->last_deferred_got

struct elf_arc_obj_tdata
{
  struct elf_obj_tdata root;

  /* tls_type for each local got entry.  */
  unsigned char *local_got_tls_type;
};

#define elf_arc_tdata(abfd) \
  ((struct elf_arc_obj_tdata *) (abfd)->tdata.any)

#define elf_arc_local_got_tls_type(abfd) \
  (elf_arc_tdata (abfd)->local_got_tls_type)

static bfd_boolean
arc_elf_mkobject (bfd *abfd)
{
  return bfd_elf_allocate_object (abfd, sizeof (struct elf_arc_obj_tdata),
                                  ARC_ELF_DATA);
}

/* Create an entry in an ARC ELF linker hash table.  */

static struct bfd_hash_entry *
elf_ARC_link_hash_newfunc (struct bfd_hash_entry *entry,
			   struct bfd_hash_table *table,
			   const char *string)
{
  struct elf_ARC_link_hash_entry *ret =
    (struct elf_ARC_link_hash_entry *) entry;

  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (ret == (struct elf_ARC_link_hash_entry *) NULL)
    ret = ((struct elf_ARC_link_hash_entry *)
	   bfd_hash_allocate (table,
			      sizeof (struct elf_ARC_link_hash_entry)));
  if (ret == (struct elf_ARC_link_hash_entry *) NULL)
    return (struct bfd_hash_entry *) ret;

  /* Call the allocation method of the superclass.  */
  ret = ((struct elf_ARC_link_hash_entry *)
	 _bfd_elf_link_hash_newfunc ((struct bfd_hash_entry *) ret,
				     table, string));
  if (ret != (struct elf_ARC_link_hash_entry *) NULL)
    {
      ret->pcrel_relocs_copied = NULL;
      ret->next_deferred = NULL;
      ret->got_alloc = NULL;
      ret->tls_type = GOT_UNKNOWN;
      ret->force_got = 0;
    }

  return (struct bfd_hash_entry *) ret;
}

/* Create an ARC ELF linker hash table.  */

static struct bfd_link_hash_table *
elf_ARC_link_hash_table_create (bfd * abfd)
{
  struct elf_ARC_link_hash_table *ret;

  ret = ((struct elf_ARC_link_hash_table *)
	 bfd_alloc (abfd, sizeof (struct elf_ARC_link_hash_table)));
  if (ret == (struct elf_ARC_link_hash_table *) NULL)
    return NULL;

  if (! _bfd_elf_link_hash_table_init (&ret->root, abfd,
				       elf_ARC_link_hash_newfunc,
				       sizeof (struct elf_ARC_link_hash_entry),
				       ARC_ELF_DATA))
    {
      bfd_release (abfd, ret);
      return NULL;
    }

  ret->sym_cache.abfd = NULL;
  ret->first_deferred_got = NULL;
  ret->last_deferred_got
    = (struct elf_ARC_link_hash_entry *)
	((char *) &ret->first_deferred_got
	 - offsetof (struct elf_ARC_link_hash_entry, next_deferred));

  return &ret->root.root;
}

static void
elf_ARC_copy_indirect_symbol (struct bfd_link_info *      info,
			      struct elf_link_hash_entry *dir,
			      struct elf_link_hash_entry *ind)
{
  struct elf_ARC_link_hash_entry *adir;
  struct elf_ARC_link_hash_entry *aind;

  _bfd_elf_link_hash_copy_indirect (info, dir, ind);

  adir = (struct elf_ARC_link_hash_entry *) dir;
  aind = (struct elf_ARC_link_hash_entry *) ind;

  if (aind->got_alloc == aind)
    {
      adir->got_alloc = aind->got_alloc;
      adir->next_deferred = NULL;
    }
  if (aind->tls_type != GOT_UNKNOWN)
    adir->tls_type = aind->tls_type;
}

/* This function is called via elf_ARC_link_hash_traverse if we are
   creating a shared object with -Bsymbolic.  It discards the space
   allocated to copy PC relative relocs against symbols which are
   defined in regular objects.  We allocated space for them in the
   check_relocs routine, but we won't fill them in in the
   relocate_section routine.  */

/*ARGSUSED*/
static bfd_boolean
elf_ARC_discard_copies (struct elf_ARC_link_hash_entry * h, void *inf)
{
  struct bfd_link_info *info = (struct bfd_link_info *) inf;
  struct elf_ARC_pcrel_relocs_copied *s;

  /* Discard space allocated for copied relocs if we find out
     we don't need these copies after all.
     When creating an executable, there are no worries about the
     difference between SYMBOL_REFERENCES_LOCAL and SYMBOL_CALLS_LOCAL.
     When creating a shared library, we should have compiled the code
     with -fPIC, so any pc-relative relocs for protected symbols should
     indeed be for calls; pointer loads of global symbols that the
     main program might refer to by their plt address should use
     GOT relocs.  */
  if (!SYMBOL_CALLS_LOCAL (info, &h->root))
    {
      /* m68k / bfin set DT_TEXTREL here, but we have a different way to
	 control add_dynamic_entry.  */

      return TRUE;
    }

  BFD_ASSERT (h->pcrel_relocs_copied == NULL
	      || h->tls_type == GOT_UNKNOWN || h->tls_type == GOT_NORMAL);

  for (s = h->pcrel_relocs_copied; s != NULL; s = s->next)
    s->section->size -=
      s->count * sizeof (Elf32_External_Rela); /* relA */

  return TRUE;
}

/* The HOWTO Array needs to be specified as follows.
   HOWTO
   {
    type        --- > Relocation Type
    rightshift  --- > Rightshift the value by this amount.
    size        --- > Size 0- byte , 1-short, 2 -long
    bitsize     --- > Exact bitsize.
    pcrel       --- > PC Relative reloc.
    bitpos      --- > Bit Position.
    complain_on_overflow ---> What complaint on overflow.
    function    --- > Any special function to be used .
    name        --- > Relocation Name.
    partial_inplace--> Addend sits partially in place and in
		       Reloc Table.
    srcmask       ---> Source Mask 0 for RELA and corresponding
		       field if USE_REL or partial_inplace
		       is set.
    dstmask       ---> Destination Mask . Destination field mask.
    pcreloffset   ---> pcrel offset . If a PCREL reloc is created
		       and the assembler leaves an offset in here.

   }
   If in the backend you need to access the howto array, please
   use the arc_elf_calculate_howto_index function.  All changes in
   the HOWTO array need a corresponding change in the above mentioned
   function. The need for this function is the presence of a hole
   in the ARC ABI.
*/

#define ARC_RELA_HOWTO(type,rightshift,size,bitsz,pcrel,bitpos , \
function,name,dstmask) \
	  \
		       HOWTO( type,rightshift,size,bitsz,pcrel,bitpos,   \
			      complain_overflow_bitfield,function, \
			      name,FALSE,0,dstmask,FALSE)

#define ARCOMPACT_RELA_HOWTO(type,rightshift,size,bitsz,pcrel,bitpos, \
		       function,name,dstmask) \
	  \
		       HOWTO( type,rightshift,size,bitsz,pcrel,bitpos,   \
			      complain_overflow_signed,function, \
			      name,FALSE,0,dstmask,FALSE)



#define ARC_UNSUPPORTED_HOWTO(type,name)  \
 ARC_RELA_HOWTO (type ,0 ,2 ,32,FALSE,0,arc_unsupported_reloc,name,0)

#define ARC_IGNORE_HOWTO(type,name)  \
 ARC_RELA_HOWTO (type, 0, 2, 32, FALSE, 0, arc_elf_ignore_reloc, name, 0)

static reloc_howto_type elf_arc_howto_table[] =
{
  /* This reloc does nothing.  */
  ARC_RELA_HOWTO (R_ARC_NONE ,0 ,2 ,32,FALSE,0,bfd_elf_generic_reloc,
		  "R_ARC_NONE",0),
  ARC_RELA_HOWTO (R_ARC_8    ,0 ,0 , 8,FALSE,0,bfd_elf_generic_reloc,
		  "R_ARC_8" ,0xff),
  ARC_RELA_HOWTO (R_ARC_16   ,0 ,1 ,16,FALSE,0,bfd_elf_generic_reloc,
		  "R_ARC_16",0xffff),
  ARC_RELA_HOWTO (R_ARC_24   ,0 ,2 ,24,FALSE,0,bfd_elf_generic_reloc,
		  "R_ARC_24",0xffffff),
  /* A standard 32 bit relocation.  */
  ARC_RELA_HOWTO (R_ARC_32   ,0 ,2 ,32,FALSE,0,bfd_elf_generic_reloc,
		  "R_ARC_32",-1),
  /* A 26 bit absolute branch, right shifted by 2.  */
  ARC_RELA_HOWTO (R_ARC_B26  ,2 ,2 ,26,FALSE,0,bfd_elf_generic_reloc,
		  "R_ARC_B26",0xffffff),
  /* A relative 22 bit branch; bits 21-2 are stored in bits 26-7.  */
  ARC_RELA_HOWTO (R_ARC_B22_PCREL,2,2,22,TRUE,7,arc_elf_b22_pcrel,
		  "R_ARC_B22_PCREL",0x7ffff80),
  ARC_RELA_HOWTO (R_ARC_H30 ,2 ,2 ,32, FALSE, 0, bfd_elf_generic_reloc,
		  "R_ARC_H30",-1),
  ARC_UNSUPPORTED_HOWTO(R_ARC_N8,"R_ARC_N8"),
  ARC_UNSUPPORTED_HOWTO(R_ARC_N16,"R_ARC_N16"),
  ARC_UNSUPPORTED_HOWTO(R_ARC_N24,"R_ARC_N24"),
  ARC_UNSUPPORTED_HOWTO(R_ARC_N32,"R_ARC_N32"),
  ARC_UNSUPPORTED_HOWTO(R_ARC_SDA,"R_ARC_SDA"),
  ARC_UNSUPPORTED_HOWTO(R_ARC_SECTOFF,"R_ARC_SECTOFF"),

  /* FIXME: Change complaint to complain_overflow_signed.  */
  /* Tangent-A5 relocations.  */
  ARCOMPACT_RELA_HOWTO (R_ARC_S21H_PCREL,1,2,21,TRUE,0,arcompact_elf_me_reloc,
		  "R_ARC_S21H_PCREL",0x7feffc0),
  ARCOMPACT_RELA_HOWTO (R_ARC_S21W_PCREL,2,2,21,TRUE,0,arcompact_elf_me_reloc,
		  "R_ARC_S21W_PCREL",0x7fcffc0),
  ARCOMPACT_RELA_HOWTO (R_ARC_S25H_PCREL,1,2,25,TRUE,0,arcompact_elf_me_reloc,
		  "R_ARC_S25H_PCREL",0x7feffcf),
  ARCOMPACT_RELA_HOWTO (R_ARC_S25W_PCREL,2,2,25,TRUE,0,arcompact_elf_me_reloc,
			"R_ARC_S25W_PCREL",0x7fcffcf),

  ARCOMPACT_RELA_HOWTO (R_ARC_SDA32,0,2,32,FALSE,0,arcompact_elf_me_reloc,
			"R_ARC_SDA32",-1),
  ARCOMPACT_RELA_HOWTO (R_ARC_SDA_LDST,0,2,9,FALSE,15,arcompact_elf_me_reloc,
			"R_ARC_SDA_LDST",0x00ff8000),
  ARCOMPACT_RELA_HOWTO (R_ARC_SDA_LDST1,1,2,10,FALSE,15,arcompact_elf_me_reloc,
			"R_ARC_SDA_LDST1",0x00ff8000),
  ARCOMPACT_RELA_HOWTO (R_ARC_SDA_LDST2,2,2,11,FALSE,15,arcompact_elf_me_reloc,
			"R_ARC_SDA_LDST2",0x00ff8000),

  ARCOMPACT_RELA_HOWTO (R_ARC_SDA16_LD,0,2,9,FALSE,0,arcompact_elf_me_reloc,
			"R_ARC_SDA16_LD",0x01ff),
  ARCOMPACT_RELA_HOWTO (R_ARC_SDA16_LD1,1,2,10,FALSE,0,arcompact_elf_me_reloc,
			"R_ARC_SDA16_LD1",0x01ff),
  ARCOMPACT_RELA_HOWTO (R_ARC_SDA16_LD2,2,2,11,FALSE,0,arcompact_elf_me_reloc,
			"R_ARC_SDA16_LD2",0x01ff),

  ARCOMPACT_RELA_HOWTO (R_ARC_S13_PCREL,2,1,13,TRUE,0,arcompact_elf_me_reloc,
			"R_ARC_S13_PCREL",0x7ff),

  ARC_UNSUPPORTED_HOWTO (R_ARC_W,"R_ARC_W"),

/* 'Middle-endian' (ME) 32-bit word relocations, stored in two half-words.
   The individual half-words are stored in the native endian of the
   machine; this is how all 32-bit instructions and long-words are stored
   in the ARCompact ISA in the executable section.  */

  ARC_RELA_HOWTO (R_ARC_32_ME ,0 ,2 ,32, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_32_ME",-1),

  ARC_UNSUPPORTED_HOWTO (R_ARC_N32_ME,"R_ARC_N32_ME"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_SECTOFF_ME,"R_ARC_SECTOFF_ME"),

  ARCOMPACT_RELA_HOWTO (R_ARC_SDA32_ME,0,2,32,FALSE,0,arcompact_elf_me_reloc,
			"R_ARC_SDA32_ME",-1),

  ARC_UNSUPPORTED_HOWTO (R_ARC_W_ME,"R_ARC_W_ME"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_H30_ME,"R_ARC_H30_ME"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_SECTOFF_U8,"R_ARC_SECTOFF_U8"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_SECTOFF_S9,"R_ARC_SECTOFF_S9"),
  ARC_UNSUPPORTED_HOWTO (R_AC_SECTOFF_U8,"R_AC_SECTOFF_U8"),
  ARC_UNSUPPORTED_HOWTO (R_AC_SECTOFF_U8_1,"R_AC_SECTOFF_U8_1"),
  ARC_UNSUPPORTED_HOWTO (R_AC_SECTOFF_U8_2,"R_ARC_SECTOFF_U8_2"),
  ARC_UNSUPPORTED_HOWTO (R_AC_SECTOFF_S9,"R_AC_SECTOFF_S9"),
  ARC_UNSUPPORTED_HOWTO (R_AC_SECTOFF_S9_1,"R_AC_SECTOFF_S9_1"),
  ARC_UNSUPPORTED_HOWTO (R_AC_SECTOFF_S9_2,"R_AC_SECTOFF_S9_2"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_SECTOFF_ME_1,"R_ARC_SECTOFF_ME_1"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_SECTOFF_ME_2,"R_ARC_SECTOFF_ME_2"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_SECTOFF_1,"R_ARC_SECTOFF_1"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_SECTOFF_2,"R_ARC_SECTOFF_2"),

  ARC_RELA_HOWTO (R_ARC_SDA_12, 0, 2, 12, FALSE, 0, arcompact_elf_me_reloc,
			"R_ARC_SDA_12", 0x0FFF),
  #define R_ARC_hole_base 0x2e
#if 0 /* not yet.  */
  /* There is a gap here of 4.  */
  #define R_ARC_reloc_hole_gap 4

  /* A standard 32 bit pc-relative (data) relocation.  */
  ARC_RELA_HOWTO (R_ARC_32_PCREL, 0, 2, 32, TRUE, 0, bfd_elf_generic_reloc,
                  "R_ARC_32_PCREL",-1),
#else
  /* There is a gap here of 2.  */
  #define R_ARC_reloc_hole_gap 2
#endif
  /* The same as R_ARC_SDA16_LD2 except the bits are laid out differently.  */
  ARC_RELA_HOWTO (R_ARC_SDA16_ST2, 2, 2, 11, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_SDA16_ST2", 0x01FF),

  ARC_UNSUPPORTED_HOWTO (R_ARC_32_PCREL, "R_ARC_32_PCREL"),

  ARC_RELA_HOWTO (R_ARC_PC32, 0, 2, 32, TRUE, 0, arcompact_elf_me_reloc,
		  "R_ARC_PC32",-1),
  /* PC relative was true for this earlier. */
  ARC_RELA_HOWTO (R_ARC_GOTPC32, 0, 2, 32, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_GOTPC32",-1),

  ARC_RELA_HOWTO (R_ARC_PLT32, 0, 2, 32, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_PLT32",-1),

  ARC_RELA_HOWTO (R_ARC_COPY, 0, 2, 32, FALSE,0 , arcompact_elf_me_reloc,
		  "R_ARC_COPY",-1),

  ARC_RELA_HOWTO (R_ARC_GLOB_DAT, 0, 2, 32, FALSE,0 , arcompact_elf_me_reloc,
		  "R_ARC_GLOB_DAT",-1),

  ARC_RELA_HOWTO (R_ARC_JMP_SLOT, 0, 2, 32, FALSE,0 , arcompact_elf_me_reloc,
		  "R_ARC_JMP_SLOT",-1),

  ARC_RELA_HOWTO (R_ARC_RELATIVE, 0, 2, 32, FALSE,0 , arcompact_elf_me_reloc,
		  "R_ARC_RELATIVE",-1),

  ARC_RELA_HOWTO (R_ARC_GOTOFF, 0, 2, 32, FALSE,0 , arcompact_elf_me_reloc,
		  "R_ARC_GOTOFF",-1),

  ARC_RELA_HOWTO (R_ARC_GOTPC, 0, 2, 32, FALSE,0 , arcompact_elf_me_reloc,
		  "R_ARC_GOTPC",-1),
  ARC_UNSUPPORTED_HOWTO (R_ARC_GOT32,"R_ARC_GOT32"),

  /* Implements bl<cc> printf@plt */
  ARC_RELA_HOWTO (R_ARC_S21W_PCREL_PLT, 2, 2, 21, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_S21W_PCREL_PLT",-1),

  /* Implements b printf@plt */
  ARC_RELA_HOWTO (R_ARC_S25H_PCREL_PLT, 1, 2, 25, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_S25H_PCREL_PLT",-1),

  ARC_UNSUPPORTED_HOWTO (R_ARC_SPE_SECTOFF,"R_ARC_SPE_SECTOFF"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_JLI_SECTOFF,"R_ARC_JLI_SECTOFF"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_AOM_TOKEN_ME,"R_ARC_AOM_TOKEN_ME"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_AOM_TOKEN,"R_ARC_AOM_TOKEN"),
  ARC_UNSUPPORTED_HOWTO (R_ARC_TLS_DTPMOD,"R_ARC_TLS_DTPMOD"),
  ARC_RELA_HOWTO (R_ARC_TLS_DTPOFF, 0, 2, 32, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_TLS_DTPOFF",-1),
  ARC_UNSUPPORTED_HOWTO (R_ARC_TLS_TPOFF,"R_ARC_TLS_TPOFF"),
  ARC_RELA_HOWTO (R_ARC_TLS_GD_GOT, 0, 2, 32, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_TLS_GD_GOT",-1),
  ARC_IGNORE_HOWTO (R_ARC_TLS_GD_LD,"R_ARC_TLS_GD_LD"),
  ARC_IGNORE_HOWTO (R_ARC_TLS_GD_CALL,"R_ARC_TLS_GD_CALL"),
  ARC_RELA_HOWTO (R_ARC_TLS_IE_GOT, 0, 2, 32, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_TLS_IE_GOT",-1),
  ARC_RELA_HOWTO (R_ARC_TLS_DTPOFF_S9, 0, 2, 32, FALSE, 0,
		  arcompact_elf_me_reloc, "R_ARC_TLS_DTPOFF_S9",-1),
  ARC_RELA_HOWTO (R_ARC_TLS_LE_S9, 0, 2, 9, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_TLS_LE_S9",-1),
  ARC_RELA_HOWTO (R_ARC_TLS_LE_32, 0, 2, 32, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_TLS_LE_32",-1),

  /* Implements bl printf@plt */
  ARC_RELA_HOWTO (R_ARC_S25W_PCREL_PLT, 2, 2, 25, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_S25W_PCREL_PLT",-1),

  /* Implements b<cc> printf@plt */
  ARC_RELA_HOWTO (R_ARC_S21H_PCREL_PLT, 1, 2, 21, FALSE, 0, arcompact_elf_me_reloc,
		  "R_ARC_S21H_PCREL_PLT",-1),
};

/*Indicates whether the value contained in
  the relocation type is signed, unsigned
  or the reclocation type is unsupported.
  0 -> unsigned reloc type
  1 -> signed reloc type
  -1 -> reloc type unsupported*/
short arc_signed_reloc_type[] =
{
  0, // R_ARC_NONE              Reloc Number
  0, // R_ARC_8
  0, // R_ARC_16
  0, // R_ARC_24
  0, // R_ARC_32
  0, // R_ARC_B26
  1, // R_ARC_B22_PCREL          0x6

  0, // R_ARC_H30                0x7
 -1, // R_ARC_N8
 -1, // R_ARC_N16
 -1, // R_ARC_N24
 -1, // R_ARC_N32
 -1, // R_ARC_SDA
 -1, // R_ARC_SECTOFF            0xD

  1, // R_ARC_S21H_PCREL         0xE
  1, // R_ARC_S21W_PCREL
  1, // R_ARC_S25H_PCREL
  1, // R_ARC_S25W_PCREL         0x11

  1, // R_ARC_SDA32              0x12
  1, // R_ARC_SDA_LDST
  1, // R_ARC_SDA_LDST1
  1, // R_ARC_SDA_LDST2          0x15

  1, // R_ARC_SDA16_LD           0x16
  1, // R_ARC_SDA16_LD1
  1, // R_ARC_SDA16_LD2          0x18

  1, // R_ARC_S13_PCREL          0x19

  -1, // R_ARC_W                 0x1A
  0, // R_ARC_32_ME              0x1B

  -1, // R_ARC_N32_ME            0x1c
  -1, // R_ARC_SECTOFF_ME        0x1D

  0, // R_ARC_SDA32_ME           0x1E

  -1, // R_ARC_W_ME              0x1F
  -1, // R_ARC_H30_ME
  -1, // R_ARC_SECTOFF_U8
  -1, // R_ARC_SECTOFF_S9
  -1, // R_AC_SECTOFF_U8
  -1, // R_AC_SECTOFF_U8_1
  -1, // R_AC_SECTOFF_U8_2
  -1, // R_AC_SECTOFF_S9
  -1, // R_AC_SECTOFF_S9_1
  -1, // R_AC_SECTOFF_S9_2
  -1, // R_ARC_SECTOFF_ME_1
  -1, // R_ARC_SECTOFF_ME_2
  -1, // R_ARC_SECTOFF_1
  -1, // R_ARC_SECTOFF_2         0x2c

  1, // R_ARC_SDA_12             0x2D
  -1, // R_ARC_hole_base starts here 0x2e
  -1, // ends here               0x2f
  1, // R_ARC_SDA16_ST2          0x30
  0, //  R_ARC_32_PCREL		 0x31

  0, //  R_ARC_PC32              0x32
  0, //  R_ARC_GOTPC32
  0, //  R_ARC_PLT32
  0, //  R_ARC_COPY
  0, //  R_ARC_GLOB_DAT
  0, //  R_ARC_JMP_SLOT
  0, //  R_ARC_RELATIVE
  0, //  R_ARC_GOTOFF
  0, //  R_ARC_GOTPC             0x3a
  0, //  R_ARC_GOT32             0x3b

  0, //  R_ARC_S21W_PCREL_PLT   0x3c
  0, //  R_ARC_S25H_PCREL_PLT   0x3d

  0, //  R_ARC_SPE_SECTOFF	0x3e
  0, //  R_ARC_JLI_SECTOFF	0x3f
  0, //  R_ARC_AOM_TOKEN_ME	0x40
  0, //  R_ARC_AOM_TOKEN	0x41

  0, //  R_ARC_TLS_DTPMOD	0x42
  1, //  R_ARC_TLS_DTPOFF	0x43
  0, //  R_ARC_TLS_TPOFF	0x44
  0, //  R_ARC_TLS_GD_GOT	0x45
  0, //  R_ARC_TLS_GD_LD	0x46
  0, //  R_ARC_TLS_GD_CALL	0x47
  0, //  R_ARC_TLS_IE_GOT	0x48
  1, //  R_ARC_TLS_DTPOFF_S9	0x49
  1, //  R_ARC_TLS_LE_S9	0x4a
  1, //  R_ARC_TLS_LE_32	0x4b

  0, //  R_ARC_S25W_PCREL_PLT   0x4c
  0, //  R_ARC_S21H_PCREL_PLT   0x4d
};



static bfd_reloc_status_type
arc_unsupported_reloc (bfd * ibfd ATTRIBUTE_UNUSED,
		       arelent * rel ATTRIBUTE_UNUSED,
		       asymbol * sym ATTRIBUTE_UNUSED,
		       void *ptr ATTRIBUTE_UNUSED,
		       asection * section ATTRIBUTE_UNUSED,
		       bfd *obfd ATTRIBUTE_UNUSED,
		       char ** data ATTRIBUTE_UNUSED
		       )
{
  return bfd_reloc_notsupported;
}


/* Map BFD reloc types to ARC ELF reloc types.  */

struct arc_reloc_map
{
    enum bfd_reloc_code_real bfd_reloc_val;
    enum elf_arc_reloc_type elf_reloc_val;
};

static const struct arc_reloc_map arc_reloc_map[] =
{
  { BFD_RELOC_NONE, R_ARC_NONE },
  { BFD_RELOC_8, R_ARC_8 },
  { BFD_RELOC_16,R_ARC_16 },
  { BFD_RELOC_24, R_ARC_24 },
  { BFD_RELOC_32, R_ARC_32 },
  { BFD_RELOC_CTOR, R_ARC_32 },
  { BFD_RELOC_ARC_B26, R_ARC_B26 },
  { BFD_RELOC_ARC_B22_PCREL, R_ARC_B22_PCREL },
  { BFD_RELOC_ARC_S21H_PCREL, R_ARC_S21H_PCREL },
  { BFD_RELOC_ARC_S21W_PCREL, R_ARC_S21W_PCREL },
  { BFD_RELOC_ARC_S25H_PCREL, R_ARC_S25H_PCREL },
  { BFD_RELOC_ARC_S25W_PCREL, R_ARC_S25W_PCREL },
  { BFD_RELOC_ARC_S13_PCREL, R_ARC_S13_PCREL },
  { BFD_RELOC_ARC_32_ME, R_ARC_32_ME },
#if 0
  { BFD_RELOC_32_PCREL, R_ARC_32_PCREL },
#endif
  { BFD_RELOC_ARC_PC32, R_ARC_PC32 },
  { BFD_RELOC_ARC_GOTPC32, R_ARC_GOTPC32 },
  { BFD_RELOC_ARC_COPY , R_ARC_COPY },
  { BFD_RELOC_ARC_JMP_SLOT, R_ARC_JMP_SLOT },
  { BFD_RELOC_ARC_GLOB_DAT, R_ARC_GLOB_DAT },
  { BFD_RELOC_ARC_GOTOFF , R_ARC_GOTOFF },
  { BFD_RELOC_ARC_GOTPC , R_ARC_GOTPC },
  { BFD_RELOC_ARC_PLT32 , R_ARC_PLT32 },

  { BFD_RELOC_ARC_SDA, R_ARC_SDA },
  { BFD_RELOC_ARC_SDA32, R_ARC_SDA32 },
  { BFD_RELOC_ARC_SDA32_ME, R_ARC_SDA32_ME },
  { BFD_RELOC_ARC_SDA_LDST, R_ARC_SDA_LDST },
  { BFD_RELOC_ARC_SDA_LDST1, R_ARC_SDA_LDST1 },
  { BFD_RELOC_ARC_SDA_LDST2, R_ARC_SDA_LDST2 },
  { BFD_RELOC_ARC_SDA16_LD, R_ARC_SDA16_LD },
  { BFD_RELOC_ARC_SDA16_LD1, R_ARC_SDA16_LD1 },
  { BFD_RELOC_ARC_SDA16_LD2, R_ARC_SDA16_LD2 },
  { BFD_RELOC_ARC_SDA_12,    R_ARC_SDA_12 },
  { BFD_RELOC_ARC_SDA16_ST2, R_ARC_SDA16_ST2 },

  { BFD_RELOC_ARC_TLS_DTPOFF, R_ARC_TLS_DTPOFF },
  { BFD_RELOC_ARC_TLS_GD_GOT, R_ARC_TLS_GD_GOT },
  { BFD_RELOC_ARC_TLS_GD_LD,  R_ARC_TLS_GD_LD },
  { BFD_RELOC_ARC_TLS_GD_CALL,R_ARC_TLS_GD_CALL },
  { BFD_RELOC_ARC_TLS_IE_GOT, R_ARC_TLS_IE_GOT },
  { BFD_RELOC_ARC_TLS_DTPOFF_S9, R_ARC_TLS_DTPOFF_S9 },
  { BFD_RELOC_ARC_TLS_LE_S9,  R_ARC_TLS_LE_S9 },
  { BFD_RELOC_ARC_TLS_LE_32,  R_ARC_TLS_LE_32 },

  { BFD_RELOC_ARC_S21W_PCREL_PLT, R_ARC_S21W_PCREL_PLT },
  { BFD_RELOC_ARC_S25H_PCREL_PLT, R_ARC_S25H_PCREL_PLT },
  { BFD_RELOC_ARC_S25W_PCREL_PLT, R_ARC_S25W_PCREL_PLT },
  { BFD_RELOC_ARC_S21H_PCREL_PLT, R_ARC_S21H_PCREL_PLT },
};

static reloc_howto_type *
arc_elf32_bfd_reloc_type_lookup (bfd *abfd ATTRIBUTE_UNUSED,
				 bfd_reloc_code_real_type code)
{
  unsigned int i;
  for (i = 0;
       i < sizeof (arc_reloc_map) / sizeof (struct arc_reloc_map);
       i++)
    {
      if (arc_reloc_map[i].bfd_reloc_val == code)
	{
	  enum elf_arc_reloc_type r_type;
	  r_type = arc_reloc_map[i].elf_reloc_val;
	  return arc_elf_calculate_howto_index(r_type);
	}
    }

  return NULL;
}

/* Function to set the ELF flag bits.  */
static bfd_boolean
arc_elf_set_private_flags (bfd *abfd, flagword flags)
{
  elf_elfheader (abfd)->e_flags = flags;
  elf_flags_init (abfd) = TRUE;
  return TRUE;
}

/* Print private flags. */
static bfd_boolean
arc_elf_print_private_bfd_data (bfd *abfd, void * ptr)
{
  FILE *file = (FILE *) ptr;
  flagword flags;

  BFD_ASSERT (abfd != NULL && ptr != NULL);

  /* Print normal ELF private data.  */
  _bfd_elf_print_private_bfd_data (abfd, ptr);

  flags = elf_elfheader (abfd)->e_flags;
  fprintf (file, _("private flags = 0x%lx:"), (unsigned long) flags);

  switch (flags & EF_ARC_MACH_MSK)
    {
    case EF_ARC_CPU_GENERIC : fprintf (file, " -mcpu=generic/A4"); break;
    case EF_ARC_CPU_ARCV2HS : fprintf (file, " -mcpu=ARCv2HS");    break;
    case EF_ARC_CPU_ARCV2EM : fprintf (file, " -mcpu=ARCv2EM");    break;
    case E_ARC_MACH_A5      : fprintf (file, " -mcpu=A5");         break;
    case E_ARC_MACH_ARC600  : fprintf (file, " -mcpu=ARC600");     break;
    case E_ARC_MACH_ARC601  : fprintf (file, " -mcpu=ARC601");     break;
    case E_ARC_MACH_ARC700  : fprintf (file, " -mcpu=ARC700");     break;
    default:
      break;
    }

  switch (flags & EF_ARC_OSABI_MSK)
    {
    case E_ARC_OSABI_ORIG : fprintf (file, " (ABI:legacy)"); break;
    case E_ARC_OSABI_V2   : fprintf (file, " (ABI:v2)");     break;
    case E_ARC_OSABI_V3   : fprintf (file, " (ABI:v3)");     break;
    default: break;
    }

  fputc ('\n', file);
  return TRUE;
}

/* Copy backend specific data from one object module to another.  */

static bfd_boolean
arc_elf_copy_private_bfd_data (bfd *ibfd, bfd *obfd)
{
  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return TRUE;

  BFD_ASSERT (!elf_flags_init (obfd)
	      || elf_elfheader (obfd)->e_flags == elf_elfheader (ibfd)->e_flags);

  elf_elfheader (obfd)->e_flags = elf_elfheader (ibfd)->e_flags;
  elf_flags_init (obfd) = TRUE;

  /* Copy object attributes.  */
  _bfd_elf_copy_obj_attributes (ibfd, obfd);

  return TRUE;
}


static reloc_howto_type *
bfd_elf32_bfd_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED,
				 const char *r_name)
{
  unsigned int i;

  for (i = 0;
       i < sizeof (elf_arc_howto_table) / sizeof (elf_arc_howto_table[0]);
       i++)
    if (elf_arc_howto_table[i].name != NULL
	&& strcasecmp (elf_arc_howto_table[i].name, r_name) == 0)
      return &elf_arc_howto_table[i];

  return NULL;
}

/* Calculate the howto index.  */
static reloc_howto_type *
arc_elf_calculate_howto_index(enum elf_arc_reloc_type r_type)
{
  BFD_ASSERT (r_type < (unsigned int) R_ARC_max);
  BFD_ASSERT ((r_type < (unsigned int) R_ARC_hole_base)
	      || (r_type
		  >= (unsigned int) R_ARC_hole_base + R_ARC_reloc_hole_gap));
  if (r_type > R_ARC_hole_base)
    r_type -= R_ARC_reloc_hole_gap;
  return &elf_arc_howto_table[r_type];

}
/* Set the howto pointer for an ARC ELF reloc.  */

static void
arc_info_to_howto_rel (bfd *abfd ATTRIBUTE_UNUSED,
		       arelent *cache_ptr,
		       Elf_Internal_Rela *dst)
{
  enum elf_arc_reloc_type r_type;


  r_type = ELF32_R_TYPE (dst->r_info);
  cache_ptr->howto = arc_elf_calculate_howto_index(r_type);
}

/* Merge backend specific data from an object file to the output
   object file when linking.  */
static bfd_boolean
arc_elf_merge_private_bfd_data (bfd *ibfd, bfd *obfd)
{
  unsigned short mach_ibfd;
  static unsigned short mach_obfd = EM_NONE;
  flagword out_flags;
  flagword in_flags;
  asection *sec;

   /* Check if we have the same endianess.  */
  if (! _bfd_generic_verify_endian_match (ibfd, obfd))
    {
      _bfd_error_handler (
			  _("ERROR: Endian Match failed . Attempting to link %B with binary %s \
of opposite endian-ness"),
			  ibfd, bfd_get_filename (obfd));
      return FALSE;
    }

  /* Collect ELF flags. */
  in_flags = elf_elfheader (ibfd)->e_flags & EF_ARC_MACH_MSK;
  out_flags = elf_elfheader (obfd)->e_flags & EF_ARC_MACH_MSK;

#if DEBUG
  (*_bfd_error_handler) ("out_flags = 0x%.8lx, in_flags = 0x%.8lx, init = %s, filename = %s",
			 out_flags, in_flags, elf_flags_init (obfd) ? "yes" : "no",
			 bfd_get_filename (ibfd));
#endif

  if (!elf_flags_init (obfd))			/* First call, no flags set.  */
    {
      elf_flags_init (obfd) = TRUE;
      out_flags = in_flags;
    }

  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return TRUE;

  /* Check to see if the input BFD actually contains any sections.  If
     not, its flags may not have been initialised either, but it
     cannot actually cause any incompatiblity.  Do not short-circuit
     dynamic objects; their section list may be emptied by
     elf_link_add_object_symbols.  */
  if (!(ibfd->flags & DYNAMIC))
    {
      bfd_boolean null_input_bfd = TRUE;
      bfd_boolean only_data_sections = TRUE;

      for (sec = ibfd->sections; sec != NULL; sec = sec->next)
	{
	  if ((bfd_get_section_flags (ibfd, sec)
	       & (SEC_LOAD | SEC_CODE | SEC_HAS_CONTENTS))
	      == (SEC_LOAD | SEC_CODE | SEC_HAS_CONTENTS))
	    only_data_sections = FALSE;

	  null_input_bfd = FALSE;
	  break;
	}

      if (null_input_bfd || only_data_sections)
	return TRUE;
    }


  /* Complain about various flag/architecture mismatches.  */
  mach_ibfd = elf_elfheader (ibfd)->e_machine;
  if (mach_obfd == EM_NONE)
    {
      mach_obfd = mach_ibfd;
    }
  else
    {
      if(mach_ibfd != mach_obfd)
	{
	  _bfd_error_handler (_("ERROR: Attempting to link %B \
with a binary %s of different architecture"),
			      ibfd, bfd_get_filename (obfd));
	  return FALSE;
	}
      else if (in_flags != out_flags)
	{
	  /* Warn if different flags. */
	  (*_bfd_error_handler)
	    (_("%s: uses different e_flags (0x%lx) fields than previous modules (0x%lx)"),
	     bfd_get_filename (ibfd), (long)in_flags, (long)out_flags);
	  if (in_flags && out_flags)
	    return FALSE;
	  /* MWDT doesnt set the eflags hence make sure we choose the
	     eflags set by gcc.  */
	  in_flags = in_flags > out_flags ? in_flags : out_flags;
	}

    }

  /* Update the flags. */
  elf_elfheader (obfd)->e_flags = in_flags;

  if (bfd_get_mach (obfd) < bfd_get_mach (ibfd))
    {
      return bfd_set_arch_mach (obfd, bfd_arch_arc, bfd_get_mach(ibfd));
    }

  return TRUE;
}

/* Set the right machine number for an ARC ELF file.  */

static bfd_boolean
arc_elf_object_p (bfd *abfd)
{
  /* Make sure this is initialised, or you'll have the potential of
     passing garbage---or misleading values---into the call to
     bfd_default_set_arch_mach().  */
  int mach = bfd_mach_arc_arc700;
  unsigned long arch = elf_elfheader (abfd)->e_flags & EF_ARC_MACH_MSK;
  unsigned e_machine = elf_elfheader (abfd)->e_machine;

  if (e_machine == EM_ARCOMPACT
      || e_machine == EM_ARCV2)
    {
      switch (arch)
	{
	case E_ARC_MACH_ARC600:
	  mach = bfd_mach_arc_arc600;
	  break;
	case E_ARC_MACH_ARC601:
	  mach = bfd_mach_arc_arc601;
	  break;
	case E_ARC_MACH_ARC700:
	  mach = bfd_mach_arc_arc700;
	  break;
	case EF_ARC_CPU_ARCV2HS:
	case EF_ARC_CPU_ARCV2EM:
	  mach = bfd_mach_arc_arcv2;
	  break;
	default:
	  mach = (e_machine == EM_ARCV2) ?
	    bfd_mach_arc_arcv2 : bfd_mach_arc_arc700;
	  break;
	}
    }
  else
    {
      /* This is an old ARC, throw a warning. Probably the best is to
	 return FALSE */
      (*_bfd_error_handler)
	(_("Warning: unset or old architecture flags. \n"
	   "         Use default machine.\n"));
    }

  /* We could return TRUE, but we may as well benefit from a little
   * more sanity-checking done by _bfd_elf_set_arch_mach.  */
  return bfd_default_set_arch_mach (abfd, bfd_arch_arc, mach);
}

/* The final processing done just before writing out an ARC ELF object file.
   This gets the ARC architecture right based on the machine number.  */

static void
arc_elf_final_write_processing (bfd *abfd,
				bfd_boolean linker ATTRIBUTE_UNUSED)
{
  int mach;

  switch (mach = bfd_get_mach (abfd))
    {
    case bfd_mach_arc_a4:
      elf_elfheader (abfd)->e_machine = EM_ARC;
      break;
    case bfd_mach_arc_a5:
      elf_elfheader (abfd)->e_machine = EM_ARCOMPACT;
      break;
    case bfd_mach_arc_arc600:
    case bfd_mach_arc_arc601:
      elf_elfheader (abfd)->e_machine = EM_ARCOMPACT;
      break;
    case bfd_mach_arc_arc700:
      elf_elfheader (abfd)->e_machine = EM_ARCOMPACT;
      break;
    case bfd_mach_arc_arcv2:
      elf_elfheader (abfd)->e_machine = EM_ARCV2;
      break;
    default:
      abort();
    }

  /* Record whatever is the current syscall ABI version */
  elf_elfheader (abfd)->e_flags |= E_ARC_OSABI_CURRENT;
}

/* Handle an ARCompact 'middle-endian' relocation.  */
static bfd_reloc_status_type
arcompact_elf_me_reloc (bfd *abfd ,
			arelent *reloc_entry,
			asymbol *symbol_in,
			void *data,
			asection *input_section,
			bfd *output_bfd,
			char ** error_message ATTRIBUTE_UNUSED)
{
  unsigned long insn;
#ifdef USE_REL
  unsigned long offset
#endif
  bfd_vma sym_value;
  enum elf_arc_reloc_type r_type;
  bfd_vma addr = reloc_entry->address;
  bfd_byte *hit_data = addr + (bfd_byte *) data;

  r_type = reloc_entry->howto->type;

  if (output_bfd != NULL)
    {
      reloc_entry->address += input_section->output_offset;

      /* In case of relocateable link and if the reloc is against a
	 section symbol, the addend needs to be adjusted according to
	 where the section symbol winds up in the output section.  */

      if ((symbol_in->flags & BSF_SECTION_SYM) && symbol_in->section)
	reloc_entry->addend += symbol_in->section->output_offset;

      return bfd_reloc_ok;
    }

  /* Return an error if the symbol is not defined. An undefined weak
     symbol is considered to have a value of zero (SVR4 ABI, p. 4-27). */

  if (symbol_in != NULL && bfd_is_und_section (symbol_in->section)
      && ((symbol_in->flags & BSF_WEAK) == 0))
    return bfd_reloc_undefined;

  if (bfd_is_com_section (symbol_in->section))
    sym_value = 0;
  else
    sym_value = (symbol_in->value
		 + symbol_in->section->output_section->vma
		 + symbol_in->section->output_offset);

  sym_value += reloc_entry->addend;

  if (r_type != R_ARC_32_ME) {
     sym_value -= (input_section->output_section->vma
		+ input_section->output_offset);
     sym_value -= (reloc_entry->address & ~0x3);
  }

  insn = bfd_get_32_me(abfd, hit_data);

  switch(r_type)
  {
    case R_ARC_S21H_PCREL:
#ifdef USE_REL
      /* Retrieve the offset from the instruction, if any.  */
      /* Extract the first 10 bits from Position 6 to 15 in insn.  */
      offset = ((insn << 16) >> 22) << 10;

      /* Extract the remaining 10 bits from Position 17 to 26 in insn.  */
      offset |= ((insn << 5) >> 22);

      /* Fill in 1 bit to get the 21 bit Offset Value. */
      offset = offset << 1;

      /* Ramana : No addends remain in place. */
      /* sym_value += offset; */

#endif /* USE_REL.  */
      /* Extract the instruction opcode alone from 'insn'. */
      insn = insn & 0xf801003f;
      insn |= ((((sym_value >> 1) & 0x3ff) << 17)
	       | (((sym_value >> 1) & 0xffc00) >> 4));
      break;
    case R_ARC_S21W_PCREL:
#ifdef USE_REL
      /* Retrieve the offset from the instruction, if any */
      /* Extract the first 10 bits from Position 6 to 15 in insn */
      offset = ((insn << 16) >> 22) << 9;

      /* Extract the remaining 9 bits from Position 18 to 26 in insn */
      offset |= ((insn << 5) >> 23);

      /* Fill in 2 bits to get the 25 bit Offset Value */
      offset = offset << 2;

      /* No addends remain in place */
      /*       sym_value += offset; */

#endif /* USE_REL. */
      /* Extract the instruction opcode alone from 'insn' */
      insn = insn & 0xf803003f;

      insn |= ((((sym_value >> 2) & 0x1ff) << 18)
	       | (((sym_value >> 2) & 0x7fe00) >> 3));
      break;
    case R_ARC_S25H_PCREL:
#ifdef USE_REL
      /* Retrieve the offset from the instruction, if any */
      /* Extract the high 4 bits from Position 0 to 3 in insn */
      offset = ((insn << 28) >> 28) << 10;

      /* Extract the next 10 bits from Position 6 to 15 in insn */
      offset |= ((insn << 16) >> 22);
      offset = offset << 10;

      /* Extract the remaining 10 bits from Position 17 to 26 in insn */
      offset |= ((insn << 5) >> 22);

      /* Fill in 1 bit to get the 25 bit Offset Value */
      offset = offset << 1;

      /* Ramana : No addends remain in place. */
      /* sym_value += offset; */


#endif /* USE_REL. */
      /* Extract the instruction opcode alone from 'insn' */
      insn = insn & 0xf8010030;

      insn |= ((((sym_value >> 1) & 0x3ff) << 17)
	       | (((sym_value >> 1) & 0xffc00) >> 4)
	       | (((sym_value >> 1) & 0xf00000) >> 20));
      break;

    case R_ARC_S21W_PCREL_PLT:
    case R_ARC_S25H_PCREL_PLT:
    case R_ARC_S25W_PCREL_PLT:
    case R_ARC_S21H_PCREL_PLT:
    case R_ARC_PLT32:
      break;
    case R_ARC_S25W_PCREL:
#ifdef USE_REL
      /* Retrieve the offset from the instruction, if any */
      /* Extract the high 4 bits from Position 0 to 3 in insn */
      offset = ((insn << 28) >> 28) << 10;

      /* Extract the next 10 bits from Position 6 to 15 in insn */
      offset |= ((insn << 16) >> 22);
      offset = offset << 9;

      /* Extract the remaining 9 bits from Position 18 to 26 in insn */
      offset |= ((insn << 5) >> 23);

      /* Fill in 2 bits to get the 25 bit Offset Value */
      offset = offset << 2;

      /* Ramana : No addends remain in place */
      /*      sym_value += offset; */

#endif    /* USE_REL. */
      /* Extract the instruction opcode alone from 'insn' */
      insn = insn & 0xf8030030;

      insn |= ((((sym_value >> 2) & 0x1ff) << 18)
	       | (((sym_value >> 2) & 0x7fe00) >> 3)
	       | (((sym_value >> 2) & 0x780000) >> 19));
      break;
    case R_ARC_S13_PCREL:
#ifdef USE_REL
      /* Retrieve the offset from the instruction, if any */
      /* Extract the 11 bits from Position 0 to 10 in insn */
      offset = (insn << 5) >> 21;

      /* Fill in 2 bits to get the 13 bit Offset Value */
      offset = offset << 2;

      /* No addends remain in place */
      /*      sym_value += offset; */
#endif
      /* Extract the instruction opcode alone from 'insn' */
      insn = (insn & 0xf800ffff);
     insn |= ((sym_value >> 2) & 0x7ff) << 16;
      break;
  case R_ARC_GOTPC32:
  case R_ARC_32_ME:
  case R_ARC_TLS_GD_GOT:
  case R_ARC_TLS_IE_GOT:
  case R_ARC_TLS_LE_32:
  case R_ARC_TLS_DTPOFF:
      insn = sym_value;
      break;
  default:
    return bfd_reloc_notsupported;
    break;
  }

  /* Middle-Endian Instruction Encoding only for executable code */
  /* FIXME:: I am still not sure about this. Ramana . */
  if (input_section && (input_section->flags & SEC_CODE))
    bfd_put_32_me(abfd, insn, hit_data);
  else
    bfd_put_32(abfd, insn, hit_data);

  return bfd_reloc_ok;
}

/* This function is used for relocs which are only used for (tls model)
   relaxing, which the linker should otherwise ignore.  */

static bfd_reloc_status_type
arc_elf_ignore_reloc (bfd *abfd ATTRIBUTE_UNUSED, arelent *reloc_entry,
		     asymbol *symbol ATTRIBUTE_UNUSED,
		     void *data ATTRIBUTE_UNUSED, asection *input_section,
		     bfd *output_bfd,
		     char **error_message ATTRIBUTE_UNUSED)
{
  if (output_bfd != NULL)
    reloc_entry->address += input_section->output_offset;
  return bfd_reloc_ok;
}


static bfd_vma
bfd_get_32_me (bfd * abfd,const unsigned char * data)
{
  bfd_vma value = 0;

  if (bfd_big_endian(abfd)) {
    value = bfd_get_32 (abfd, data);
  }
  else {
    value = ((bfd_get_8 (abfd, data) & 255) << 16);
    value |= ((bfd_get_8 (abfd, data + 1) & 255) << 24);
    value |= (bfd_get_8 (abfd, data + 2) & 255);
    value |= ((bfd_get_8 (abfd, data + 3) & 255) << 8);
  }

  return value;
}

static void
bfd_put_32_me (bfd *abfd, bfd_vma value,unsigned char *data)
{
  bfd_put_16 (abfd, (value & 0xffff0000) >> 16, data);
  bfd_put_16 (abfd, value & 0xffff, data + 2);
}


/* ******************************************
 * PIC-related routines for the arc backend
 * ******************************************/

/* This will be overridden by the interpreter specified in
   the linker specs */
#define ELF_DYNAMIC_INTERPRETER  "/sbin/ld-uClibc.so"

/* size of one plt entry in bytes*/
#define PLT_ENTRY_SIZE  12
#define PLT_ENTRY_SIZE_V2 16

/* Instructions appear in memory as a sequence of half-words (16 bit);
   individual half-words are represented on the target in target byte order.
   We use 'unsigned short' on the host to represent the PLT templates,
   and translate to target byte order as we copy to the target.  */
typedef unsigned short insn_hword;

/* The zeroth entry in the absolute plt entry */
static const insn_hword elf_arc_abs_plt0_entry [2 * PLT_ENTRY_SIZE/2] =
  {
    0x1600,			/* ld %r11, [0] */
    0x700b,
    0x0000,
    0x0000,
    0x1600,			/* ld %r10, [0] */
    0x700a,			/*  */
    0,
    0,
    0x2020,			/* j [%r10] */
    0x0280,			/* ---"---- */
    0x0000,			/* pad */
    0x0000			/* pad */
  };

/* Contents of the subsequent entries in the absolute plt */
static const insn_hword elf_arc_abs_pltn_entry [PLT_ENTRY_SIZE/2] =
  {
    0x2730,			/* ld %r12, [%pc,func@gotpc] */
    0x7f8c,			/* ------ " " -------------- */
    0x0000,			/* ------ " " -------------- */
    0x0000,			/* ------ " " -------------- */
    0x7c20,			/* j_s.d [%r12]              */
    0x74ef			/* mov_s %r12, %pcl          */
  };

/* The zeroth entry in the absolute plt entry for ARCv2 */
static const insn_hword elf_arcV2_abs_plt0_entry [2 * PLT_ENTRY_SIZE_V2/2] =
  {
    0x1600,			/* ld %r11, [0] */
    0x700b,
    0x0000,
    0x0000,
    0x1600,			/* ld %r10, [0] */
    0x700a,			/*  */
    0,
    0,
    0x2020,			/* j [%r10] */
    0x0280,			/* ---"---- */
    0x0000,			/* pad */
    0x0000,			/* pad */
    0x0000,			/* pad */
    0x0000,			/* pad */
    0x0000,			/* pad */
    0x0000			/* pad */
  };

/* Contents of the subsequent entries in the absolute plt for ARCv2 */
static const insn_hword elf_arcV2_abs_pltn_entry [PLT_ENTRY_SIZE_V2/2] =
  {
    0x2730,			/* ld %r12, [%pcl,func@gotpc] */
    0x7f8c,			/* ------ " " --------------  */
    0x0000,			/* ------ " " --------------  */
    0x0000,			/* ------ " " --------------  */
    0x2021,			/* j.d [%r12]                */
    0x0300,			/* ------ " " -------------- */
    0x240a,			/* mov %r12, %pcl             */
    0x1fc0			/* ------ " " --------------  */
  };

/* The zeroth entry in the pic plt entry */
static const insn_hword elf_arc_pic_plt0_entry [2 * PLT_ENTRY_SIZE/2] =
  {
    0x2730,			/* ld %r11, [pcl,0] : 0 to be replaced by _DYNAMIC@GOTPC+4 */
    0x7f8b,
    0x0000,
    0x0000,
    0x2730,			/* ld %r10, [pcl,0] : 0 to be replaced by -DYNAMIC@GOTPC+8  */
    0x7f8a,			/*  */
    0,
    0,
    0x2020,			/* j [%r10] */
    0x0280,			/* ---"---- */
    0x0000,			/* pad */
    0x0000			/* pad */
  };

/* Contents of the subsequent entries in the pic plt */
static const insn_hword elf_arc_pic_pltn_entry [PLT_ENTRY_SIZE/2] =
  {
    0x2730,			/* ld %r12, [%pc,func@got]   */
    0x7f8c,			/* ------ " " -------------- */
    0x0000,			/* ------ " " -------------- */
    0x0000,			/* ------ " " -------------- */
    0x7c20,			/* j_s.d [%r12]              */
    0x74ef,			/* mov_s %r12, %pcl          */
  };

/* The zeroth entry in the pic plt entry for ARCv2 */
static const insn_hword elf_arcV2_pic_plt0_entry [2 * PLT_ENTRY_SIZE_V2/2] =
  {
    0x2730,			/* ld %r11, [pcl,0] : 0 to be replaced by _DYNAMIC@GOTPC+4 */
    0x7f8b,
    0x0000,
    0x0000,
    0x2730,			/* ld %r10, [pcl,0] : 0 to be replaced by -DYNAMIC@GOTPC+8  */
    0x7f8a,			/*  */
    0,
    0,
    0x2020,			/* j [%r10] */
    0x0280,			/* ---"---- */
    0x0000,			/* pad */
    0x0000,			/* pad */
    0x0000,			/* pad */
    0x0000,			/* pad */
    0x0000,			/* pad */
    0x0000			/* pad */
  };

/* Contents of the subsequent entries in the pic plt for ARCv2*/
static const insn_hword elf_arcV2_pic_pltn_entry [PLT_ENTRY_SIZE_V2/2] =
  {
    0x2730,			/* ld %r12, [%pc,func@got]   */
    0x7f8c,			/* ------ " " -------------- */
    0x0000,			/* ------ " " -------------- */
    0x0000,			/* ------ " " -------------- */
    0x2021,			/* j.d [%r12]                */
    0x0300,			/* ------ " " -------------- */
    0x240a,			/* mov %r12, %pcl            */
    0x1fc0			/* ------ " " -------------- */
  };


/* Function: arc_plugin_one_reloc
 * Brief   : Fill in the relocated value of the symbol into an insn
 *           depending on the relocation type. The instruction is
 *           assumed to have been read in the correct format (ME / LE/ BE)
 * Args    : 1. insn              : the original insn into which the relocated
 *                                  value has to be filled in.
             2. r_type            : relocation type after tls transition.
 *           3. value             : the value to be plugged in the insn.
 *           4. overflow_detected : Pointer to short to indicate relocation
 *                                  overflows.
 *           5. symbol_defined    : bool value representing if the symbol
 *                                  definition is present.
 * Returns : the insn with the relocated value plugged in.
 */
unsigned long
arc_plugin_one_reloc (unsigned long insn, enum elf_arc_reloc_type r_type,
                      int value,
                      short *overflow_detected, bfd_boolean symbol_defined);
unsigned long
arc_plugin_one_reloc (unsigned long insn, enum elf_arc_reloc_type r_type,
                      int value,
                      short *overflow_detected, bfd_boolean symbol_defined
		      )
{
  unsigned long offset;
  long long check_overfl_pos,check_overfl_neg;
  reloc_howto_type *howto;

  howto            = arc_elf_calculate_howto_index(r_type);
  int scale = 0;

  if (arc_signed_reloc_type [howto->type] == 1)
    {
      check_overfl_pos = (long long)1 << (howto->bitsize-1);
      check_overfl_neg = -check_overfl_pos;

      if (r_type == R_ARC_PLT32)
	{
	  /* BLcc and Bcc */
	  /* Relocations of the type R_ARC_PLT32 are for the BLcc and Bcc
	     instructions. However the BL/B instruction takes a 25-bit relative
	     displacement while the BLcc/Bcc instruction takes a 21-bit relative
	     displacement. We are using bit-17 to distinguish between these two
	     cases and handle them differently.  */
	  if (! (insn & ((insn & 0x08000000) ? 0x00020000 : 0x00010000)))
	    {
	      check_overfl_pos = 0x100000LL;
	      check_overfl_neg = -check_overfl_pos;
	    }
	}
      else if (r_type == R_ARC_TLS_LE_S9 || r_type == R_ARC_TLS_DTPOFF_S9)
	{
	  /* Extract scale, size and offset fields.  */
	  int aa, zz, addend;
	  if (insn & 0x80000000) /* ld_s */
	    {
	      zz = aa = insn >> 25;
	      addend = (insn >> 16) & 0x1ff;
	    }
	  else if (insn & 0x08000000) /* st */
	    {
	      aa = insn >> 3;
	      zz = insn >> 1;
	      addend = (insn >> 16 & 0xff) | (insn >> 7 & 0x100);
	    }
	  else /* ld */
	    {
	      aa = insn >> 9;
	      zz = insn >> 7;
	      addend = (insn >> 16 & 0xff) | (insn >> 7 & 0x100);
	    }
	  zz &= 3;
	  aa &= 3;
	  if (aa == 0)
	    addend <<= 2;
	  else if (aa == 2)
	    addend <<= 1;
	  if (r_type == R_ARC_TLS_DTPOFF_S9)
	    value += addend;
	  /* Find new scale factor / shift count.  */
	  if (zz == 0)
	    scale = 2;
	  else if (zz == 2)
	    scale = 1;
	  if (value & ((1 << scale) - 1))
	    {
	      if (insn & 0x80000000) /* ld_s */
		*overflow_detected = 1;
	      scale = 0;
	    }
	  else
	    value >>= scale;
	}

      if ((value >= check_overfl_pos) || (check_overfl_neg > value))
	*overflow_detected = 1;
    }
  else
    {
      check_overfl_pos = (long long)1 << (howto->bitsize);
      check_overfl_neg = 0;
      if ((unsigned int) value >= check_overfl_pos)
	*overflow_detected = 1;
    }

    if (*overflow_detected
      && symbol_defined == TRUE)
    {
      (*_bfd_error_handler ) ("Error: Overflow detected in relocation value;");
      if (howto->pc_relative)
	(*_bfd_error_handler) ("Relocation value should be between %lld and %lld whereas it  %d",
			     check_overfl_pos - 1, (signed long long) check_overfl_neg,
			      value);
      else
	(*_bfd_error_handler) ("Relocation value should be between %lld and %lld whereas it  %ld",
			       check_overfl_pos - 1, (signed long long) check_overfl_neg,
			       (unsigned int) value);

      bfd_set_error (bfd_error_bad_value);
      *overflow_detected = 1;
      return 0;
    }
  else
    *overflow_detected = 0;

  switch(r_type)
  {
    case R_ARC_B26:
	/* Retrieve the offset from the instruction, if any */
	/* Extract the last 24 bits from Position 0 to 23 in insn */

      offset = insn & 0x00ffffff;
      /* Fill in 2 bit to get the 26 bit Offset Value */
      offset = offset << 2;


      /* Extract the instruction opcode alone from 'insn' */
      insn = insn & 0xff000000;
      /* With the addend now being in the addend table, there is no
       * need to use this
       */
      /* Ramana : No longer required since
       * addends no longer exist in place
       */
      /*      value += offset; */
      insn |= ((value >> 2) & (~0xff000000));
      break;

    case R_ARC_B22_PCREL:
	/* Retrieve the offset from the instruction, if any */
	/* Extract the first 10 bits from Position 6 to 15 in insn */
	offset = ((insn << 5) >> 12);

	/* Fill in 2 bit to get the 22 bit Offset Value */
	offset = offset << 2;

	/* Extract the instruction opcode alone from 'insn' */
	insn = insn & 0xf800007f;

	/* Ramana: All addends exist in the relocation table. Ignore
	 *  the in place addend
	 */
	/*value += offset; */

	insn |= ((value >> 2) << 7) & (~0xf800007f);

	break;

    case R_ARC_S21H_PCREL:
      /* Retrieve the offset from the instruction, if any */
      /* Extract the first 10 bits from Position 6 to 15 in insn */
      offset = ((insn << 16) >> 22) << 10;

      /* Extract the remaining 10 bits from Position 17 to 26 in insn */
      offset |= ((insn << 5) >> 22);

      /* Fill in 1 bit to get the 21 bit Offset Value */
      offset = offset << 1;

      /* Extract the instruction opcode alone from 'insn' */
      insn = insn & 0xf801003f;



      /* Ramana: All addends exist in the relocation table. Ignore
       *  the in place addend
       */
      /*value += offset; */


      insn |= ((((value >> 1) & 0x3ff) << 17)
	       | (((value >> 1) & 0xffc00) >> 4));
      break;
    case R_ARC_S21W_PCREL:
      /* Retrieve the offset from the instruction, if any */
      /* Extract the first 10 bits from Position 6 to 15 in insn */
      offset = ((insn << 16) >> 22) << 9;

      /* Extract the remaining 9 bits from Position 18 to 26 in insn */
      offset |= ((insn << 5) >> 23);

      /* Fill in 2 bits to get the 25 bit Offset Value */
      offset = offset << 2;

      /* Extract the instruction opcode alone from 'insn' */
      insn = insn & 0xf803003f;

      /* Ramana: All addends exist in the relocation table. Ignore
       *  the in place addend
       */

      /*value += offset;*/


      insn |= ((((value >> 2) & 0x1ff) << 18)
	       | (((value >> 2) & 0x7fe00) >> 3));
      break;
    case R_ARC_S25H_PCREL:
      /* Retrieve the offset from the instruction, if any */
      /* Extract the high 4 bits from Position 0 to 3 in insn */
      offset = ((insn << 28) >> 28) << 10;

      /* Extract the next 10 bits from Position 6 to 15 in insn */
      offset |= ((insn << 16) >> 22);
      offset = offset << 10;

      /* Extract the remaining 10 bits from Position 17 to 26 in insn */
      offset |= ((insn << 5) >> 22);

      /* Fill in 1 bit to get the 25 bit Offset Value */
      offset = offset << 1;

      /* Extract the instruction opcode alone from 'insn' */
      insn = insn & 0xf8010030;

      /* Ramana: All addends exist in the relocation table. Ignore
       *  the in place addend
       */

      /* value += offset; */

      insn |= ((((value >> 1) & 0x3ff) << 17)
	       | (((value >> 1) & 0xffc00) >> 4)
	       | (((value >> 1) & 0xf00000) >> 20));
      break;
  case R_ARC_PLT32:
    BFD_DEBUG_PIC (fprintf(stderr,"plt for %x value=0x%x\n",insn,value));
    /*
      Relocations of the type R_ARC_PLT32 are for the BLcc
      instructions. However the BL instruction takes a 25-bit relative
      displacement while the BLcc instruction takes a 21-bit relative
      displacement. We are using bit-17 to distinguish between these two
      cases and handle them differently.
    */

/* Insns for valid relocs:
   bcc  00000ssssssssss0SSSSSSSSSSNQQQQQ
   b    00000ssssssssss1SSSSSSSSSSNRtttt
   blcc 00001sssssssss00SSSSSSSSSSNQQQQQ
   bl   00001sssssssss10SSSSSSSSSSNRtttt
   Insns for vestigal relocs for __tls_get_addr
   add  00100bbb00000000FBBBCCCCCCAAAAAA A=0 B=0 C=rtp, IE transition
   nop  00100110010010100111000000000000                LE transition.  */
    if (insn
	& ((insn & 0x08000000) ? 0x00020000 : 0x00010000)) /* Non-conditional */
      {
	insn = insn & 0xf8030030;
	insn |= (((value >> 2) & 0x780000) >> 19);
      }
    else if (insn & 0x20000000)
      /* add or nop, leave it alone.  */
      break;
    else
      {
	insn = insn & 0xf803003f;
      }

    insn |= ((((value >> 2) & 0x1ff) << 18)
	     | (((value >> 2) & 0x7fe00) >> 3));
    break;

    /* bl   00001sssssssss10SSSSSSSSSSNRtttt */
  case R_ARC_S25W_PCREL_PLT:
    insn |= ((value >> 2) & 0x01ff) << 18;
    insn |= ((value >> 11) & 0x03ff) << 6;
    insn |= ((value >> 21) & 0x000f) << 0;
    break;
    /* b    00000ssssssssss1SSSSSSSSSSNRtttt */
  case R_ARC_S25H_PCREL_PLT:
    insn |= ((value >> 1) & 0x03ff) << 17;
    insn |= ((value >> 11) & 0x03ff) << 6;
    insn |= ((value >> 21) & 0x000f) << 0;
    break;
    /* blcc 00001sssssssss00SSSSSSSSSSNQQQQQ */
  case R_ARC_S21W_PCREL_PLT:
    insn |= ((value >> 2) & 0x01ff) << 18;
    insn |= ((value >> 11) & 0x03ff) << 6;
    break;
    /* bcc  00000ssssssssss0SSSSSSSSSSNQQQQQ */
  case R_ARC_S21H_PCREL_PLT:
    insn |= ((value >> 1) & 0x03ff) << 17;
    insn |= ((value >> 11) & 0x03ff) << 6;
    break;
  case R_ARC_S25W_PCREL:

       /* Retrieve the offset from the instruction, if any */
       /* Extract the high 4 bits from Position 0 to 3 in insn */
       offset = ((insn << 28) >> 28) << 10;

       /* Extract the next 10 bits from Position 6 to 15 in insn */
       offset |= ((insn << 16) >> 22);
       offset = offset << 9;

       /* Extract the remaining 9 bits from Position 18 to 26 in insn */
       offset |= ((insn << 5) >> 23);

       /* Fill in 2 bits to get the 25 bit Offset Value */
       offset = offset << 2;
      /* Extract the instruction opcode alone from 'insn' */
      insn = insn & 0xf8030030;
      /* Ramana: All addends exist in the relocation table. Ignore
       *  the in place addend
       */

      /* value += offset; 	 */

      insn |= ((((value >> 2) & 0x1ff) << 18)
	       | (((value >> 2) & 0x7fe00) >> 3)
	       | (((value >> 2) & 0x780000) >> 19));
      break;
    case R_ARC_S13_PCREL:
      /* Retrieve the offset from the instruction, if any */
      /* Extract the 11 bits from Position 0 to 10 in insn */
      offset = (insn << 5) >> 21;

      /* Fill in 2 bits to get the 13 bit Offset Value */
      offset = offset << 2;

      /* Extract the instruction opcode alone from 'insn' */
      insn = (insn & 0xf800ffff);

      /* Ramana: All addends exist in the relocation table. Ignore
       *  the in place addend
       */

      /* value += offset; */

      insn |= ((value >> 2) & 0x7ff) << 16;
      break;

  case R_ARC_TLS_DTPOFF:
  case R_ARC_TLS_LE_32:
    insn += value;
    break;

  case R_ARC_32:
  case R_ARC_32_PCREL:
  case R_ARC_GOTPC:
  case R_ARC_GOTOFF:
  case R_ARC_GOTPC32:
  case R_ARC_32_ME:
  case R_ARC_PC32:
  case R_ARC_TLS_GD_GOT:
  case R_ARC_TLS_IE_GOT:
      insn = value;

  case R_ARC_8:
  case R_ARC_16:
  case R_ARC_24:
    /* One would have to OR the value here since
       insn would contain the bits read in correctly. */


    insn |= value ;
      break;

  case R_ARC_SDA32_ME:
    insn |= value;
    break;

  case R_ARC_SDA_LDST2:
    value >>= 1;
  case R_ARC_SDA_LDST1:
    value >>= 1;
  case R_ARC_SDA_LDST:
    value &= 0x1ff;
    if ((insn &  0xF8180000) == 0x50100000)
      {
	/* insert ARCv2 S11 value into ST_S RO,[GP,S11]. */
	insn |= (value & 0x1F8) << 18;
	insn |= (value & 0x07) << 16;
      }
    else
      {
	insn |= ( ((value & 0xff) << 16)  | ((value >> 8) << 15));
      }
    break;

  case R_ARC_SDA16_LD:
    /* FIXME: The 16-bit insns shd not come in as higher bits of a 32-bit word */
    insn |= (value & 0x1ff) <<16;
    break;

  case R_ARC_SDA16_LD1:
    /* FIXME: The 16-bit insns shd not come in as higher bits of a 32-bit word */
    insn |= ((value >> 1) & 0x1ff ) <<16;
    break;

  case R_ARC_SDA16_LD2:
    /* FIXME: The 16-bit insns shd not come in as higher bits of a 32-bit word */
    if ((insn & 0xF8180000) == 0x50000000)
      {
	/* insert ARCv2 S11 value. */
	insn |= ((value >> 2) & 0x1F8) << 18;
	insn |= ((value >> 2) & 0x07) << 16;
      }
    else
      {
	insn |= ((value >> 2) & 0x1ff) <<16;
      }
    break;

  case R_ARC_SDA16_ST2:
    insn |= ((value >> 2) & 0x1F8) << 18;
    insn |= ((value >> 2) & 0x07) << 16;
    break;

  case R_ARC_SDA_12:
    insn |= ((value >> 0) & 0x003f) << 6;
    insn |= ((value >> 6) & 0x003f) << 0;
    break;

  case R_ARC_TLS_LE_S9:
  case R_ARC_TLS_DTPOFF_S9:
    /* Check for ld{,b,w}_s r0,gp,s{11,10,9}; since the reloc is for 32 bit,
       the actual insn will be the upper 16 bit in that case.  */
    if (insn & 0x80000000)
      insn |= (value & 0x1ff) << 16;
    else
      {
	/* Add .as (scaling) modifier.  */
	if (scale)
	  insn |= (insn & 0x08000000) /* st */ ? 0x18 : /* ld */ 0x600;
	insn |= (value & 0xff) << 16 | (value & 0x100) << 7;
      }
    break;

  case R_ARC_NONE:
    break;

  default:
    /* FIXME:: This should go away once the HOWTO Array
       is used for this purpose.
    */
    fprintf(stderr, "Unsupported reloc used : %s (value = %d)\n", (arc_elf_calculate_howto_index(r_type))->name, value);
    break;
  }

  return insn;
}

/* Return the reloc type of REL, taking into account tls type information
   for the symbol described by H.
   If CONTENTS is non-null, adjust the code if we change the reloc type.  */
/* We leave R_ARC_TLS_DTPOFF{,_S9} alone here; if the symbol transitions to
   LE, we'll load the address of .tbss into r0, and then the original *DTPOFF*
   relocs will work just fine (as long as the offset fits).  */
static enum elf_arc_reloc_type
arc_tls_transition (const Elf_Internal_Rela *rel,
		    /* enum tls_type_e */ unsigned char *ttp,
		    bfd *abfd, bfd_byte *contents)
{
  int rtp = 25; /* FIXME: should be able to set rtp by option.  */

  unsigned int r_type = ELF32_R_TYPE (rel->r_info);
  if (ttp) switch (r_type)
    {
    case R_ARC_TLS_IE_GOT:
      if (*ttp == GOT_TLS_LE)
	{
	  if (contents)
	    {
	      /* ld rn,[pcl,symbol@tlsie] -> mov rn,symbol@tpoff */
	      long insn = bfd_get_32_me (abfd, contents + rel->r_offset - 4);
	      /* Verify it's ld a,[pcl,limm] or ld a,[limm,pcl].  */
	      BFD_ASSERT ((insn & 0xfeffff80) == 0x26307f80);
	      insn &= 0x3f;
	      insn = (insn & 7) << 24 | (insn & 56) << 9 | 0x200A0F80;
	      bfd_put_32_me (abfd, insn, contents + rel->r_offset - 4);
	    }
	  return R_ARC_TLS_LE_32;
	}
      break;
    case R_ARC_TLS_GD_GOT:
      if (*ttp == GOT_TLS_LE)
	{
	  if (contents)
	    {
	      long insn = bfd_get_32_me (abfd, contents + rel->r_offset - 4);
              /* Verify it's: add REG,pcl,symbol@tlsgd */
              BFD_ASSERT ((insn & 0xffffffc0) == 0x27007f80);
	      /* add REG,pcl,symbol@tlsgd -> add REG,rtp,symbol@tpoff */
	      insn = ((insn & 0xf8ff8fff)
                      | (rtp & 0x7) << 24
                      | ((rtp >> 3) & 0x7) << 12);
	      bfd_put_32_me (abfd, insn, contents + rel->r_offset - 4);
	    }
	  return R_ARC_TLS_LE_32;
	}
      if (*ttp == GOT_TLS_IE)
	{
	  if (contents)
	    {
	      /* add r0,pcl,symbol@tlsgd -> ld r0,[pcl,symbol@tlsie] */
	      bfd_put_32_me (abfd, 0x27307F80, contents + rel->r_offset - 4);
	    }
	  return R_ARC_TLS_IE_GOT;
	}
      break;
    case R_ARC_TLS_GD_LD:
      if (contents && *ttp != GOT_TLS_GD)
	{
	  bfd_byte *loc = contents + rel->r_offset;
	  bfd_boolean insn_long = (bfd_get_16 (abfd, loc) & 0xf800) <= 0x2000;
	  /* Where the compiler finds to can/(wants to) arrange the
	     dispatch to be movable to be adjacent to the load, it
	     should omit the R_ARC_TLS_GD_CALL reloc on the dispatch
	     and instead put an addend into the R_ARC_TLS_GD_LD reloc
	     to describe the offset to the dispatch.  */
	  BFD_ASSERT ((rel->r_addend & 1) == 0);
	  BFD_ASSERT (!insn_long || rel->r_addend == 0);
	  bfd_boolean short_dispatch
	    = rel->r_addend && (bfd_get_16 (abfd, loc + rel->r_addend) & 0x400);
	  bfd_boolean need_long
	    = (*ttp == GOT_TLS_IE
	       && bfd_get_mach (abfd) == bfd_mach_arc_arcv2
	       && rtp == 30);
	  /* If the dispatch is long - that could be for scheduling reasons -
	     make its second half into a nop_s.  */
	  if (need_long && rel->r_addend && !short_dispatch)
	    bfd_put_16 (abfd, 0x78E0, loc + rel->r_addend + 2);
	  /* If we don't use the dispatch, nop it out.  */
	  /* FIXME: If we have a long dispatch, we could use an add with
	     a LIMM to use up all 6 bytes, potentially saving one cycle.  */
	  else if (!need_long && rel->r_addend > 2)
	    {
	      if (short_dispatch)
		bfd_put_16 (abfd, 0x78E0, loc + rel->r_addend);
	      else
		bfd_put_32_me (abfd, 0x264A7000, loc + rel->r_addend);
	    }
	  if (need_long || short_dispatch)
	    {
	      bfd_vma off;
	      /* If necessary, move the intervening code.  Move the intervening
		 code backward and the location where we'll place the add
		 forward, so that we have a better chance of hiding the
		 load latency.  */
	      for (off = rel->r_addend; off > 2; loc += 2, off -= 2)
		bfd_put_16 (abfd, bfd_get_16 (abfd, loc), loc + 2);
	      BFD_ASSERT (insn_long || rel->r_addend);
	      /* Write a long nop.  */
	      bfd_put_32_me (abfd, 0x264A7000, loc);
	      insn_long = TRUE;
	    }
	  if (*ttp == GOT_TLS_IE)
	    {
	      /* ... -> add r0,r0,rtp ; need long add in case rtp is r30 on v2.  */
	      if (insn_long)
		bfd_put_32_me (abfd, 0x20000000 | rtp << 6,
			       contents + rel->r_offset);
	      else
		bfd_put_16 (abfd, 0x7000 | (rtp & 7) << 5 | (rtp >> 3 & 24),
			    contents + rel->r_offset);
	      return R_ARC_NONE;
	    }
	}
      /* Fall through.  */
    case R_ARC_TLS_GD_CALL:
      if (contents && *ttp != GOT_TLS_GD)
	{
	  /* R_ARC_TLS_GD_LD (obsolete): bl __tls_get_addr@plt -> nop */
	  /* R_ARC_TLS_GD_LD:   ld(_s)... -> nop(_s) */
	  /* R_ARC_TLS_GD_CALL: jl(_s)(.d)-> nop(_s) */
	  if ((bfd_get_16 (abfd, contents + rel->r_offset) & 0xf800) <= 0x2000)
	    bfd_put_32_me (abfd, 0x264A7000, contents + rel->r_offset);
	  else
	    bfd_put_16 (abfd, 0x78E0, contents + rel->r_offset);
	}
      return R_ARC_NONE;
    default:
      break;
    }
  return r_type;
}

/* Return true iff we can resolve rel, a GOTPC32 relocation, using
   pc-relative addressing, assuming the reference is local.
   If install is true, also adjust the opcode accordingly.  */
static bfd_boolean
arc_got_to_pcrel (const Elf_Internal_Rela *rel,
		  bfd *abfd, bfd_byte *contents, bfd_boolean install)
{
  long insn = bfd_get_32_me (abfd, contents + rel->r_offset - 4);
  /* ld rn,[pcl,symbol@tgot] -> add rn,pcl,symbol@pcl */
  insn -= 0x27307F80;
  if ((unsigned long) insn > 62UL)
    {
      (*_bfd_error_handler)
	(_("%B: can't modify insn %x at %x"), abfd, insn, rel->r_offset - 4);
      return FALSE;
    }
  insn += 0x27007F80;
  if (install)
    bfd_put_32_me (abfd, insn, contents + rel->r_offset - 4);
  return TRUE;
}

/* Function : elf_arc_check_relocs
 * Brief    : Check the relocation entries and take any special
 *           actions, depending on the relocation type if needed.
 * Args     : 1. abfd   : The input bfd
 *            2. info   : link information
 *	      3. sec    : section being relocated
 *	      4. relocs : the list of relocations.
 * Returns  : True/False as the return status.
 */
static bfd_boolean
elf_arc_check_relocs (bfd *abfd,
		      struct bfd_link_info *info,
		      asection *sec,
		      const Elf_Internal_Rela *relocs)
{
  bfd *dynobj;
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  bfd_vma *local_got_offsets;
  struct elf_ARC_link_hash_entry *last_deferred_got;
  const Elf_Internal_Rela *rel;
  const Elf_Internal_Rela *rel_end;
  asection *sgot;
  asection *srelgot;
  asection *sreloc;
  Elf_Internal_Sym *isym;

  if (info->relocatable)
    return TRUE;

  dynobj = elf_hash_table (info)->dynobj;
  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (abfd);
  last_deferred_got = elf_arc_last_deferred_got (info);

  sgot = NULL;
  srelgot = NULL;
  sreloc = NULL;

  local_got_offsets = elf_local_got_offsets (abfd);
  if (symtab_hdr->sh_info && local_got_offsets == NULL)
    {
      size_t size;
      register unsigned int i;

      size = symtab_hdr->sh_info * (sizeof (bfd_vma) + sizeof (char));
      local_got_offsets = (bfd_vma *) bfd_alloc (abfd, size);
      if (local_got_offsets == NULL)
	return FALSE;
      elf_local_got_offsets (abfd) = local_got_offsets;
      elf_arc_local_got_tls_type (abfd)
	= (unsigned char *) (local_got_offsets + symtab_hdr->sh_info);

      for (i = 0; i < symtab_hdr->sh_info; i++)
	local_got_offsets[i] = (bfd_vma) -1;
    }

  rel_end = relocs + sec->reloc_count;
  for (rel = relocs; rel < rel_end; rel++)
    {
      unsigned long r_symndx;
      struct elf_link_hash_entry *h;
      BFD_DEBUG_PIC (fprintf(stderr,"Processing reloc #%d in %s\n",
			     rel-relocs,__PRETTY_FUNCTION__));

      r_symndx = ELF32_R_SYM (rel->r_info);

      if (r_symndx < symtab_hdr->sh_info)
	{
	  h = NULL;
	  isym = bfd_sym_from_r_symndx(
	       &elf_ARC_hash_table (info)->sym_cache,
	       abfd, r_symndx);
	}
      else
	{
	  h = sym_hashes[r_symndx - symtab_hdr->sh_info];
	  isym = NULL;
	}

      /* Some relocs require a global offset table.  */
      if (dynobj == NULL)
	{
	  switch (ELF32_R_TYPE (rel->r_info))
	    {
	    case R_ARC_GOTPC32:
	    case R_ARC_GOTOFF:
	    case R_ARC_GOTPC:
	    case R_ARC_TLS_IE_GOT:
	    case R_ARC_TLS_GD_GOT:
	      if (dynobj == NULL)
		{
		  elf_hash_table (info)->dynobj = dynobj = abfd;
		  if (! _bfd_elf_create_got_section (dynobj, info))
		    return FALSE;
		}
	      break;

	    default:
	      break;
	    }
	}

      enum tls_type_e tls_type = GOT_NORMAL;
      struct elf_ARC_link_hash_entry *ah = (struct elf_ARC_link_hash_entry *) h;
      /* enum tls_type_e */ unsigned char *ttp
	= h ? &ah->tls_type : &elf_arc_local_got_tls_type (abfd) [r_symndx];
      enum elf_arc_reloc_type r_type
	= arc_tls_transition (rel, ttp, NULL, NULL);
      bfd_boolean force_got = FALSE;
      switch (r_type)
	{
	case R_ARC_TLS_LE_32:
	case R_ARC_TLS_LE_S9:
	local_exec:
	  tls_type = GOT_TLS_LE;
	local_tls:
	  if (*ttp == GOT_NORMAL)
	    (*_bfd_error_handler)
	      (_("%B: %s' accessed both as normal and thread local symbol"),
		abfd,
		h ? h->root.root.string
		: bfd_elf_sym_name (abfd, symtab_hdr, isym, NULL));
	  *ttp = tls_type;
	  break;

	case R_ARC_TLS_DTPOFF:
	case R_ARC_TLS_DTPOFF_S9:
	  tls_type = GOT_TLS_GD;
	  if (*ttp == GOT_UNKNOWN || *ttp == GOT_NORMAL)
	    goto local_tls;
	  break;

	case R_ARC_TLS_IE_GOT:
	  if (info->shared)
	    info->flags |= DF_STATIC_TLS;
	  else if (!h && info->executable)
	    goto local_exec;
	  tls_type = GOT_TLS_IE;
	  goto tls_create_got;

	case R_ARC_TLS_GD_GOT:
	  tls_type = GOT_TLS_GD;
	tls_create_got:
	  if (!h && info->executable)
	    goto local_exec;
	  goto create_got;

	case R_ARC_GOTPC32:
	/* We'd need to be able to inspect the contents of the input section
	   in order to check if we have a load.
	   Otherwise, if we really need to have @got relocs for instructions
	   that don't fit the mold, we could have a different reloc.  */
#if 0
	  if (!arc_got_to_pcrel (rel, abfd, /* bfd_byte * */contents, false))
	    force_got = 1;
	  if (ah)
	    ah->force_got = force_got;
#endif
	create_got:
	  /* This symbol requires a global offset table entry.  */

	  if (sgot == NULL)
	    {
	      sgot = bfd_get_section_by_name (dynobj, ".got");
	      BFD_ASSERT (sgot != NULL);
	    }

	  if (srelgot == NULL
	      && (h != NULL || info->shared))
	    {
	      srelgot = bfd_get_section_by_name (dynobj, ".rela.got");
	      if (srelgot == NULL)
		{
		  srelgot
		    = bfd_make_section_with_flags (dynobj, ".rela.got",
						   SEC_ALLOC
						   | SEC_LOAD
						   | SEC_HAS_CONTENTS
						   | SEC_IN_MEMORY
						   | SEC_LINKER_CREATED
						   | SEC_READONLY);
		  if (srelgot == NULL
		      || ! bfd_set_section_alignment (dynobj, srelgot, 2))
		    return FALSE;
		}
	    }

	  if (h != NULL)
	    {
	      if (*ttp != tls_type && *ttp != GOT_UNKNOWN)
		{
		  if (tls_type == GOT_TLS_IE && *ttp == GOT_TLS_GD)
		    /* No change - this is ok.  */;
		  else
		    (*_bfd_error_handler)
		      (_("%B: %s' accessed both as normal and thread local symbol"),
		       abfd, h->root.root.string);
		}

	      *ttp = tls_type;
	      if (h->got.offset != (bfd_vma) -1)
		{
		  BFD_DEBUG_PIC(fprintf(stderr, "got entry stab entry already done%d\n",r_symndx));

		  /* We have already allocated space in the .got.  */
		  break;
		}
	      if (ah->next_deferred)
		break;
	      /* Make sure this symbol is output as a dynamic symbol.  */
	      else if (h->dynindx == -1 && !h->forced_local)
		{
		  if (! bfd_elf_link_record_dynamic_symbol (info, h))
		    return FALSE;
		}

	      ah->next_deferred = (struct elf_ARC_link_hash_entry *) -1;
	      ah->got_alloc = ah;
	      last_deferred_got->next_deferred = ah;
	      last_deferred_got = ah;
	      break;
	    }
	  else
	    {
	      *ttp = tls_type;

	      /* Ordinary local GOT references can be turned into
		 pc-relative references.  */
	      if (r_type == R_ARC_GOTPC32 && !force_got)
		break;

     	      /* This is a global offset table entry for a local
                 symbol.  */
	      if (local_got_offsets[r_symndx] != (bfd_vma) -1)
		{
		  BFD_DEBUG_PIC(fprintf(stderr, "got entry stab entry already done%d\n",r_symndx));

		  /* We have already allocated space in the .got.  */
		  break;
		}

	      BFD_DEBUG_PIC(fprintf(stderr, "got entry stab entry %d\n",r_symndx));


	      local_got_offsets[r_symndx] = sgot->size;

	      if (info->shared)
		{
		  /* If we are generating a shared object, we need to
		     output a R_ARC_RELATIVE reloc so that the dynamic
		     linker can adjust this GOT entry.  */
		  srelgot->size += sizeof (Elf32_External_Rela);
		}
	    }

	  BFD_DEBUG_PIC(fprintf (stderr, "Got raw size increased\n"));

	  sgot->size += 4;
	  if (r_type == R_ARC_TLS_GD_GOT)
	    sgot->size += 4;

	  break;

	case R_ARC_S21W_PCREL_PLT:
	case R_ARC_S25H_PCREL_PLT:
	case R_ARC_S25W_PCREL_PLT:
	case R_ARC_S21H_PCREL_PLT:
	case R_ARC_PLT32:
	  /* This symbol requires a procedure linkage table entry.  We
	     actually build the entry in adjust_dynamic_symbol,
	     because this might be a case of linking PIC code which is
	     never referenced by a dynamic object, in which case we
	     don't need to generate a procedure linkage table entry
	     after all.  */

	  /* If this is a local symbol, we resolve it directly without
	     creating a procedure linkage table entry.  */
	  if (h == NULL)
	    continue;

	  h->needs_plt = 1;

	  break;

	case R_ARC_32:
	case R_ARC_32_ME:
	  /* During shared library creation, these relocs should not appear in
	     a shared library (as memory will be read only and the dynamic
	     linker can not resolve these. However the error should not occur
	     for e.g. debugging or non-readonly sections. */
	  if (info->shared && !info->pie
	      && (sec->flags & SEC_ALLOC) != 0
	      && (sec->flags & SEC_READONLY) != 0)
	    {
	      const char *name;
	      if (h)
		name = h->root.root.string;
	      else
		name = bfd_elf_sym_name (abfd, symtab_hdr, isym, NULL);
	      (*_bfd_error_handler)
		(_("%B: relocation %s against `%s' can not be used when making a shared object; recompile with -fPIC"),
		 abfd, arc_elf_calculate_howto_index(
		   ELF32_R_TYPE (rel->r_info))->name, name);
	      bfd_set_error (bfd_error_bad_value);
	      return FALSE;
	    }

          /* In some cases we are not setting the 'non_got_ref' flag, even
             though the relocations don't require a GOT access.  We should
             extend the testing in this area to ensure that no significant
             cases are being missed.  */
          if (h)
            h->non_got_ref = 1;
	  /* FALLTHROUGH */
	case R_ARC_PC32:
	case R_ARC_32_PCREL:
	  /* If we are creating a shared library, and this is a reloc
	     against a global symbol, or a non PC relative reloc
	     against a local symbol, then we need to copy the reloc
	     into the shared library.  However, if we are linking with
	     -Bsymbolic, we do not need to copy a reloc against a
	     global symbol which is defined in an object we are
	     including in the link (i.e., DEF_REGULAR is set).  At
	     this point we have not seen all the input files, so it is
	     possible that DEF_REGULAR is not set now but will be set
	     later (it is never cleared).  We account for that
	     possibility below by storing information in the
	     pcrel_relocs_copied field of the hash table entry.  */
	  if (info->shared
	      && ((r_type != R_ARC_PC32 && r_type != R_ARC_32_PCREL)
		  || (h != NULL
		      && (!info->symbolic || !h->def_regular))))
	    {
	      /* When creating a shared object, we must copy these
		 reloc types into the output file.  We may need to
		 create a reloc section in the dynobj and make room
		 for this reloc.  */
	      if (sreloc == NULL)
		{
		  sreloc = _bfd_elf_make_dynamic_reloc_section (sec, dynobj, 2, abfd, /*rela*/ TRUE);

		  if (sreloc == NULL)
		    return FALSE;
		}

	      sreloc->size += sizeof (Elf32_External_Rela);

	      /* We count the number of PC relative relocations we have
		 entered for this symbol, so that we can discard them
		 again if, in the -Bsymbolic case, the symbol is later
		 defined by a regular object, or, in the normal shared
		 case, the symbol is forced to be local.  Note that this
		 function is only called if we are using an elf_ARC linker
		 hash table, which means that h is really a pointer to an
		 an elf_ARC_link_hash_entry.  */

	      if (r_type == R_ARC_PC32
		  || r_type == R_ARC_32_PCREL)
		{
		  struct elf_ARC_pcrel_relocs_copied *p;
		  struct elf_ARC_pcrel_relocs_copied **head;

		  if (h != NULL)
		    {
		      struct elf_ARC_link_hash_entry *eh
			= (struct elf_ARC_link_hash_entry *) h;
		      head = &eh->pcrel_relocs_copied;
		    }
		  else
		    {
		      asection *s;
		      void *vpp;

		      isym = bfd_sym_from_r_symndx(
			   &elf_ARC_hash_table (info)->sym_cache,
			   abfd, r_symndx);
		      if (isym == NULL)
			return FALSE;

		      s = bfd_section_from_elf_index (abfd, isym->st_shndx);
		      if (s == NULL)
			s = sec;

		      //s = (bfd_section_from_r_symndx
			   //(abfd, &elf_ARC_hash_table (info)->sym_sec,
			    //sec, r_symndx));
		      //if (s == NULL)
			//return FALSE;

		      vpp = &elf_section_data (s)->local_dynrel;
		      head = (struct elf_ARC_pcrel_relocs_copied **) vpp;
		    }

		  for (p = *head; p != NULL; p = p->next)
		    if (p->section == sreloc)
		      break;

		  if (p == NULL)
		    {
		      p = ((struct elf_ARC_pcrel_relocs_copied *)
			   bfd_alloc (dynobj, sizeof *p));
		      if (p == NULL)
			return FALSE;
		      p->next = *head;
		      *head = p;
		      p->section = sreloc;
		      p->count = 0;
		    }

		  ++p->count;
		}
	    }

	  break;

	default:
	  break;
	}

    }

  elf_arc_last_deferred_got (info) = last_deferred_got;

  return TRUE;
}

/* Return the byte alignment for a relocation of type R_TYPE and
   PC_RELATIVE in section INPUT_SECTION.  The returned value should be at
   least 1, indicating any byte alignment is acceptable, otherwise it
   should be the byte alignment number, so 4 indicates alignment to 4
   bytes.  */

static bfd_vma
arc_relocation_byte_alignment (asection *input_section,
                               enum elf_arc_reloc_type r_type,
                               bfd_boolean pc_relative)
{
  BFD_ASSERT (input_section != NULL);

  if (r_type == R_ARC_32)
    return 1;
  else if (r_type==R_ARC_PLT32
           || r_type==R_ARC_GOTPC
           || r_type==R_ARC_GOTPC32
           || r_type == R_ARC_TLS_IE_GOT
           || r_type == R_ARC_TLS_GD_GOT
           || r_type == R_ARC_S21W_PCREL_PLT
           || r_type == R_ARC_S25H_PCREL_PLT
           || r_type == R_ARC_S25W_PCREL_PLT
           || r_type == R_ARC_S21H_PCREL_PLT)
    return 4;
  else if (pc_relative
           && (input_section->flags & SEC_CODE) != 0)
    return 4;
  else
    return 1;
}

/* Relocate an arc ELF section.  */
/* Function : elf_arc_relocate_section
 * Brief    : Relocate an arc section, by handling all the relocations
 *           appearing in that section.
 * Args     : output_bfd    : The bfd being written to.
 *            info          : Link information.
 *            input_bfd     : The input bfd.
 *            input_section : The section being relocated.
 *            contents      : contents of the section being relocated.
 *            relocs        : List of relocations in the section.
 *            local_syms    : is a pointer to the swapped in local symbols.
 *            local_section : is an array giving the section in the input file
 *                            corresponding to the st_shndx field of each
 *                            local symbol.
 * Returns  :
 */
static bfd_boolean
elf_arc_relocate_section (bfd *output_bfd,
			  struct bfd_link_info *info,
			  bfd *input_bfd,
			  asection *input_section,
			  bfd_byte * contents,
			  Elf_Internal_Rela *relocs,
			  Elf_Internal_Sym *local_syms,
			  asection **local_sections)
{
  bfd *dynobj;
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  bfd_vma *local_got_offsets;
  asection *sgot;
  asection *splt;
  asection *sreloc;
  Elf_Internal_Rela *rel;
  Elf_Internal_Rela *relend;
  short overflow_detected=0;

  dynobj = elf_hash_table (info)->dynobj;
  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (input_bfd);
  local_got_offsets = elf_local_got_offsets (input_bfd);

  sgot = NULL;
  splt = NULL;
  sreloc = NULL;

  rel = relocs;
  relend = relocs + input_section->reloc_count;
  for (; rel < relend; rel++)
    {
      enum elf_arc_reloc_type r_type;
      reloc_howto_type *howto;
      unsigned long r_symndx;
      struct elf_link_hash_entry *h;
      Elf_Internal_Sym *sym;
      asection *sec;
      bfd_vma relocation;
      bfd_reloc_status_type r;
      bfd_boolean symbol_defined = TRUE;
      bfd_vma byte_alignment;

      /* Distance of the relocation slot in the insn .This value is used for
	 handling relative relocations. */
      long offset_in_insn = 0;

      /* The insn bytes */
      unsigned long insn;


      r_type = ELF32_R_TYPE (rel->r_info);
      if (r_type >= (int) R_ARC_max)
	{
	  bfd_set_error (bfd_error_bad_value);
	  return FALSE;
	}
      howto = arc_elf_calculate_howto_index(r_type);

      BFD_DEBUG_PIC (fprintf(stderr,"Reloc type=%s in %s\n",
			     howto->name,
			     __PRETTY_FUNCTION__));

      r_symndx = ELF32_R_SYM (rel->r_info);


      if (info->relocatable)
	{
	  /* This is a relocateable link.  We don't have to change
	     anything, unless the reloc is against a section symbol,
	     in which case we have to adjust according to where the
	     section symbol winds up in the output section.  */

	  /* Checks if this is a local symbol
	   * and thus the reloc might (will??) be against a section symbol.
	   */
	  if (r_symndx < symtab_hdr->sh_info)
	    {
	      sym = local_syms + r_symndx;
	      if (ELF_ST_TYPE (sym->st_info) == STT_SECTION)
		{
		  sec = local_sections[r_symndx];

		  /* for RELA relocs.Just adjust the addend
		     value in the relocation entry.  */
		  rel->r_addend += sec->output_offset + sym->st_value;

		  BFD_DEBUG_PIC(fprintf (stderr, "local symbols reloc (section=%d %s) seen in %s\n", \
					 r_symndx,\
					 local_sections[r_symndx]->name, \
					 __PRETTY_FUNCTION__));
		}
	    }

	  continue;
	}

      /* This is a final link.  */
      h = NULL;
      sym = NULL;
      sec = NULL;

      if (r_symndx < symtab_hdr->sh_info)
	{
	  /* This is a local symbol */
	  sym = local_syms + r_symndx;
	  sec = local_sections[r_symndx];
	  relocation = (sec->output_section->vma
			+ sec->output_offset
			+ sym->st_value);

	  /* Mergeable section handling */
	  if ((sec->flags & SEC_MERGE)
	      && ELF_ST_TYPE (sym->st_info) == STT_SECTION)
	    {
	      asection *msec;
	      msec = sec;
	      rel->r_addend = _bfd_elf_rel_local_sym (output_bfd, sym,
						      &msec, rel->r_addend);
	      rel->r_addend -= relocation;
	      rel->r_addend += msec->output_section->vma + msec->output_offset;
	    }

	  relocation += rel->r_addend;
	  r_type = arc_tls_transition
		    (rel, &elf_arc_local_got_tls_type (input_bfd) [r_symndx],
		     output_bfd, contents);
	}
      else
	{
	  /* Global symbols */

	  /* get the symbol's entry in the symtab */
	  h = sym_hashes[r_symndx - symtab_hdr->sh_info];

	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;

	  BFD_ASSERT ((h->dynindx == -1) >= (h->forced_local != 0));
	  struct elf_ARC_link_hash_entry *ah
	    = (struct elf_ARC_link_hash_entry *) h;
	  r_type
	    = arc_tls_transition (rel, &ah->tls_type, output_bfd, contents);
	  /* if we have encountered a definition for this symbol */
	  if (h->root.type == bfd_link_hash_defined
	      || h->root.type == bfd_link_hash_defweak)
	    {
	      sec = h->root.u.def.section;
	      if (r_type == R_ARC_GOTPC
		  || ((r_type == R_ARC_PLT32
		       || r_type == R_ARC_S21W_PCREL_PLT
		       || r_type == R_ARC_S25H_PCREL_PLT
		       || r_type == R_ARC_S25W_PCREL_PLT
		       || r_type == R_ARC_S21H_PCREL_PLT)
		      && h->plt.offset != (bfd_vma) -1)
		  || (((r_type == R_ARC_GOTPC32
			&& h != NULL && !SYMBOL_REFERENCES_LOCAL (info, h))
		       || r_type == R_ARC_TLS_IE_GOT
		       || r_type == R_ARC_TLS_GD_GOT
		       /* R_ARC_TLS_GD_{LD,CALL} -> R_ARC_NONE */
		       || r_type == R_ARC_NONE)
		      && elf_hash_table (info)->dynamic_sections_created
		      && (! info->shared
			  || !(_bfd_elf_symbol_refs_local_p
				(h, info, r_type == R_ARC_PLT32))
			  || !(_bfd_elf_symbol_refs_local_p
			       (h, info, r_type == R_ARC_S21W_PCREL_PLT))
			  || !(_bfd_elf_symbol_refs_local_p
			       (h, info, r_type == R_ARC_S25H_PCREL_PLT))
			  || !(_bfd_elf_symbol_refs_local_p
			       (h, info, r_type == R_ARC_S25W_PCREL_PLT))
			  || !(_bfd_elf_symbol_refs_local_p
			       (h, info, r_type == R_ARC_S21H_PCREL_PLT))
			  ))
		  || (info->shared
		      && ((! info->symbolic && h->dynindx != -1)
			  || !h->def_regular)
		      && (r_type == R_ARC_32
			  || r_type == R_ARC_PC32
			  || r_type == R_ARC_32_PCREL)
		      && (input_section->flags & SEC_ALLOC) != 0))
		{
		  /* In these cases, we don't need the relocation
		     value.  We check specially because in some
		     obscure cases sec->output_section will be NULL.  */
		  relocation = 0;
		}
	      else if (sec->output_section == NULL)
		{
		  (*_bfd_error_handler)
		    ("%s: warning: unresolvable relocation against symbol `%s' from %s section",
		     bfd_get_filename (input_bfd), h->root.root.string,
		     bfd_get_section_name (input_bfd, input_section));
		  relocation = 0;
		}
	      else
		{
		  relocation = (h->root.u.def.value
				+ sec->output_section->vma
				+ sec->output_offset);
		  /* add the addend since the arc has RELA relocations */
		  relocation += rel->r_addend;
		}
	    }
	  else if (h->root.type == bfd_link_hash_undefweak)
	    {
	      symbol_defined = FALSE;
	      relocation = 0;
	    }
	  else if (info->shared && !info->symbolic)
	    relocation = 0;
	  else
	    {
	      if (! ((*info->callbacks->undefined_symbol)
		     (info, h->root.root.string,
		      input_bfd, input_section, rel->r_offset, !info->shared)))
		return FALSE;
	      symbol_defined = FALSE;
	      relocation = 0;
	    }
	}
      BFD_DEBUG_PIC ( fprintf (stderr, "Relocation = %d (%x)\n", relocation, relocation));


      switch (r_type)
	{
	case R_ARC_TLS_IE_GOT:
	case R_ARC_TLS_GD_GOT:
	  /* We don't care about the value of RELOCATION here in the non-local
	     case, as it'll be replaced just below, but
	     for local IE variables, the offset of the tls var from this
	     module's tls block start will get put in the GOT.  */
	  if (elf_hash_table (info)->tls_sec)
	    relocation -= elf_hash_table (info)->tls_sec->output_section->vma;
	  /* Fall through.  */
	case R_ARC_GOTPC32:
	  /* Relocation is to the entry for this symbol in the global
	     offset table.  */
	  if (sgot == NULL)
	    {
	      sgot = bfd_get_section_by_name (dynobj, ".got");
	      BFD_DEBUG_PIC (fprintf (stderr, "made got\n"));
	      BFD_ASSERT (sgot != NULL);
	    }

	  if (r_type == R_ARC_GOTPC32
	      && (h == NULL || SYMBOL_REFERENCES_LOCAL (info, h))
	      && arc_got_to_pcrel (rel, output_bfd, contents, TRUE))
	    ; /* We'll resolve directly against the local symbol.  */
	  else if (h != NULL)
	    {
	      bfd_vma off;

	      off = h->got.offset;
	      BFD_ASSERT (off != (bfd_vma) -1);

	      if (! elf_hash_table (info)->dynamic_sections_created
		  || (info->shared
		      && SYMBOL_REFERENCES_LOCAL (info, h)))
		{
		  /* This is actually a static link, or it is a
		     -Bsymbolic link and the symbol is defined
		     locally, or the symbol was forced to be local
		     because of a version file.  We must initialize
		     this entry in the global offset table.  Since the
		     offset must always be a multiple of 4, we use the
		     least significant bit to record whether we have
		     initialized it already.

		     When doing a dynamic link, we create a .rela.got
		     relocation entry to initialize the value.  This
		     is done in the finish_dynamic_symbol routine.  */
		  if ((off & 1) != 0)
		    off &= ~1;
		  else
		    {
		      bfd_put_32 (output_bfd, relocation,
				  sgot->contents + off
				  + (r_type == R_ARC_TLS_GD_GOT ? 4 : 0));
		      h->got.offset |= 1;
		    }
		}

	      relocation = sgot->output_section->vma + sgot->output_offset + off;
	      BFD_DEBUG_PIC(fprintf(stderr, "OFFSET=0x%x output_offset=%x (1)\n", off, sgot->output_offset));
	    }
	  else
	    {
	      bfd_vma off;

	      BFD_ASSERT (local_got_offsets != NULL
			  && local_got_offsets[r_symndx] != (bfd_vma) -1);

	      off = local_got_offsets[r_symndx];

	      /* The offset must always be a multiple of 4.  We use
		 the least significant bit to record whether we have
		 already generated the necessary reloc.  */
	      if ((off & 1) != 0)
		off &= ~1;
	      else
		{
		  if (info->shared)
		    {
		      asection *srelgot;
		      Elf_Internal_Rela outrel;
		      bfd_byte *loc;

		      srelgot = bfd_get_section_by_name (dynobj, ".rela.got");
		      BFD_ASSERT (srelgot != NULL);

		      outrel.r_offset = (sgot->output_section->vma
					 + sgot->output_offset
					 + off);
		      /* RELA relocs */
		      switch (r_type)
			{
			case R_ARC_GOTPC32:
			  outrel.r_addend = 0; //PBB??
			  outrel.r_info = ELF32_R_INFO (0, R_ARC_RELATIVE);
			  break;
			case R_ARC_TLS_IE_GOT:
			  outrel.r_addend =  relocation;
			  outrel.r_info = ELF32_R_INFO (0, R_ARC_TLS_TPOFF);
			  relocation = 0;
			  break;
			case R_ARC_TLS_GD_GOT:
			  /* We use a symbol index of 0 to mean current
			     module's dtv index / descriptor.  */
			  outrel.r_info = ELF32_R_INFO (0, R_ARC_TLS_DTPMOD);
			  outrel.r_addend = 0;
			  bfd_put_32 (output_bfd, relocation,
				      sgot->contents + off + 4);
			  relocation = 0;
			  break;
			default:
;
			  (*_bfd_error_handler)
			    (_("%B: Error: unsupported GOT relocation against local symbol \"%s\""),
			     input_bfd,
			     bfd_elf_sym_name (input_bfd, symtab_hdr, sym,
					       sec));
			  outrel.r_info = ELF32_R_INFO (0, R_ARC_NONE);
			  outrel.r_addend =  relocation = 0;
			}

		      loc = srelgot->contents;
		      loc += srelgot->reloc_count++ * sizeof (Elf32_External_Rela); /* relA */
		      bfd_elf32_swap_reloca_out (output_bfd, &outrel, loc);
		    }

		  bfd_put_32 (output_bfd, relocation, sgot->contents + off);

		  local_got_offsets[r_symndx] |= 1;
		}

	      relocation = sgot->output_section->vma + sgot->output_offset + off;
	      BFD_DEBUG_PIC(fprintf(stderr, "OFFSET=0x%x (2)\n", off));
	    }

	  BFD_DEBUG_PIC(fprintf(stderr, "RELOCATION =%x\n",relocation));
	  /* the data in GOT32 relocs is 4 bytes into the insn */
	  offset_in_insn = 4;

	  break;

	case R_ARC_GOTOFF:
	  /* Relocation is relative to the start of the global offset
	     table.  */

	  if (sgot == NULL)
	    {
	      sgot = bfd_get_section_by_name (dynobj, ".got");
	      BFD_ASSERT (sgot != NULL);
	    }

	  /* Note that sgot->output_offset is not involved in this
	     calculation.  We always want the start of .got.  If we
	     defined _GLOBAL_OFFSET_TABLE in a different way, as is
	     permitted by the ABI, we might have to change this
	     calculation.  */
	  BFD_DEBUG_PIC(fprintf(stderr,"GOTOFF relocation = %x. Subtracting %x\n",relocation, sgot->output_section->vma));
	  relocation -= sgot->output_section->vma;

	  break;

	case R_ARC_GOTPC:
	  /* Use global offset table as symbol value.  */

	  if (sgot == NULL)
	    {
	      sgot = bfd_get_section_by_name (dynobj, ".got");
	      BFD_ASSERT (sgot != NULL);
	    }

	  relocation = sgot->output_section->vma;

	  offset_in_insn = 4;
	  break;

	case R_ARC_S21W_PCREL_PLT:
	case R_ARC_S25H_PCREL_PLT:
	case R_ARC_S25W_PCREL_PLT:
	case R_ARC_S21H_PCREL_PLT:
	case R_ARC_PLT32:
	  /* Relocation is to the entry for this symbol in the
	     procedure linkage table.  */

	  /* Resolve a PLT32 reloc again a local symbol directly,
	     without using the procedure linkage table.  */
	  if (h == NULL)
	    break;

	  if (h->plt.offset == (bfd_vma) -1)
	    {
	      /* We didn't make a PLT entry for this symbol.  This
		 happens when statically linking PIC code, or when
		 using -Bsymbolic.  */
	      break;
	    }

	  if (splt == NULL)
	    {
	      splt = bfd_get_section_by_name (dynobj, ".plt");
	      BFD_ASSERT (splt != NULL);
	    }

	  relocation = (splt->output_section->vma
			+ splt->output_offset
			+ h->plt.offset);

	  break;

	case R_ARC_32:
	case R_ARC_32_ME:
	case R_ARC_PC32:
	case R_ARC_32_PCREL:
	  if (info->shared
	      && (r_type != R_ARC_PC32
		  || (h != NULL
		      && h->dynindx != -1
		      && (!info->symbolic || !h->def_regular))))
	    {
	      Elf_Internal_Rela outrel;
	      bfd_boolean skip, relocate;
	      bfd_byte *loc;

	      /* When generating a shared object, these relocations
		 are copied into the output file to be resolved at run
		 time.  */

	      if (sreloc == NULL)
		{
		  sreloc = _bfd_elf_get_dynamic_reloc_section
		    (input_bfd, input_section, /*RELA*/ TRUE);

		  BFD_ASSERT (sreloc != NULL);
		}

	      skip = FALSE;

	      outrel.r_offset = _bfd_elf_section_offset (output_bfd,
							 info,
							 input_section,
							 rel->r_offset);
	      if (outrel.r_offset == (bfd_vma) -1)
		  skip = TRUE;

	      outrel.r_addend = rel->r_addend;
	      outrel.r_offset += (input_section->output_section->vma
				  + input_section->output_offset);

	      if (skip)
		{
		  memset (&outrel, 0, sizeof outrel);
		  relocate = FALSE;
		}
	      else if (r_type == R_ARC_PC32
		       || r_type == R_ARC_32_PCREL)
		{
		  BFD_ASSERT (h != NULL && h->dynindx != -1);
		  if ((input_section->flags & SEC_ALLOC) != 0)
		    relocate = FALSE;
		  else
		    relocate = TRUE;
		  outrel.r_info = ELF32_R_INFO (h->dynindx, r_type);
		}
	      else
		{
                  /* Handle local symbols, they either do not have a global
                     hash table entry (h == NULL), or are forced local due
                     to a version script (h->forced_local), or the third
                     condition is legacy, it appears to say something like,
                     for links where we are pre-binding the symbols, or
                     there's not an entry for this symbol in the dynamic
                     symbol table, and it's a regular symbol not defined in
                     a shared object, then treat the symbol as local,
                     resolve it now.  */
		  if (h == NULL
		      || ((info->symbolic || h->dynindx == -1)
			  && h->def_regular)
		      || h->forced_local)
		    {
		      relocate = TRUE;
		      /* outrel.r_addend = 0; */
		      outrel.r_info = ELF32_R_INFO (0, R_ARC_RELATIVE);
		    }
		  else
		    {
		      BFD_ASSERT (h->dynindx != -1);
		      if ((input_section->flags & SEC_ALLOC) != 0)
			relocate = FALSE;
		      else
			relocate = TRUE;
		      outrel.r_info = ELF32_R_INFO (h->dynindx, R_ARC_32);
		    }
		}

	      BFD_ASSERT(sreloc->contents != 0);

	      loc = sreloc->contents;
	      loc += sreloc->reloc_count++ * sizeof (Elf32_External_Rela); /* relA */

	      bfd_elf32_swap_reloca_out (output_bfd, &outrel, loc);

	      /* If this reloc is against an external symbol, we do
		 not want to fiddle with the addend.  Otherwise, we
		 need to include the symbol value so that it becomes
		 an addend for the dynamic reloc.  */
	      if (! relocate)
		continue;
	    }

	  /* PLT32 has to be w.r.t the instruction's start */
	  offset_in_insn = 0;
	  break;

	case R_ARC_B22_PCREL:
	  /* 'offset_in_insn' in case of the A4 is from the instruction in
	     the delay slot of the branch instruction hence the -4 offset.  */
	  offset_in_insn = -4;
	  break;

	case R_ARC_SDA32_ME:

	case R_ARC_SDA_LDST:
	case R_ARC_SDA_LDST1:
	case R_ARC_SDA_LDST2:

	case R_ARC_SDA16_LD:
	case R_ARC_SDA16_LD1:
	case R_ARC_SDA16_LD2:

	case R_ARC_SDA16_ST2:
	case R_ARC_SDA_12:
	  {
	    /* Get the base of .sdata section */
	    struct elf_link_hash_entry *h2;

	    h2 = elf_link_hash_lookup (elf_hash_table (info), "__SDATA_BEGIN__",
				      FALSE, FALSE, TRUE);

	    if (h2 == NULL || h2->root.type == bfd_link_hash_undefined)
	    {
	      (*_bfd_error_handler)("Error: Linker symbol __SDATA_BEGIN__ not found");
	      bfd_set_error (bfd_error_bad_value);
	      return FALSE;
	    }

	    /* Subtract the address of __SDATA_BEGIN__ from the relocation value */
	    ///	    fprintf (stderr, "relocation BEFORE = 0x%x SDATA_BEGIN = 0x%x\n", relocation, h->root.u.def.value);
	    relocation -= (h2->root.u.def.value + h2->root.u.def.section->output_section->vma);
	    //	    fprintf (stderr, "relocation AFTER = 0x%x SDATA_BEGIN = 0x%x\n", relocation, h2->root.u.def.value);
	    break;
	  }

	case R_ARC_TLS_LE_32:
	case R_ARC_TLS_LE_S9:
	  /* The value we have is inside the .tbss section; we want
	     it to be relative to the thread pointer.  */
	  relocation += TCB_BASE_OFFSET + TCB_SIZE;
	  /* Fall through.  */

	case R_ARC_TLS_DTPOFF:
	case R_ARC_TLS_DTPOFF_S9:
	  /* Undo the addition from above.  */
	  relocation -= rel->r_addend;
	  if (rel->r_addend == STN_UNDEF)
	    {
	      /* The value we have is inside the .tbss section; we want
		 it to be relative to the .tbss start.  */
	      relocation -= elf_hash_table (info)->tls_sec->output_section->vma;
	      break;
	    }
	  asection *b_sec;
	  unsigned long b_symndx = rel->r_addend;
	  /* Now find the base symbol that's encoded in the addend.  */
	  if (b_symndx < symtab_hdr->sh_info)
	    {
	      /* This is a local symbol.  */
	      Elf_Internal_Sym *b_sym = local_syms + b_symndx;
	      b_sec = local_sections[b_symndx];
	      relocation -= b_sym->st_value;
	    }
	  else
	    {
	      /* Global symbol.  */
	      /* get the symbol's entry in the symtab */
	      struct elf_link_hash_entry *b_h
		= sym_hashes[b_symndx - symtab_hdr->sh_info];
	      while (b_h->root.type == bfd_link_hash_indirect
		     || b_h->root.type == bfd_link_hash_warning)
		b_h = (struct elf_link_hash_entry *) b_h->root.u.i.link;
	      BFD_ASSERT ((b_h->dynindx == -1) >= (b_h->forced_local != 0));
	      b_sec = b_h->root.u.def.section;

	      if (b_sec->output_section == NULL)
		{
		  (*_bfd_error_handler)
		    ("%s: warning: unresolvable relocation against symbol `%s' from %s section",
		     bfd_get_filename (input_bfd), b_h->root.root.string,
		     bfd_get_section_name (input_bfd, input_section));
		  continue;
		}
	      relocation -= b_h->root.u.def.value;
	    }
	  relocation -= b_sec->output_section->vma + b_sec->output_offset;
	  break;

	default:
	  /* FIXME: Putting in a random dummy relocation value for the time being */
	  //	  fprintf (stderr, "In %s, relocation = 0x%x,  r_type = %d\n", __PRETTY_FUNCTION__, relocation, r_type);
	  break;
	}


      /* get the insn bytes here */
      if(elf_elfheader(input_bfd)->e_machine == EM_ARC)
	insn = bfd_get_32 (input_bfd, contents + rel->r_offset);
      else
	if (input_section && (input_section->flags & SEC_CODE)
	    && r_type != R_ARC_32_PCREL)
	  insn = bfd_get_32_me (input_bfd, contents + rel->r_offset);
	else
	  insn = bfd_get_32 (input_bfd, contents + rel->r_offset);

      BFD_DEBUG_PIC(fprintf(stderr, "relocation before the pc relative stuff @offset 0x%x= %d[0x%x]\n",
			    rel->r_offset,relocation, relocation));

      BFD_DEBUG_PIC(fprintf(stderr,"addend = 0x%x\n",rel->r_addend));

      /* Some pc-relative relocations patch the relocation assuming a pc
         value rounded down to a particular byte boundary.  */
      byte_alignment =
        arc_relocation_byte_alignment (input_section, r_type,
                                       howto->pc_relative);
      BFD_ASSERT (byte_alignment >= 1);
      /* In the following `if` check, it should be enough to check that the
         relocation is pc-relative.  However, not all of the relocations
         that need to be handled here are correctly marked as pc-relative
         in their howto structure.  Due to an upcoming deadline (June 2015)
         and the possible risk of changing several relocations to be
         pc-relative I have added in the additional check of BYTE_ALIGNMENT
         here, however, this should be considered a temporary hack.  At
         some point in the near future we should correctly mark the
         required relocations as pc-relative and remove the extra check of
         BYTE_ALIGNMENT here.  */
      if (howto->pc_relative || byte_alignment > 1)
        relocation -= (((input_section->output_section->vma
                         + input_section->output_offset
                         + rel->r_offset) & ~(byte_alignment - 1))
                       - offset_in_insn);

      BFD_DEBUG_PIC(fprintf(stderr, "relocation AFTER the pc relative handling = %d[0x%x]\n", relocation, relocation));

      /* What does the modified insn look like */
      insn = arc_plugin_one_reloc (insn, r_type, relocation,
				   &overflow_detected, symbol_defined);

      if (overflow_detected)
	{
	  if(h)
	    (*_bfd_error_handler) ("Global symbol: \"%s\".", h->root.root.string);
	  else
	    (*_bfd_error_handler) ("Local symbol: \"%s\".", local_sections[r_symndx]->name);
	  (*_bfd_error_handler) ("\nRelocation type is:%s \nFileName:%s \
			     \nSection Name:%s\
			     \nOffset in Section:%ld", howto->name, bfd_get_filename (input_bfd),
			     bfd_get_section_name (input_bfd, input_section),
			     rel->r_offset);

	  return FALSE;
	}

      BFD_DEBUG_PIC (fprintf (stderr, "Relocation = %d [0x%x]\n", (int)relocation, (unsigned)relocation));

      /* now write back into the section, with middle endian encoding
	 only for executable section */
      if(elf_elfheader(input_bfd)->e_machine == EM_ARC)
	bfd_put_32 (input_bfd, insn, contents + rel->r_offset);
      else
	if (input_section && (input_section->flags & SEC_CODE)
	    && r_type != R_ARC_32_PCREL)
	  bfd_put_32_me (input_bfd, insn, contents + rel->r_offset);
	else
	  bfd_put_32 (input_bfd, insn, contents + rel->r_offset);

      r = bfd_reloc_ok;


      if (r != bfd_reloc_ok)
	{
	  switch (r)
	    {
	    default:
	    case bfd_reloc_outofrange:
	      abort ();
	    case bfd_reloc_overflow:
	      {
		const char *name;

		if (h != NULL)
		  name = h->root.root.string;
		else
		  {
		    name = bfd_elf_string_from_elf_section (input_bfd,
							    symtab_hdr->sh_link,
							    sym->st_name);
		    if (name == NULL)
		      return FALSE;
		    if (*name == '\0')
		      name = bfd_section_name (input_bfd, sec);
		  }
		if (! ((*info->callbacks->reloc_overflow)
		       (info, (h ? &h->root : NULL), name, howto->name,
			(bfd_vma) 0, input_bfd, input_section, rel->r_offset)))
		  return FALSE;
	      }
	      break;
	    }
	}

    }

  return TRUE;
}

/* Similar to memcpy, but SRC points to an array of instruction halfwords
   (i.e. each stands for two bytes) in host byte order.
   Copy to DEST in target byte order.  N is still measured in bytes.  */
static void
pltcpy (bfd *abfd, bfd_byte *dest, const unsigned short *src, size_t n)
{
  for (; n; src++, dest+=2, n -= 2)
    bfd_put_16 (abfd, *src, dest);
}

/* Function :  elf_arc_finish_dynamic_symbol
 * Brief    :  Finish up dynamic symbol handling.  We set the
 *           contents of various dynamic sections here.
 * Args     :  output_bfd :
 *             info       :
 *             h          :
 *             sym        :
 * Returns  : True/False as the return status.
 */
static bfd_boolean
elf_arc_finish_dynamic_symbol (bfd *output_bfd,
			       struct bfd_link_info *info,
			       struct elf_link_hash_entry *h,
			       Elf_Internal_Sym *sym)
{
  bfd *dynobj;

  dynobj = elf_hash_table (info)->dynobj;

  if (h->plt.offset != (bfd_vma) -1)
    {
      asection *splt;
      asection *sgot;
      asection *srel;
      bfd_vma plt_index;
      bfd_vma got_offset;
      Elf_Internal_Rela rel;
      bfd_byte *loc;

      /* This symbol has an entry in the procedure linkage table.  Set
	 it up.  */

      BFD_ASSERT (h->dynindx != -1);

      splt = bfd_get_section_by_name (dynobj, ".plt");
      sgot = bfd_get_section_by_name (dynobj, ".got.plt");
      srel = bfd_get_section_by_name (dynobj, ".rela.plt");
      BFD_ASSERT (splt != NULL && sgot != NULL && srel != NULL);

      /* Get the index in the procedure linkage table which
	 corresponds to this symbol.  This is the index of this symbol
	 in all the symbols for which we are making plt entries.  The
	 first TWO entries in the procedure linkage table are reserved.  */
      plt_index = h->plt.offset / (bfd_get_mach (output_bfd) == bfd_mach_arc_arcv2
				   ? PLT_ENTRY_SIZE_V2 : PLT_ENTRY_SIZE) - 2;

      /* Get the offset into the .got table of the entry that
	 corresponds to this function.  Each .got entry is 4 bytes.
	 The first three are reserved.  */
      got_offset = (plt_index + 3) * 4;

      /* Fill in the entry in the procedure linkage table.  */
      if (! info->shared)
	{
	  if (bfd_get_mach (output_bfd) == bfd_mach_arc_arcv2)
	    {
	      pltcpy (output_bfd,
		      splt->contents + h->plt.offset, elf_arcV2_abs_pltn_entry,
		      PLT_ENTRY_SIZE_V2);
	    }
	  else
	    {
	      pltcpy (output_bfd,
		      splt->contents + h->plt.offset, elf_arc_abs_pltn_entry,
		      PLT_ENTRY_SIZE);
	    }

	  /* fill in the limm in the plt entry to make it jump through its corresponding *(gotentry) */
	  bfd_put_32_me (output_bfd,
			 (sgot-> output_section->vma + sgot->output_offset + got_offset)
			 -(splt->output_section->vma + splt->output_offset + h->plt.offset),
			 splt->contents + h->plt.offset + 4);



	}
      else
	{
	  if (bfd_get_mach (output_bfd) == bfd_mach_arc_arcv2)
	    {
	      pltcpy (output_bfd,
		      splt->contents + h->plt.offset, elf_arcV2_pic_pltn_entry,
		      PLT_ENTRY_SIZE_V2);
	    }
	  else
	    {
	      pltcpy (output_bfd,
		      splt->contents + h->plt.offset, elf_arc_pic_pltn_entry,
		      PLT_ENTRY_SIZE);
	    }

	  /* fill in the limm in the plt entry to make it jump through its corresponding *(gotentry) */
	  bfd_put_32_me (output_bfd,
			 (sgot-> output_section->vma + sgot->output_offset + got_offset)
			 -(splt->output_section->vma + splt->output_offset + h->plt.offset),
			 splt->contents + h->plt.offset + 4);


	}


      /* Fill in the entry in the global offset table.  */
      bfd_put_32 (output_bfd,
		  (splt->output_section->vma
		   + splt->output_offset),
		  sgot->contents + got_offset);

      /* Fill in the entry in the .rela.plt section.  */
      rel.r_offset = (sgot->output_section->vma
		      + sgot->output_offset
		      + got_offset);
      /* RELA relocs */
      rel.r_addend = 0;
      rel.r_info = ELF32_R_INFO (h->dynindx, R_ARC_JMP_SLOT);

      loc = srel->contents;
      loc += plt_index * sizeof (Elf32_External_Rela); /* relA */

      bfd_elf32_swap_reloca_out (output_bfd, &rel, loc);

      if (!h->def_regular)
	{
	  /* Mark the symbol as undefined, rather than as defined in
	     the .plt section.  Leave the value alone.  */
	  sym->st_shndx = SHN_UNDEF;
	}

    }

  if (h->got.offset != (bfd_vma) -1)
    {
      asection *sgot;
      asection *srel;
      Elf_Internal_Rela rel;
      bfd_byte *loc;
      bfd_vma h_got_offset = h->got.offset & ~1;

      /* This symbol has an entry in the global offset table.  Set it
	 up.  */

      sgot = bfd_get_section_by_name (dynobj, ".got");
      srel = bfd_get_section_by_name (dynobj, ".rela.got");
      BFD_ASSERT (sgot != NULL && srel != NULL);

      rel.r_offset = (sgot->output_section->vma
		      + sgot->output_offset
		      + h_got_offset);

      struct elf_ARC_link_hash_entry *ah = (struct elf_ARC_link_hash_entry *) h;
      if (ah->tls_type > GOT_NORMAL) switch (ah->tls_type)
	{
	case GOT_TLS_GD:
	  /* With the obsolete GD design, DTPMOD is the dtv index.
	     With the descriptor design, it is the address of a function
	     that knows the dtv index of the symbol - fetching the
	     correspondig value and adding the contents from the DTPOFF
	     slot, or a lazy resolver
	     function that finds and stores the address of the former
	     kind of function, before jumping to it.
	     The runtime can also further optimize
	     de-facto initial-exec access by adding the module
	     base to the DTPOFF slot contents and storing the sum back there,
	     and putting into the DTPMOD slot the address of a function that
	     merely fetches that and returns.
	     So, the actual differences are not in the static linker, but
	     in gcc and the runtime.  */
	  bfd_put_32 (output_bfd, (bfd_vma) 0, sgot->contents + h_got_offset);
	  rel.r_addend = 0;
	  if (SYMBOL_REFERENCES_LOCAL (info, h))
	    {
	      rel.r_info = ELF32_R_INFO (0, R_ARC_TLS_DTPMOD);
	      break;
	    }
	  rel.r_info = ELF32_R_INFO (h->dynindx, R_ARC_TLS_DTPMOD);
	  bfd_elf32_swap_reloca_out
	    (output_bfd, & rel,
	     (bfd_byte *) ((Elf32_External_Rela *) srel->contents
			   + srel->reloc_count));
	  ++ srel->reloc_count;
	  rel.r_info = ELF32_R_INFO (h->dynindx, R_ARC_TLS_DTPOFF);
	  rel.r_offset += 4;
	  rel.r_addend = 0;
	  bfd_put_32 (output_bfd, (bfd_vma) 0,
		      sgot->contents + h_got_offset + 4);
	  break;
	case GOT_TLS_IE:
	  if (h->dynindx == -1)
	    {
	      /* We originally stored the addend in the GOT, but at this
		 point, we want to move it to the reloc instead as that's
		 where the dynamic linker wants it.  */
	      rel.r_addend
		= bfd_get_32 (output_bfd, sgot->contents + h_got_offset);
	      rel.r_info = ELF32_R_INFO (0, R_ARC_TLS_TPOFF);
	    }
	  else
	    {
	      rel.r_addend = 0;
	      rel.r_info = ELF32_R_INFO (h->dynindx, R_ARC_TLS_TPOFF);
	    }
	  bfd_put_32 (output_bfd, (bfd_vma) 0, sgot->contents + h_got_offset);
	  break;
	default:
	  abort();
	}
      /* If this is a -Bsymbolic link, and the symbol is defined
	 locally, we just want to emit a RELATIVE reloc.  Likewise if
	 the symbol was forced to be local because of a version file.
	 The entry in the global offset table will already have been
	 initialized in the relocate_section function.  */
      else if (info->shared
	  && (info->symbolic || h->dynindx == -1)
	  && h->def_regular)
	{
	  rel.r_addend = 0;
	  rel.r_info = ELF32_R_INFO (0, R_ARC_RELATIVE);
	}
      else if (h->dynindx == -1)
        memset (&rel, 0, sizeof rel);
      else
	{
	  bfd_put_32 (output_bfd, (bfd_vma) 0, sgot->contents + h->got.offset);
	  /* RELA relocs */
	  rel.r_addend = 0;
	  rel.r_info = ELF32_R_INFO (h->dynindx, R_ARC_GLOB_DAT);
	}

      loc = srel->contents;
      loc += srel->reloc_count++ * sizeof (Elf32_External_Rela);/* relA */

      bfd_elf32_swap_reloca_out (output_bfd, &rel, loc);
    }

  if (h->needs_copy)
    {
      asection *s;
      Elf_Internal_Rela rel;
      bfd_byte *loc;

      /* This symbol needs a copy reloc.  Set it up.  */

      BFD_ASSERT (h->dynindx != -1
		  && (h->root.type == bfd_link_hash_defined
		      || h->root.type == bfd_link_hash_defweak));

      s = bfd_get_section_by_name (h->root.u.def.section->owner,
				   ".rela.bss");
      BFD_ASSERT (s != NULL);

      rel.r_addend = 0;
      rel.r_offset = (h->root.u.def.value
		      + h->root.u.def.section->output_section->vma
		      + h->root.u.def.section->output_offset);
      rel.r_info = ELF32_R_INFO (h->dynindx, R_ARC_COPY);

      loc =  s->contents;
      loc += s->reloc_count++ * sizeof (Elf32_External_Rela); /* relA */

      bfd_elf32_swap_reloca_out (output_bfd, &rel, loc);
    }

  /* Mark _DYNAMIC and _GLOBAL_OFFSET_TABLE_ as absolute.  */
  if (strcmp (h->root.root.string, "_DYNAMIC") == 0
      || strcmp (h->root.root.string, "__DYNAMIC") == 0
      || strcmp (h->root.root.string, "_GLOBAL_OFFSET_TABLE_") == 0)
    sym->st_shndx = SHN_ABS;

  return TRUE;
}


/* Function :  elf_arc_finish_dynamic_sections
 * Brief    :  Finish up the dynamic sections handling.
 * Args     :  output_bfd :
 *             info       :
 *             h          :
 *             sym        :
 * Returns  : True/False as the return status.
 */
static bfd_boolean
elf_arc_finish_dynamic_sections (bfd *output_bfd,struct bfd_link_info *info)
{
  bfd *dynobj;
  asection *sgot;
  asection *sdyn;
  asection *asec_ptr;
  char * oldname;

  dynobj = elf_hash_table (info)->dynobj;

  sgot = bfd_get_section_by_name (dynobj, ".got.plt");
  BFD_ASSERT (sgot != NULL);
  sdyn = bfd_get_section_by_name (dynobj, ".dynamic");

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      asection *splt;
      Elf32_External_Dyn *dyncon, *dynconend;

      splt = bfd_get_section_by_name (dynobj, ".plt");
      BFD_ASSERT (splt != NULL && sdyn != NULL);

      dyncon = (Elf32_External_Dyn *) sdyn->contents;
      dynconend = (Elf32_External_Dyn *) (sdyn->contents + sdyn->size);
      for (; dyncon < dynconend; dyncon++)
	{
	  Elf_Internal_Dyn dyn;
	  const char *name;
	  asection *s;

	  bfd_elf32_swap_dyn_in (dynobj, dyncon, &dyn);

	  switch (dyn.d_tag)
	    {
	    default:
	      break;

	    case DT_INIT:
	      oldname = INIT_SYM_STRING;
	      name = init_str;
	      goto get_sym;

	    case DT_FINI:
	      oldname = FINI_SYM_STRING;
	      name = fini_str;
	      goto get_sym;

	    get_sym:
	      {
		struct elf_link_hash_entry *h;

		h = elf_link_hash_lookup (elf_hash_table (info), name,
					  FALSE, FALSE, TRUE);
		if (h != NULL
		    && (h->root.type == bfd_link_hash_defined
			|| h->root.type == bfd_link_hash_defweak))
		  {
		    dyn.d_un.d_val = h->root.u.def.value;
		    asec_ptr = h->root.u.def.section;
		    if (asec_ptr->output_section != NULL)
		      dyn.d_un.d_val += (asec_ptr->output_section->vma
					 + asec_ptr->output_offset);
		    else
		      {
			/* The symbol is imported from another shared
			   library and does not apply to this one.  */
			dyn.d_un.d_val = 0;
		      }

		    bfd_elf32_swap_dyn_out (dynobj, &dyn, dyncon);
		  }
		else
		  {
		    (*_bfd_error_handler)
		      ("warning: specified init/fini symbol %s not found.Defaulting to address of symbol %s",
		       name, oldname);

		    /* restore the default name */
		    name = oldname;

		    h = elf_link_hash_lookup (elf_hash_table (info), name,
					      FALSE, FALSE, TRUE);
		    if (h != NULL
			&& (h->root.type == bfd_link_hash_defined
			    || h->root.type == bfd_link_hash_defweak))
		      {
			dyn.d_un.d_val = h->root.u.def.value;
			asec_ptr = h->root.u.def.section;
			if (asec_ptr->output_section != NULL)
			  dyn.d_un.d_val += (asec_ptr->output_section->vma
					     + asec_ptr->output_offset);
			else
			  {
			    /* The symbol is imported from another shared
			       library and does not apply to this one.  */
			    dyn.d_un.d_val = 0;
			  }

			bfd_elf32_swap_dyn_out (dynobj, &dyn, dyncon);
		      }

		  }

	      }
	      break;

	    case DT_PLTGOT:
	      name = ".plt";
	      goto get_vma;
	    case DT_JMPREL:
	      name = ".rela.plt";
	    get_vma:
	      s = bfd_get_section_by_name (output_bfd, name);
	      BFD_ASSERT (s != NULL);
	      dyn.d_un.d_ptr = s->vma;
	      bfd_elf32_swap_dyn_out (output_bfd, &dyn, dyncon);
	      break;

	    case DT_PLTRELSZ:
	      s = bfd_get_section_by_name (output_bfd, ".rela.plt");
	      BFD_ASSERT (s != NULL);
	      dyn.d_un.d_val = s->size;
	      bfd_elf32_swap_dyn_out (output_bfd, &dyn, dyncon);
	      break;

	    case DT_RELASZ:
	      /* My reading of the SVR4 ABI indicates that the
		 procedure linkage table relocs (DT_JMPREL) should be
		 included in the overall relocs (DT_REL).  This is
		 what Solaris does.  However, UnixWare can not handle
		 that case.  Therefore, we override the DT_RELASZ entry
		 here to make it not include the JMPREL relocs.  Since
		 the linker script arranges for .rela.plt to follow all
		 other relocation sections, we don't have to worry
		 about changing the DT_REL entry.  */
	      s = bfd_get_section_by_name (output_bfd, ".rela.plt");
	      if (s != NULL)
		dyn.d_un.d_val -= s->size;
	      bfd_elf32_swap_dyn_out (output_bfd, &dyn, dyncon);
	      break;
	    }
	}

      /* Fill in the first entry in the procedure linkage table.  */
      if (splt->size > 0)
	{
	  if (info->shared)
	    {
	      if (bfd_get_mach (output_bfd) == bfd_mach_arc_arcv2)
		{
		  pltcpy (output_bfd, splt->contents,
			  elf_arcV2_pic_plt0_entry, 2 * PLT_ENTRY_SIZE_V2);
		}
	      else
		{
		  pltcpy (output_bfd, splt->contents,
			  elf_arc_pic_plt0_entry, 2 * PLT_ENTRY_SIZE);
		}

	      /* fill in the _DYNAMIC@GOTPC+4 and
		 _DYNAMIC@GOTPC+8 at PLT0+4 and PLT0+12 */
	      bfd_put_32_me (output_bfd,
			     ( sgot->output_section->vma + sgot->output_offset + 4 )
			     -(splt->output_section->vma + splt->output_offset ),
			     splt->contents + 4);
	      bfd_put_32_me (output_bfd,
			     (sgot->output_section->vma + sgot->output_offset + 8)
			     -(splt->output_section->vma + splt->output_offset +8),
			     splt->contents + 12);

	      /* put got base at plt0+12 */
	      bfd_put_32 (output_bfd,
			  (sgot->output_section->vma + sgot->output_offset),
			  splt->contents + 20);
	    }
	  else
	    {
	      if (bfd_get_mach (output_bfd) == bfd_mach_arc_arcv2)
		{
		  pltcpy (output_bfd, splt->contents, elf_arcV2_abs_plt0_entry,
			  2 * PLT_ENTRY_SIZE_V2);
		}
	      else
		{
		  pltcpy (output_bfd, splt->contents, elf_arc_abs_plt0_entry,
			  2 * PLT_ENTRY_SIZE);
		}

	      /* in the executable, fill in the exact got addresses
		 for the module id ptr (gotbase+4) and the dl resolve
		 routine (gotbase+8) in the middle endian format */
	      bfd_put_32_me (output_bfd,
			     sgot->output_section->vma + sgot->output_offset + 4,
			     splt->contents + 4);
	      bfd_put_32_me (output_bfd,
			     sgot->output_section->vma + sgot->output_offset + 8,
			     splt->contents + 12);

	      /* put got base at plt0+12 */
	      bfd_put_32 (output_bfd,
			  (sgot->output_section->vma + sgot->output_offset),
			  splt->contents + 20);
	    }
	}

      /* UnixWare sets the entsize of .plt to 4, although that doesn't
	 really seem like the right value.  */
      elf_section_data (splt->output_section)->this_hdr.sh_entsize = 4;

    }


  /* Fill in the first three entries in the global offset table.  */
  if (sgot)
  {
  if (sgot->size > 0)
    {
      if (sdyn == NULL)
	bfd_put_32 (output_bfd, (bfd_vma) 0, sgot->contents);
      else
	bfd_put_32 (output_bfd,
		    sdyn->output_section->vma + sdyn->output_offset,
		    sgot->contents);
      bfd_put_32 (output_bfd, (bfd_vma) 0, sgot->contents + 4);
      bfd_put_32 (output_bfd, (bfd_vma) 0, sgot->contents + 8);
    }

  elf_section_data (sgot->output_section)->this_hdr.sh_entsize = 4;
  }

  return TRUE;
}

/* Desc : Adjust a symbol defined by a dynamic object and referenced by a
   regular object.  The current definition is in some section of the
   dynamic object, but we're not including those sections.  We have to
   change the definition to something the rest of the link can
   understand.  */

static bfd_boolean
elf_arc_adjust_dynamic_symbol (struct bfd_link_info *info,
			       struct elf_link_hash_entry *h)
{
  bfd *dynobj;
  asection *s;
  unsigned int power_of_two;

  dynobj = elf_hash_table (info)->dynobj;

  /* Make sure we know what is going on here.  */
  BFD_ASSERT (dynobj != NULL
	      && (h->needs_plt
		  || h->u.weakdef != NULL
		  || (h->def_dynamic && h->ref_regular && !h->def_regular)));

  /* If this is a function, put it in the procedure linkage table.  We
     will fill in the contents of the procedure linkage table later,
     when we know the address of the .got section.  */
  if (h->type == STT_FUNC || h->needs_plt)
    {
      if (!info->shared && !h->def_dynamic && !h->ref_dynamic)
	{
	  /* This case can occur if we saw a PLT32 reloc in an input
	     file, but the symbol was never referred to by a dynamic
	     object.  In such a case, we don't actually need to build
	     a procedure linkage table, and we can just do a PC32
	     reloc instead.  */
	  BFD_ASSERT (h->needs_plt);
	  return TRUE;
	}

      /* Make sure this symbol is output as a dynamic symbol.  */
      if (h->dynindx == -1 && !h->forced_local)
	{
	  if (! bfd_elf_link_record_dynamic_symbol (info, h))
	    return FALSE;
	}

      if (info->shared
	  || WILL_CALL_FINISH_DYNAMIC_SYMBOL (1, 0, h))
	{
      s = bfd_get_section_by_name (dynobj, ".plt");
      BFD_ASSERT (s != NULL);

      /* If this is the first .plt entry, make room for the special
	 first entry.  */
      if (s->size == 0)
	{
	  s->size += 2 * (bfd_get_mach (dynobj) == bfd_mach_arc_arcv2
			  ? PLT_ENTRY_SIZE_V2 : PLT_ENTRY_SIZE);
	  BFD_DEBUG_PIC (fprintf (stderr, "first plt entry at %d\n", s->size));
	}
      else
	{
	  BFD_DEBUG_PIC (fprintf (stderr, "Next plt entry at %d\n", (int)s->size));
	}

      /* If this symbol is not defined in a regular file, and we are
	 not generating a shared library, then set the symbol to this
	 location in the .plt.  This is required to make function
	 pointers compare as equal between the normal executable and
	 the shared library.  */
      if (!info->shared && !h->def_regular)
	{
	  h->root.u.def.section = s;
	  h->root.u.def.value = s->size;
	}

      h->plt.offset = s->size;

      /* Make room for this entry.  */
      s->size += (bfd_get_mach (dynobj) == bfd_mach_arc_arcv2
		  ? PLT_ENTRY_SIZE_V2 : PLT_ENTRY_SIZE);

      /* We also need to make an entry in the .got.plt section, which
	 will be placed in the .got section by the linker script.  */

      s = bfd_get_section_by_name (dynobj, ".got.plt");
      BFD_ASSERT (s != NULL);
      s->size += 4;

      /* We also need to make an entry in the .rela.plt section.  */
      s = bfd_get_section_by_name (dynobj, ".rela.plt");
      BFD_ASSERT (s != NULL);
      s->size += sizeof (Elf32_External_Rela);

      return TRUE;
	}
      else
	{
	  h->plt.offset = (bfd_vma) -1;
	  h->needs_plt = 0;
	  return TRUE;
	}
    }

  /* If this is a weak symbol, and there is a real definition, the
     processor independent code will have arranged for us to see the
     real definition first, and we can just use the same value.  */
  if (h->u.weakdef != NULL)
    {
      BFD_ASSERT (h->u.weakdef->root.type == bfd_link_hash_defined
		  || h->u.weakdef->root.type == bfd_link_hash_defweak);
      h->root.u.def.section = h->u.weakdef->root.u.def.section;
      h->root.u.def.value = h->u.weakdef->root.u.def.value;
      return TRUE;
    }

  /* If there are no non-GOT references, we do not need a copy
     relocation.  */
  if (!h->non_got_ref)
    return TRUE;

  /* This is a reference to a symbol defined by a dynamic object which
     is not a function.  */

  /* If we are creating a shared library, we must presume that the
     only references to the symbol are via the global offset table.
     For such cases we need not do anything here; the relocations will
     be handled correctly by relocate_section.  */
  if (info->shared)
    return TRUE;

  /* We must allocate the symbol in our .dynbss section, which will
     become part of the .bss section of the executable.  There will be
     an entry for this symbol in the .dynsym section.  The dynamic
     object will contain position independent code, so all references
     from the dynamic object to this symbol will go through the global
     offset table.  The dynamic linker will use the .dynsym entry to
     determine the address it must put in the global offset table, so
     both the dynamic object and the regular object will refer to the
     same memory location for the variable.  */

  s = bfd_get_section_by_name (dynobj, ".dynbss");
  BFD_ASSERT (s != NULL);

  /* We must generate a R_ARC_COPY reloc to tell the dynamic linker to
     copy the initial value out of the dynamic object and into the
     runtime process image.  We need to remember the offset into the
     .rela.bss section we are going to use.  */
  if ((h->root.u.def.section->flags & SEC_ALLOC) != 0)
    {
      asection *srel;

      srel = bfd_get_section_by_name (dynobj, ".rela.bss");
      BFD_ASSERT (srel != NULL);
      srel->size += sizeof (Elf32_External_Rela);
      h->needs_copy = 1;
    }

  /* We need to figure out the alignment required for this symbol.  I
     have no idea how ELF linkers handle this.  */
  power_of_two = bfd_log2 (h->size);
  if (power_of_two > 3)
    power_of_two = 3;

  /* Apply the required alignment.  */
  s->size = BFD_ALIGN (s->size, (bfd_size_type) (1 << power_of_two));
  if (power_of_two > bfd_get_section_alignment (dynobj, s))
    {
      if (! bfd_set_section_alignment (dynobj, s, power_of_two))
	return FALSE;
    }

  /* Define the symbol as being at this point in the section.  */
  h->root.u.def.section = s;
  h->root.u.def.value = s->size;

  /* Increment the section size to make room for the symbol.  */
  s->size += h->size;

  return TRUE;
}

/* This function is called via elf_ARC_link_hash_traverse.  */
   
static bfd_boolean
arc_copy_got_alloc (struct elf_ARC_link_hash_entry * h,
		    void *info ATTRIBUTE_UNUSED)
{
  if (h->got_alloc)
    {
      h->root.got.offset = h->got_alloc->root.got.offset;
      h->got_alloc = NULL;
    }
  return TRUE;
}

/* This function is called from elf_arc_size_dynamic_sections if we are
   creating a shared object.  We defer allocating GOT space for
   global-dynamic / initial-exec tls symbols because they could be changed
   by merging with symbols from other input bfds into initial-exec or
   local-exec, which need less space.
   And we defer allocating GOT space for ordinary GOT symbols because we
   might see a hidden/protected visibility definition later.
   Although we could find all symbols by using elf_ARC_link_hash_traverse,
   that would make the allocation order semi-random; we get more predictable
   output, and likely better locality, by using the linked list built up
   during the calls to elf_arc_check_relocs.  */

static void
arc_allocate_got (struct bfd_link_info *info)
{
  bfd *dynobj;
  asection *sgot;
  asection *srelgot;
  struct elf_ARC_link_hash_entry *ah = elf_arc_first_deferred_got (info);

  if (!ah)
    return;

  dynobj = elf_hash_table (info)->dynobj;
  sgot = bfd_get_section_by_name (dynobj, ".got");
  BFD_ASSERT (sgot != NULL);
  srelgot = bfd_get_section_by_name (dynobj, ".rela.got");
  BFD_ASSERT (srelgot != NULL);

  for (; ah != (struct elf_ARC_link_hash_entry *) -1; ah = ah->next_deferred)
    {
      if (info->executable
	  && (ah->tls_type == GOT_TLS_IE || ah->tls_type == GOT_TLS_GD)
	  && SYMBOL_REFERENCES_LOCAL (info, &ah->root))
	ah->tls_type = GOT_TLS_LE;
      switch (ah->tls_type)
	{
	case GOT_NORMAL:
	  /* A GOTPC32 reference can have the purpose of loading the pointer
	     to a function, in which case, a shared library must load the
	     address from the GOT, which should be the plt entry from the main
	     program if the (non-pic) main program loads this pointer too.
	     Thus, we can't use SYMBOL_CALLS_LOCAL here.  */
	  if (!ah->force_got && SYMBOL_REFERENCES_LOCAL (info, &ah->root))
	    break;
	  /* Fall through.  */
	case GOT_TLS_IE:
	  srelgot->size += sizeof (Elf32_External_Rela);
	  BFD_ASSERT (ah->root.got.offset == (bfd_vma) -1);
	  ah->root.got.offset = sgot->size;
	  sgot->size += 4;
	  break;
	case GOT_TLS_GD:
	  /* We need a DTPMOD reloc for the first got slot, and unless this
	     symbol is local, a DTPOFFF reloc for the second got slot.
	     Fixme: could do with a single reloc if that was more intelligent.
	   */
	  srelgot->size += (sizeof (Elf32_External_Rela)
			    << !SYMBOL_REFERENCES_LOCAL (info, &ah->root));
	  BFD_ASSERT (ah->root.got.offset == (bfd_vma) -1);
	  ah->root.got.offset = sgot->size;
	  sgot->size += 8;
	  break;
	default:
	  break;
	}
      ah->got_alloc = NULL;
    }
  /* Symbol versioning might have set up copies of the symbols that we
     just have given their GOT slot allocations.  Copy these allocations
     to the symbol copies.  */
  elf_ARC_link_hash_traverse (elf_ARC_hash_table (info), arc_copy_got_alloc,
			      (void *) info);
}

/* Set the sizes of the dynamic sections.  */

static bfd_boolean
elf_arc_size_dynamic_sections (bfd *output_bfd,
			       struct bfd_link_info *info)
{
  bfd *dynobj;
  asection *s;
  bfd_boolean plt;
  bfd_boolean relocs;
  bfd_boolean reltext;

  dynobj = elf_hash_table (info)->dynobj;
  BFD_ASSERT (dynobj != NULL);

  arc_allocate_got (info);

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      struct elf_link_hash_entry *h;

      /* Set the contents of the .interp section to the interpreter.  */
      if (! info->shared)
	{
	  s = bfd_get_section_by_name (dynobj, ".interp");
	  BFD_ASSERT (s != NULL);
	  s->size = sizeof ELF_DYNAMIC_INTERPRETER;
	  s->contents = (unsigned char *) ELF_DYNAMIC_INTERPRETER;
	}

      /* Add some entries to the .dynamic section.  We fill in some of the
	 values later, in elf_bfd_final_link, but we must add the entries
	 now so that we know the final size of the .dynamic section.  */
      /* Checking if the .init section is present. We also create DT_INIT / DT_FINE
       * entries if the init_str has been changed by the user
       */
      h =  elf_link_hash_lookup (elf_hash_table (info), "init", FALSE,
				FALSE, FALSE);
      if ((h != NULL
	   && (h->ref_regular || h->def_regular))
	  || (strcmp (init_str, INIT_SYM_STRING) != 0))
	{
	    /*Ravi: changed from bfd_elf32_add_dynamic_entry */
	    if (! _bfd_elf_add_dynamic_entry (info, DT_INIT, 0))
		return FALSE;
	}
      h =  elf_link_hash_lookup (elf_hash_table (info), "fini", FALSE,
				 FALSE, FALSE);
      if ((h != NULL
	   && (h->ref_regular || h->def_regular))
	  || (strcmp (fini_str, FINI_SYM_STRING) != 0))

	{
	    /*Ravi: changed from bfd_elf32_add_dynamic_entry */
	    if (! _bfd_elf_add_dynamic_entry (info, DT_FINI, 0))
		return FALSE;
	}

    }
  else
    {
      /* We may have created entries in the .rela.got section.
	 However, if we are not creating the dynamic sections, we will
	 not actually use these entries.  Reset the size of .rela.got,
	 which will cause it to get stripped from the output file
	 below.  */
      s = bfd_get_section_by_name (dynobj, ".rela.got");
      if (s != NULL)
	s->size = 0;
    }

  /* If this is a -Bsymbolic shared link, then we need to discard all
     PC relative relocs against symbols defined in a regular object.
     For the normal shared case we discard the PC relative relocs
     against symbols that have become local due to visibility changes.
     We allocated space for them in the check_relocs routine, but we
     will not fill them in in the relocate_section routine.  */
  if (info->shared)
    elf_ARC_link_hash_traverse (elf_ARC_hash_table (info),
				 elf_ARC_discard_copies,
				 (void *) info);

  /* The check_relocs and adjust_dynamic_symbol entry points have
     determined the sizes of the various dynamic sections.  Allocate
     memory for them.  */
  plt = FALSE;
  relocs = FALSE;
  reltext = FALSE;
  for (s = dynobj->sections; s != NULL; s = s->next)
    {
      const char *name;
      bfd_boolean strip;

      if ((s->flags & SEC_LINKER_CREATED) == 0)
	continue;

      /* It's OK to base decisions on the section name, because none
	 of the dynobj section names depend upon the input files.  */
      name = bfd_get_section_name (dynobj, s);

      strip = FALSE;

      if (strcmp (name, ".plt") == 0)
	{
	  if (s->size == 0)
	    {
	      /* Strip this section if we don't need it; see the
		 comment below.  */
	      strip = TRUE;
	    }
	  else
	    {
	      /* Remember whether there is a PLT.  */
	      plt = TRUE;
	    }
	}
      else if (strncmp (name, ".rela", 5) == 0)
	{
	  if (s->size == 0)
	    {
	      /* If we don't need this section, strip it from the
		 output file.  This is mostly to handle .rela.bss and
		 .rela.plt.  We must create both sections in
		 create_dynamic_sections, because they must be created
		 before the linker maps input sections to output
		 sections.  The linker does that before
		 adjust_dynamic_symbol is called, and it is that
		 function which decides whether anything needs to go
		 into these sections.  */
	      strip = TRUE;
	    }
	  else
	    {
	      asection *target;

	      /* Remember whether there are any reloc sections other
		 than .rela.plt.  */
	      if (strcmp (name, ".rela.plt") != 0)
		{
		  const char *outname;

		  relocs = TRUE;

		  /* If this relocation section applies to a read only
		     section, then we probably need a DT_TEXTREL
		     entry.  The entries in the .rela.plt section
		     really apply to the .got section, which we
		     created ourselves and so know is not readonly.  */
		  outname = bfd_get_section_name (output_bfd,
						  s->output_section);
		  target = bfd_get_section_by_name (output_bfd, outname + 4);
		  if (target != NULL
		      && (target->flags & SEC_READONLY) != 0
		      && (target->flags & SEC_ALLOC) != 0)
		    reltext = TRUE;
		}

	      /* We use the reloc_count field as a counter if we need
		 to copy relocs into the output file.  */
	      s->reloc_count = 0;
	    }
	}
      else if (strncmp (name, ".got", 4) != 0)
	{
	  /* It's not one of our sections, so don't allocate space.  */
	  continue;
	}

      if (strip)
	{
	  s->flags |= SEC_EXCLUDE;
	  continue;
	}

      /* Allocate memory for the section contents.  The memory is zero
         initialised during initialisation as zero content represents an
         R_ARC_NONE relocation.  Due to issues with how the sizes of
         dynamic sections are managed we currently fail to remove dynamic
         sections that are completely unneeded, the result is that
         uninitialised dynamic sections would be merged into the final
         binary.  Except that we now initialise the sections here.  */
      s->contents = (bfd_byte *) bfd_zalloc (dynobj, s->size);
      if (s->contents == NULL && s->size != 0)
	return FALSE;
    }

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      /* Add some entries to the .dynamic section.  We fill in the
	 values later, in elf_arc_finish_dynamic_sections, but we
	 must add the entries now so that we get the correct size for
	 the .dynamic section.  The DT_DEBUG entry is filled in by the
	 dynamic linker and used by the debugger.  */
      if (! info->shared)
	{
	    /*Ravi: changed from bfd_elf32_add_dynamic_entry */
	    if (! _bfd_elf_add_dynamic_entry (info, DT_DEBUG, 0))
		return FALSE;
	}

      if (plt)
	{
	    /*Ravi: changed from bfd_elf32_add_dynamic_entry */
	    if (! _bfd_elf_add_dynamic_entry (info, DT_PLTGOT, 0)
		|| ! _bfd_elf_add_dynamic_entry (info, DT_PLTRELSZ, 0)
		|| ! _bfd_elf_add_dynamic_entry (info, DT_PLTREL, DT_RELA)
		|| ! _bfd_elf_add_dynamic_entry (info, DT_JMPREL, 0))
		return FALSE;
	}

      if (relocs)
	{
	    /*Ravi: changed from bfd_elf32_add_dynamic_entry */
	    if (! _bfd_elf_add_dynamic_entry (info, DT_RELA, 0)
		|| ! _bfd_elf_add_dynamic_entry (info, DT_RELASZ, 0)
		|| ! _bfd_elf_add_dynamic_entry (info, DT_RELENT,
						  sizeof (Elf32_External_Rela)))
		return FALSE;
	}

      if (reltext)
	{
	    /*Ravi: changed from bfd_elf32_add_dynamic_entry */
	    if (! _bfd_elf_add_dynamic_entry (info, DT_TEXTREL, 0))
		return FALSE;
	}
    }

  return TRUE;
}

/* Update the got entry reference counts for the section being removed.  */

static bfd_boolean
elf32_arc_gc_sweep_hook (bfd *                     abfd,
			 struct bfd_link_info *    info,
			 asection *                sec,
			 const Elf_Internal_Rela * relocs)
{
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  bfd_signed_vma *local_got_refcounts;
  const Elf_Internal_Rela *rel, *relend;

  if (info->relocatable)
    return TRUE;

  elf_section_data (sec)->local_dynrel = NULL;

  symtab_hdr = & elf_symtab_hdr (abfd);
  sym_hashes = elf_sym_hashes (abfd);
  local_got_refcounts = elf_local_got_refcounts (abfd);

  relend = relocs + sec->reloc_count;
  for (rel = relocs; rel < relend; rel++)
    {
      unsigned long r_symndx;
      struct elf_link_hash_entry *h = NULL;
      int r_type;

      r_symndx = ELF32_R_SYM (rel->r_info);
      if (r_symndx >= symtab_hdr->sh_info)
	{
	  h = sym_hashes[r_symndx - symtab_hdr->sh_info];
	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;
	}

      r_type = ELF32_R_TYPE (rel->r_info);

      switch (r_type)
	{
	  /* FIXME: Do we need other relocs here? */
	  case R_ARC_GOT32:
	    if (h != NULL)
	      {
		if (h->got.refcount > 0)
		  h->got.refcount--;
	      }
	    else
	      {
		if (local_got_refcounts && local_got_refcounts[r_symndx] > 0)
		  local_got_refcounts[r_symndx]--;
	      }
	    break;

	  default:
	    break;
	}
    }

  return TRUE;
}

/* GDB expects general purpose registers to be in section .reg. However Linux
 * kernel doesn't create this section and instead writes registers to NOTE
 * section. It is up to the binutils to create a pseudo-section .reg from the
 * contents of NOTE. Also BFD will read pid and signal number from NOTE. This
 * function relies on offsets inside elf_prstatus structure in Linux to be
 * stable. */
static bfd_boolean
elf32_arc_grok_prstatus (bfd *abfd, Elf_Internal_Note *note)
{
  int offset;
  size_t size;

  switch (note->descsz)
  {
    default:
      return FALSE;
    case 236: /* sizeof(struct elf_prstatus) on Linux/arc.  */
      /* pr_cursig */
      elf_tdata (abfd)->core_signal = bfd_get_16 (abfd, note->descdata + 12);
      /* pr_pid */
      elf_tdata (abfd)->core_lwpid = bfd_get_32 (abfd, note->descdata + 24);
      /* pr_regs */
      offset = 72;
      size = ( 40 * 4 ); /* There are 40 registers in user_regs_struct */
      break;
  }
  /* Make a ".reg/999" section.  */
  return _bfd_elfcore_make_pseudosection (abfd, ".reg", size,
					  note->descpos + offset);
}

/* R_ARC_TLS_DTPOFF / R_ARC_TLS_DTPOFF_S9 use the addend to encode the
   base symbol.  Unfortunately, we can't calculate the symbol
   index yet in gas' tc_gen_reloc, so we have to do it here.  */
static void
arc_elf32_write_relocs (bfd *abfd, asection *sec, void *data)
{
  unsigned int idx;

  for (idx = 0; idx < sec->reloc_count; idx++)
    {
      arelent *ptr = sec->orelocation[idx];
      if (ptr->howto->type != R_ARC_TLS_DTPOFF
	  && ptr->howto->type != R_ARC_TLS_DTPOFF_S9)
	continue;
      asymbol *sym = (asymbol *) ptr->addend;
      ptr->addend
	= sym ? _bfd_elf_symbol_from_bfd_symbol (abfd, &sym) : STN_UNDEF;
    }
  bfd_elf32_write_relocs (abfd, sec, data);
}

const struct elf_size_info arc_elf32_size_info =
{
  sizeof (Elf32_External_Ehdr),
  sizeof (Elf32_External_Phdr),
  sizeof (Elf32_External_Shdr),
  sizeof (Elf32_External_Rel),
  sizeof (Elf32_External_Rela),
  sizeof (Elf32_External_Sym),
  sizeof (Elf32_External_Dyn),
  sizeof (Elf_External_Note), 
  4,
  1,
  32, 2,
  ELFCLASS32, EV_CURRENT,
  bfd_elf32_write_out_phdrs,
  bfd_elf32_write_shdrs_and_ehdr,
  bfd_elf32_checksum_contents,
  arc_elf32_write_relocs, 
  bfd_elf32_swap_symbol_in,
  bfd_elf32_swap_symbol_out,
  bfd_elf32_slurp_reloc_table,
  bfd_elf32_slurp_symbol_table,
  bfd_elf32_swap_dyn_in,
  bfd_elf32_swap_dyn_out,
  bfd_elf32_swap_reloc_in,
  bfd_elf32_swap_reloc_out,
  bfd_elf32_swap_reloca_in,
  bfd_elf32_swap_reloca_out
};

#define TARGET_LITTLE_SYM	bfd_elf32_littlearc_vec
#define TARGET_LITTLE_NAME	"elf32-littlearc"
#define TARGET_BIG_SYM		bfd_elf32_bigarc_vec
#define TARGET_BIG_NAME		"elf32-bigarc"
#define ELF_ARCH		bfd_arch_arc
#define ELF_MACHINE_CODE	EM_ARC
#define ELF_MACHINE_ALT1	EM_ARCOMPACT
#define ELF_MACHINE_ALT2	EM_ARCV2
#define ELF_TARGET_ID           ARC_ELF_DATA
#define ELF_MAXPAGESIZE		0x2000

#define elf_info_to_howto                       arc_info_to_howto_rel
#define elf_info_to_howto_rel                   arc_info_to_howto_rel
#define bfd_elf32_bfd_merge_private_bfd_data    arc_elf_merge_private_bfd_data
#define bfd_elf32_bfd_reloc_type_lookup         arc_elf32_bfd_reloc_type_lookup
#define bfd_elf32_bfd_set_private_flags	        arc_elf_set_private_flags
#define bfd_elf32_bfd_print_private_bfd_data	arc_elf_print_private_bfd_data
#define bfd_elf32_bfd_copy_private_bfd_data	arc_elf_copy_private_bfd_data
#define bfd_elf32_mkobject                      arc_elf_mkobject

#define elf_backend_object_p                 arc_elf_object_p
#define elf_backend_final_write_processing   arc_elf_final_write_processing
#define elf_backend_relocate_section         elf_arc_relocate_section
#define elf_backend_check_relocs             elf_arc_check_relocs
#define elf_backend_adjust_dynamic_symbol    elf_arc_adjust_dynamic_symbol

#define elf_backend_finish_dynamic_sections  elf_arc_finish_dynamic_sections

#define elf_backend_finish_dynamic_symbol    elf_arc_finish_dynamic_symbol

#define elf_backend_create_dynamic_sections  _bfd_elf_create_dynamic_sections

#define elf_backend_size_dynamic_sections    elf_arc_size_dynamic_sections

#define elf_backend_grok_prstatus elf32_arc_grok_prstatus

#define elf_backend_gc_sweep_hook	elf32_arc_gc_sweep_hook
#define elf_backend_can_gc_sections    1
#define elf_backend_want_got_plt 1
#define elf_backend_plt_readonly 1
#define elf_backend_want_plt_sym 0
#define elf_backend_got_header_size 12
#define elf_backend_default_execstack 0

#define elf_backend_size_info		arc_elf32_size_info

#include "elf32-target.h"
