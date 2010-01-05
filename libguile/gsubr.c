/* Copyright (C) 1995,1996,1997,1998,1999,2000,2001, 2006, 2008, 2009 Free Software Foundation, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <alloca.h>

#include <stdio.h>
#include <stdarg.h>

#include "libguile/_scm.h"
#include "libguile/procprop.h"
#include "libguile/root.h"

#include "libguile/gsubr.h"
#include "libguile/deprecation.h"

#include "libguile/private-options.h"

/*
 * gsubr.c
 * Provide `gsubrs' -- subrs taking a prescribed number of required, optional,
 * and rest arguments.
 */

/* #define GSUBR_TEST */

SCM_GLOBAL_SYMBOL (scm_sym_name, "name");

static SCM
create_gsubr (int define, const char *name,
	      unsigned int req, unsigned int opt, unsigned int rst,
	      SCM (*fcn) (), SCM *generic_loc)
{
  SCM subr;
  SCM sname;
  SCM *meta_info;
  unsigned type;

  type = SCM_GSUBR_MAKTYPE (req, opt, rst);
  if (SCM_GSUBR_REQ (type) != req
      || SCM_GSUBR_OPT (type) != opt
      || SCM_GSUBR_REST (type) != rst)
    scm_out_of_range ("create_gsubr", scm_from_uint (req + opt + rst));

  meta_info = scm_gc_malloc (2 * sizeof (*meta_info), "subr meta-info");
  sname = scm_from_locale_symbol (name);
  meta_info[0] = sname;
  meta_info[1] = SCM_EOL;  /* properties */

  subr = scm_double_cell ((scm_t_bits) scm_tc7_gsubr | (type << 8U),
                          (scm_t_bits) fcn,
                          (scm_t_bits) generic_loc,
                          (scm_t_bits) meta_info);

  if (define)
    scm_define (sname, subr);

  return subr;
}

SCM
scm_c_make_gsubr (const char *name, int req, int opt, int rst, SCM (*fcn)())
{
  return create_gsubr (0, name, req, opt, rst, fcn, NULL);
}

SCM
scm_c_define_gsubr (const char *name, int req, int opt, int rst, SCM (*fcn)())
{
  return create_gsubr (1, name, req, opt, rst, fcn, NULL);
}

SCM
scm_c_make_gsubr_with_generic (const char *name,
			       int req,
			       int opt,
			       int rst,
			       SCM (*fcn)(),
			       SCM *gf)
{
  return create_gsubr (0, name, req, opt, rst, fcn, gf);
}

SCM
scm_c_define_gsubr_with_generic (const char *name,
				 int req,
				 int opt,
				 int rst,
				 SCM (*fcn)(),
				 SCM *gf)
{
  return create_gsubr (1, name, req, opt, rst, fcn, gf);
}

/* Apply PROC, a gsubr, to the ARGC arguments in ARGV.  ARGC is expected to
   match the number of arguments of the underlying C function.  */
static SCM
gsubr_apply_raw (SCM proc, unsigned int argc, const SCM *argv)
{
  SCM (*fcn) ();
  unsigned int type, argc_max;

  type = SCM_GSUBR_TYPE (proc);
  argc_max = SCM_GSUBR_REQ (type) + SCM_GSUBR_OPT (type)
    + SCM_GSUBR_REST (type);

  if (SCM_UNLIKELY (argc != argc_max))
    /* We expect the exact argument count.  */
    scm_wrong_num_args (SCM_SUBR_NAME (proc));

  fcn = SCM_SUBRF (proc);

  switch (argc)
    {
    case 0:
      return (*fcn) ();
    case 1:
      return (*fcn) (argv[0]);
    case 2:
      return (*fcn) (argv[0], argv[1]);
    case 3:
      return (*fcn) (argv[0], argv[1], argv[2]);
    case 4:
      return (*fcn) (argv[0], argv[1], argv[2], argv[3]);
    case 5:
      return (*fcn) (argv[0], argv[1], argv[2], argv[3], argv[4]);
    case 6:
      return (*fcn) (argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
    case 7:
      return (*fcn) (argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
		     argv[6]);
    case 8:
      return (*fcn) (argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
		     argv[6], argv[7]);
    case 9:
      return (*fcn) (argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
		     argv[6], argv[7], argv[8]);
    case 10:
      return (*fcn) (argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
		     argv[6], argv[7], argv[8], argv[9]);
    default:
      scm_misc_error ((char *) SCM_SUBR_NAME (proc),
		      "gsubr invocation with more than 10 arguments not implemented",
		      SCM_EOL);
    }

  return SCM_BOOL_F; /* Never reached. */
}

/* Apply PROC, a gsubr, to the given arguments.  Missing optional arguments
   are added, and rest arguments are turned into a list.  */
SCM
scm_i_gsubr_apply (SCM proc, SCM arg, ...)
{
  unsigned int type, argc, argc_max;
  SCM *argv;
  va_list arg_list;

  type = SCM_GSUBR_TYPE (proc);
  argc_max = SCM_GSUBR_REQ (type) + SCM_GSUBR_OPT (type);
  argv = alloca ((argc_max + SCM_GSUBR_REST (type)) * sizeof (*argv));

  va_start (arg_list, arg);

  for (argc = 0;
       !SCM_UNBNDP (arg) && argc < argc_max;
       argc++, arg = va_arg (arg_list, SCM))
    argv[argc] = arg;

  if (SCM_UNLIKELY (argc < SCM_GSUBR_REQ (type)))
    /* too few args */
    scm_wrong_num_args (SCM_SUBR_NAME (proc));
  if (SCM_UNLIKELY (!SCM_UNBNDP (arg) && !SCM_GSUBR_REST (type)))
    /* too many args */
    scm_wrong_num_args (SCM_SUBR_NAME (proc));

  /* Fill in optional arguments that were not passed.  */
  while (argc < argc_max)
    argv[argc++] = SCM_UNDEFINED;

  if (SCM_GSUBR_REST (type))
    {
      /* Accumulate rest arguments in a list.  */
      SCM *rest_loc;

      argv[argc_max] = SCM_EOL;

      for (rest_loc = &argv[argc_max];
	   !SCM_UNBNDP (arg);
	   rest_loc = SCM_CDRLOC (*rest_loc), arg = va_arg (arg_list, SCM))
	*rest_loc = scm_cons (arg, SCM_EOL);

      argc = argc_max + 1;
    }

  va_end (arg_list);

  return gsubr_apply_raw (proc, argc, argv);
}

/* Apply SELF, a gsubr, to the arguments listed in ARGS.  Missing optional
   arguments are added, and rest arguments are kept into a list.  */
SCM
scm_i_gsubr_apply_list (SCM self, SCM args)
#define FUNC_NAME "scm_i_gsubr_apply"
{
  SCM v[SCM_GSUBR_MAX];
  unsigned int typ = SCM_GSUBR_TYPE (self);
  long i, n = SCM_GSUBR_REQ (typ) + SCM_GSUBR_OPT (typ) + SCM_GSUBR_REST (typ);

  for (i = 0; i < SCM_GSUBR_REQ (typ); i++) {
    if (scm_is_null (args))
      scm_wrong_num_args (SCM_SUBR_NAME (self));
    v[i] = SCM_CAR(args);
    args = SCM_CDR(args);
  }
  for (; i < SCM_GSUBR_REQ (typ) + SCM_GSUBR_OPT (typ); i++) {
    if (SCM_NIMP (args)) {
      v[i] = SCM_CAR (args);
      args = SCM_CDR(args);
    }
    else
      v[i] = SCM_UNDEFINED;
  }
  if (SCM_GSUBR_REST(typ))
    v[i] = args;
  else if (!scm_is_null (args))
    scm_wrong_num_args (SCM_SUBR_NAME (self));

  return gsubr_apply_raw (self, n, v);
}
#undef FUNC_NAME

/* Apply SELF, a gsubr, to the arguments in ARGS.  Missing optional
   arguments are added, and rest arguments are consed into a list.  */
SCM
scm_i_gsubr_apply_array (SCM self, SCM *args, int nargs, int headroom)
#define FUNC_NAME "scm_i_gsubr_apply"
{
  unsigned int typ = SCM_GSUBR_TYPE (self);
  long i, n = SCM_GSUBR_REQ (typ) + SCM_GSUBR_OPT (typ) + SCM_GSUBR_REST (typ);

  if (SCM_UNLIKELY (nargs < SCM_GSUBR_REQ (typ)))
    scm_wrong_num_args (SCM_SUBR_NAME (self));

  if (SCM_UNLIKELY (headroom < n - nargs))
    {
      /* fallback on apply-list */
      SCM arglist = SCM_EOL;
      while (nargs--)
        arglist = scm_cons (args[nargs], arglist);
      return scm_i_gsubr_apply_list (self, arglist);
    }

  for (i = nargs; i < SCM_GSUBR_REQ (typ) + SCM_GSUBR_OPT (typ); i++)
    args[i] = SCM_UNDEFINED;

  if (SCM_GSUBR_REST(typ))
    {
      SCM rest = SCM_EOL;
      /* fallback on apply-list */
      while (nargs-- >= n)
        rest = scm_cons (args[nargs], rest);
      args[n - 1] = rest;
    }
  else if (nargs > n)
    scm_wrong_num_args (SCM_SUBR_NAME (self));

  return gsubr_apply_raw (self, n, args);
}
#undef FUNC_NAME


#ifdef GSUBR_TEST
/* A silly example, taking 2 required args, 1 optional, and
   a scm_list of rest args
   */
SCM
gsubr_21l(SCM req1, SCM req2, SCM opt, SCM rst)
{
  scm_puts ("gsubr-2-1-l:\n req1: ", scm_cur_outp);
  scm_display(req1, scm_cur_outp);
  scm_puts ("\n req2: ", scm_cur_outp);
  scm_display(req2, scm_cur_outp);
  scm_puts ("\n opt: ", scm_cur_outp);
  scm_display(opt, scm_cur_outp);
  scm_puts ("\n rest: ", scm_cur_outp);
  scm_display(rst, scm_cur_outp);
  scm_newline(scm_cur_outp);
  return SCM_UNSPECIFIED;
}
#endif


void
scm_init_gsubr()
{
#ifdef GSUBR_TEST
  scm_c_define_gsubr ("gsubr-2-1-l", 2, 1, 1, gsubr_21l); /* example */
#endif

#include "libguile/gsubr.x"
}

/*
  Local Variables:
  c-file-style: "gnu"
  End:
*/
