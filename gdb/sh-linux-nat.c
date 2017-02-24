/* Low level SH interface to ptrace, for GDB when running native.
   Copyright (C) 2002, 2004 Free Software Foundation, Inc.

This file is part of GDB.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "inferior.h"
#include "gdbcore.h"
#include "regcache.h"
#include "linux-nat.h"
#include "target.h"
#include "arch-utils.h"

#include "gdb_assert.h"
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/procfs.h>
#include <asm/ptrace.h>

/* Prototypes for supply_gregset etc. */
#include "gregset.h"
#include "sh-tdep.h"

/* Defines ps_err_e, struct ps_prochandle.  */
#include "gdb_proc_service.h"

//#include <asm/elf.h>

#define SH_LINUX_NUM_REGS	40
/* This table must line up with REGISTER_NAME in "sh-tdep.c".  */
static const int regmap[] =
{
  /* general registers 0-15 */
  REG_REG0   , REG_REG0+1 , REG_REG0+2 , REG_REG0+3,
  REG_REG0+4 , REG_REG0+5 , REG_REG0+6 , REG_REG0+7,
  REG_REG0+8 , REG_REG0+9 , REG_REG0+10, REG_REG0+11,
  REG_REG0+12, REG_REG0+13, REG_REG0+14, REG_REG0+15,
  /* 16 - 22 */
  REG_PC, REG_PR, REG_GBR, -1, REG_MACH, REG_MACL, REG_SR,
  /* 23, 24 */
  REG_FPUL, REG_FPSCR,
  /* floating point registers 25 - 40 */
  REG_FPREG0   , REG_FPREG0+1 , REG_FPREG0+2 , REG_FPREG0+3 ,
  REG_FPREG0+4 , REG_FPREG0+5 , REG_FPREG0+6 , REG_FPREG0+7 ,
  REG_FPREG0+8 , REG_FPREG0+9 , REG_FPREG0+10, REG_FPREG0+11,
  REG_FPREG0+12, REG_FPREG0+13, REG_FPREG0+14, REG_FPREG0+15,
};

CORE_ADDR
register_u_addr (CORE_ADDR blockend, int regnum)
{
  if (regnum < 0 || regnum >= sizeof regmap/sizeof regmap[0])
    return (CORE_ADDR)-1;
  return (blockend + 4 * regmap[regnum]);
}


/* Return the address in the core dump or inferior of register REGNO.
   BLOCKEND is the address of the end of the user structure.  */

CORE_ADDR
register_addr (int regno, CORE_ADDR blockend)
{
  CORE_ADDR addr;

  if (regno < 0 || regno >= SH_LINUX_NUM_REGS) {
    internal_error (__FILE__, __LINE__,
		  _("Got request for bad register number %d."), regno);
  }

  REGISTER_U_ADDR (addr, blockend, regno);

  return addr;
}

/* Fetch one register.  */

static void
fetch_register (struct regcache *regcache, int tid, int regno)
{
  int val;

  if (cannot_fetch_register (regno))
    {
      regcache_raw_supply (regcache, regno, NULL);
      return;
    }

  errno = 0;
  val = ptrace (PTRACE_PEEKUSER, tid, register_addr (regno, 0), 0);
  if (errno != 0)
    perror_with_name (_("Couldn't get registers"));

  regcache_raw_supply (regcache, regno, &val);
}

/* Store one register. */

static void
store_register (struct regcache *regcache, int tid, int regno)
{
  int val;

  if (cannot_store_register (regno))
    return;

  errno = 0;
  regcache_raw_collect (regcache, regno, &val);
  ptrace (PTRACE_POKEUSER, tid, register_addr (regno, 0), val);
  if (errno != 0)
    perror_with_name (_("Couldn't write registers"));
}

/* Transfering the general-purpose registers between GDB, inferiors
   and core files.  */

/* Fill GDB's register array with the general-purpose register values
   in *GREGSETP.  */

void
supply_gregset (struct regcache *regcache, const elf_gregset_t *gregsetp)
{
  elf_greg_t *regp = (elf_greg_t *) gregsetp;
  int i;

  for (i = 0; i < 23; i++)
    if (regmap[i] == -1)
      regcache_raw_supply (regcache, i, NULL);
    else
      regcache_raw_supply (regcache, i, (char *) (regp + regmap[i]));
}

/* Fill register REGNO (if it is a general-purpose register) in
   *GREGSETPS with the value in GDB's register array.  If REGNO is -1,
   do this for all registers.  */

void
fill_gregset (const struct regcache *regcache, elf_gregset_t *gregsetp, int regno)
{
  elf_greg_t *regp = (elf_greg_t *) gregsetp;
  int i;

  for (i = 0; i < 23; i++)
    if (regmap[i] != -1 && (regno == -1 || regno == i))
      regcache_raw_collect (regcache, i, (char *) (regp + regmap[i]));
}

/* Transfering floating-point registers between GDB, inferiors and cores.  */

/* Fill GDB's register array with the floating-point register values in
   *FPREGSETP.  */

void
supply_fpregset (struct regcache *regcache, const elf_fpregset_t *fpregsetp)
{
  int i;
  long *regp = (long *)fpregsetp;

  for (i = 0; i < 16; i++)
    regcache_raw_supply (regcache, 25 + i, (char *) (regp + i));
  regcache_raw_supply (regcache, FPUL_REGNUM, (char *) (regp + REG_FPUL - REG_FPREG0));
  regcache_raw_supply (regcache, FPSCR_REGNUM, (char *) (regp + REG_FPSCR - REG_FPREG0));
}

/* Fill register REGNO (if it is a floating-point register) in
   *FPREGSETP with the value in GDB's register array.  If REGNO is -1,
   do this for all registers.  */

void
fill_fpregset (const struct regcache *regcache, elf_fpregset_t *fpregsetp, int regno)
{
  int i;
  long *regp = (long *)fpregsetp;

  for (i = 0; i < 16; i++)
    if ((regno == -1) || (regno == i))
      regcache_raw_collect (regcache, 25 + i, (char *) (regp + i));
  if ((regno == -1) || regno == FPSCR_REGNUM)
    regcache_raw_collect (regcache, FPSCR_REGNUM, (char *) (regp + REG_FPSCR - REG_FPREG0));
  if ((regno == -1) || regno == FPUL_REGNUM)
    regcache_raw_collect (regcache, FPUL_REGNUM, (char *) (regp + REG_FPUL - REG_FPREG0));
}

/* Transferring arbitrary registers between GDB and inferior.  */

/* Check if register REGNO in the child process is accessible.
   If we are accessing registers directly via the U area, only the
   general-purpose registers are available.
   All registers should be accessible if we have GETREGS support.  */
   
int
cannot_fetch_register (int regno)
{
  return (regno < 0 || regno >= sizeof regmap / sizeof regmap[0] || regmap[regno] == -1);
}

int
cannot_store_register (int regno)
{
  return (regno < 0 || regno >= sizeof regmap / sizeof regmap[0] || regmap[regno] == -1);
}

/* Fetch register values from the inferior.
   If REGNO is negative, do this for all registers.
   Otherwise, REGNO specifies which register (so we can save time). */

static void
sh_linux_fetch_inferior_registers (struct target_ops *ops, struct regcache *regcache, int regno)
{
  int i;
  int tid;

  /* GNU/Linux LWP ID's are process ID's.  */
  if ((tid = ptid_get_lwp (inferior_ptid)) == 0)
    tid = ptid_get_pid (inferior_ptid);	/* Not a threaded program.  */

  for (i = 0; i < SH_LINUX_NUM_REGS; i++)
    if (regno == -1 || regno == i)
      fetch_register (regcache, tid, i);
}
/* Store our register values back into the inferior.
   If REGNO is negative, do this for all registers.
   Otherwise, REGNO specifies which register (so we can save time).  */

static void
sh_linux_store_inferior_registers (struct target_ops *ops, struct regcache *regcache, int regno)
{
  int i;
  int tid;

  /* GNU/Linux LWP ID's are process ID's.  */
  if ((tid = ptid_get_lwp (inferior_ptid)) == 0)
    tid = ptid_get_pid (inferior_ptid);	/* Not a threaded program.  */

  for (i = 0; i < SH_LINUX_NUM_REGS; i++)
    if (regno == -1 || regno == i)
      store_register (regcache, tid, i);
}

void
_initialize_sh_linux_nat (void)
{
  struct target_ops *t;

  /* Fill in the generic GNU/Linux methods.  */
  t = linux_target ();

  /* Add our register access methods.  */
  t->to_fetch_registers = sh_linux_fetch_inferior_registers;
  t->to_store_registers = sh_linux_store_inferior_registers;

  /* Register the target.  */
  linux_nat_add_target (t);
}
