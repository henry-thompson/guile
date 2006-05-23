/* Copyright (C) 1998,1999,2000,2001, 2006 Free Software Foundation, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


/* This is an implementation of guardians as described in
 * R. Kent Dybvig, Carl Bruggeman, and David Eby (1993) "Guardians in
 * a Generation-Based Garbage Collector" ACM SIGPLAN Conference on
 * Programming Language Design and Implementation, June 1993
 * ftp://ftp.cs.indiana.edu/pub/scheme-repository/doc/pubs/guardians.ps.gz
 *
 * Original design:          Mikael Djurfeldt
 * Original implementation:  Michael Livshin
 * Hacked on since by:       everybody
 *
 * By this point, the semantics are actually quite different from
 * those described in the abovementioned paper.  The semantic changes
 * are there to improve safety and intuitiveness.  The interface is
 * still (mostly) the one described by the paper, however.
 *
 * Boiled down again:        Marius Vollmer
 *
 * Now they should again behave like those described in the paper.
 * Scheme guardians should be simple and friendly, not like the greedy
 * monsters we had...
 *
 * Rewritten for the Boehm-Wiser GC by Ludovic Court�s.
 * FIXME: This is currently not thread-safe.
 */


#include "libguile/_scm.h"
#include "libguile/async.h"
#include "libguile/ports.h"
#include "libguile/print.h"
#include "libguile/smob.h"
#include "libguile/validate.h"
#include "libguile/root.h"
#include "libguile/hashtab.h"
#include "libguile/weaks.h"
#include "libguile/deprecation.h"
#include "libguile/eval.h"

#include "libguile/guardians.h"
#include <gc/gc.h>




static scm_t_bits tc16_guardian;

typedef struct t_guardian
{
  unsigned long live;
  SCM zombies;
  struct t_guardian *next;
} t_guardian;

#define GUARDIAN_P(x)    SCM_SMOB_PREDICATE(tc16_guardian, x)
#define GUARDIAN_DATA(x) ((t_guardian *) SCM_CELL_WORD_1 (x))




static int
guardian_print (SCM guardian, SCM port, scm_print_state *pstate SCM_UNUSED)
{
  t_guardian *g = GUARDIAN_DATA (guardian);
  
  scm_puts ("#<guardian ", port);
  scm_uintprint ((scm_t_bits) g, 16, port);

  scm_puts (" (reachable: ", port);
  scm_display (scm_from_uint (g->live), port);
  scm_puts (" unreachable: ", port);
  scm_display (scm_length (g->zombies), port);
  scm_puts (")", port);

  scm_puts (">", port);

  return 1;
}

/* Handle finalization of OBJ which is guarded by the guardians listed in
   GUARDIAN_LIST.  */
static SCM
finalize_guarded (SCM obj, SCM guardian_list)
{
  SCM cell_pool;

#if 0
  printf ("finalizing guarded %p (%u guardians)\n",
	  SCM2PTR (obj), scm_to_uint (scm_length (guardian_list)));
  scm_write (guardian_list, scm_current_output_port ());
#endif

  /* Preallocate a bunch of cells so that we can make sure that no garbage
     collection (and, thus, nested calls to `finalize_guarded ()') occurs
     while executing the following loop.  This is quite inefficient (call to
     `scm_length ()') but that shouldn't be a problem in most cases.  */
  cell_pool = scm_make_list (scm_length (guardian_list), SCM_UNSPECIFIED);

  /* Tell each guardian interested in OBJ that OBJ is no longer
     reachable.  */
  for (;
       guardian_list != SCM_EOL;
       guardian_list = SCM_CDR (guardian_list))
    {
      SCM zombies;
      t_guardian *g = GUARDIAN_DATA (SCM_CAR (guardian_list));

      if (g->live == 0)
	abort ();

      /* Get a fresh cell from CELL_POOL.  */
      zombies = cell_pool;
      cell_pool = SCM_CDR (cell_pool);

      /* Compute and update G's zombie list.  */
      SCM_SETCAR (zombies, obj);
      SCM_SETCDR (zombies, g->zombies);
      g->zombies = zombies;

      g->live--;
      g->zombies = zombies;
    }

#if 0
  printf ("end of finalize (%p)\n", SCM2PTR (obj));
#endif

  return SCM_UNSPECIFIED;
}

/* Add OBJ as a guarded object of GUARDIAN.  */
static void
scm_i_guard (SCM guardian, SCM obj)
{
  t_guardian *g = GUARDIAN_DATA (guardian);

  if (!SCM_IMP (obj))
    {
      /* Register a finalizer and pass a list of guardians interested in OBJ
	 as the ``client data'' argument.  */
      SCM guardians_for_obj, prev_guardians_for_obj;

      g->live++;
      guardians_for_obj = scm_cons (guardian, SCM_EOL);

      prev_guardians_for_obj =
	scm_gc_register_finalizer (obj, finalize_guarded,
				   guardians_for_obj, 0);

      if (scm_is_pair (prev_guardians_for_obj))
	/* Concatenate the previous list of guardians for OBJ.  */
	SCM_SETCDR (guardians_for_obj, prev_guardians_for_obj);
    }
}

static SCM
scm_i_get_one_zombie (SCM guardian)
{
  t_guardian *g = GUARDIAN_DATA (guardian);
  SCM res = SCM_BOOL_F;

  if (g->zombies != SCM_EOL)
    {
      /* Note: We return zombies in reverse order.  */
      res = SCM_CAR (g->zombies);
      g->zombies = SCM_CDR (g->zombies);
    }

  return res;
}

/* This is the Scheme entry point for each guardian: If OBJ is an
 * object, it's added to the guardian's live list.  If OBJ is unbound,
 * the next available unreachable object (or #f if none) is returned.
 *
 * If the second optional argument THROW_P is true (the default), then
 * an error is raised if GUARDIAN is greedy and OBJ is already greedily
 * guarded.  If THROW_P is false, #f is returned instead of raising the
 * error, and #t is returned if everything is fine.
 */ 
static SCM
guardian_apply (SCM guardian, SCM obj, SCM throw_p)
{
#if ENABLE_DEPRECATED
  if (!SCM_UNBNDP (throw_p))
    scm_c_issue_deprecation_warning
      ("Using the 'throw?' argument of a guardian is deprecated "
       "and ineffective.");
#endif

  if (!SCM_UNBNDP (obj))
    {
      scm_i_guard (guardian, obj);
      return SCM_UNSPECIFIED;
    }
  else
    return scm_i_get_one_zombie (guardian);
}

SCM_DEFINE (scm_make_guardian, "make-guardian", 0, 0, 0, 
	    (),
"Create a new guardian.  A guardian protects a set of objects from\n"
"garbage collection, allowing a program to apply cleanup or other\n"
"actions.\n"
"\n"
"@code{make-guardian} returns a procedure representing the guardian.\n"
"Calling the guardian procedure with an argument adds the argument to\n"
"the guardian's set of protected objects.  Calling the guardian\n"
"procedure without an argument returns one of the protected objects\n"
"which are ready for garbage collection, or @code{#f} if no such object\n"
"is available.  Objects which are returned in this way are removed from\n"
"the guardian.\n"
"\n"
"You can put a single object into a guardian more than once and you can\n"
"put a single object into more than one guardian.  The object will then\n"
"be returned multiple times by the guardian procedures.\n"
"\n"
"An object is eligible to be returned from a guardian when it is no\n"
"longer referenced from outside any guardian.\n"
"\n"
"There is no guarantee about the order in which objects are returned\n"
"from a guardian.  If you want to impose an order on finalization\n"
"actions, for example, you can do that by keeping objects alive in some\n"
"global data structure until they are no longer needed for finalizing\n"
"other objects.\n"
"\n"
"Being an element in a weak vector, a key in a hash table with weak\n"
"keys, or a value in a hash table with weak value does not prevent an\n"
"object from being returned by a guardian.  But as long as an object\n"
"can be returned from a guardian it will not be removed from such a\n"
"weak vector or hash table.  In other words, a weak link does not\n"
"prevent an object from being considered collectable, but being inside\n"
"a guardian prevents a weak link from being broken.\n"
"\n"
"A key in a weak key hash table can be though of as having a strong\n"
"reference to its associated value as long as the key is accessible.\n"
"Consequently, when the key only accessible from within a guardian, the\n"
"reference from the key to the value is also considered to be coming\n"
"from within a guardian.  Thus, if there is no other reference to the\n"
	    "value, it is eligible to be returned from a guardian.\n")
#define FUNC_NAME s_scm_make_guardian
{
  t_guardian *g = scm_gc_malloc (sizeof (t_guardian), "guardian");
  SCM z;

  /* A tconc starts out with one tail pair. */
  g->live = 0;
  g->zombies = SCM_EOL;

  g->next = NULL;

  SCM_NEWSMOB (z, tc16_guardian, g);

  return z;
}
#undef FUNC_NAME

void
scm_init_guardians ()
{
  /* We use unordered finalization `a la Java.  */
  GC_java_finalization = 1;

  tc16_guardian = scm_make_smob_type ("guardian", 0);

  scm_set_smob_print (tc16_guardian, guardian_print);
#if ENABLE_DEPRECATED
  scm_set_smob_apply (tc16_guardian, guardian_apply, 0, 2, 0);
#else
  scm_set_smob_apply (tc16_guardian, guardian_apply, 0, 1, 0);
#endif

#include "libguile/guardians.x"
}

/*
  Local Variables:
  c-file-style: "gnu"
  End:
*/
