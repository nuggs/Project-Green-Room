/*
 * This file contains all the update functions.
 */
#include <sys/types.h>
#include <stdio.h>

/* include main header file */
#include "mud.h"

/*
 * Update_handler()
 *
 * This is the toplevel update function,
 * which is called once every second.
 */
void update_handler()
{
  D_SOCKET *dsock;

  for (dsock = dsock_list; dsock; dsock = dsock->next)
  {
// do nothing
  }
  return;
}
