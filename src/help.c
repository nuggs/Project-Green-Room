/*
 * This file contains the dynamic help system.
 * If you wish to update a help file, simply edit
 * the entry in ../help/ and the mud will load the
 * new version next time someone tries to access
 * that help file.
 */
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>

/* include main header file */
#include "mud.h"

HELP_DATA   *   help_list;        /* the linked list of help files     */
char        *   greeting;         /* the welcome greeting              */
char        *   motd;             /* the MOTD help file                */

/*
 * Check_help()
 *
 * This function first sees if there is a valid
 * help file in the help_list, should there be
 * no helpfile in the help_list, it will check
 * the ../help/ directory for a suitable helpfile
 * entry. Even if it finds the helpfile in the
 * help_list, it will still check the ../help/
 * directory, and should the file be newer than
 * the currently loaded helpfile, it will reload
 * the helpfile.
 */
bool check_help(D_MOBILE *dMob, char *helpfile)
{
  HELP_DATA *pHelp;
  char buf[MAX_HELP_ENTRY + 80];
  char *entry, *hFile;
  bool found = FALSE;

  hFile = capitalize(helpfile);

  for (pHelp = help_list; pHelp; pHelp = pHelp->next)
  {
    if (is_prefix(helpfile, pHelp->keyword))
    {
      found = TRUE;
      break;
    }
  }

  /* If there is an updated version we load it */
  if (found)
  {
    if (last_modified(hFile) > pHelp->load_time)
    {
      free(pHelp->text);
      pHelp->text = strdup(read_help_entry(hFile));
    }
  }
  else /* is there a version at all ?? */
  {
    if ((entry = read_help_entry(hFile)) == NULL)
      return FALSE;
    else
    {
      if ((pHelp = malloc(sizeof(*pHelp))) == NULL)
      { 
        bug("Check_help: Cannot allocate memory.");
        abort();
      }
      pHelp->keyword    =  strdup(hFile);
      pHelp->text       =  strdup(entry);
      pHelp->next       =  help_list;
      pHelp->load_time  =  time(NULL);
      help_list         =  pHelp;
    }
  }

  sprintf(buf, "=== %s ===\n\r%s", pHelp->keyword, pHelp->text);
  text_to_mobile(dMob, buf);
  return TRUE;
}

/*
 * Loads all the helpfiles listed in ../help/help.lst
 */
void load_helps()
{
  HELP_DATA *new_help;
  FILE *fp;
  char *hFile;

  /* check to see if there is a list of help files */
  if ((fp = fopen("../help/help.lst", "r")) == NULL)
    return;

  log("Load_helps: getting all help files.");

  while ((hFile = fread_line(fp)) != NULL)
  {
    if ((new_help = malloc(sizeof(*new_help))) == NULL)
    {
      bug("Load_helps: Cannot allocate memory.");
      abort();
    }
    new_help->keyword    =  strdup(hFile);
    new_help->text       =  strdup(read_help_entry(hFile));
    new_help->load_time  =  time(NULL);
    new_help->next       =  help_list;
    help_list            =  new_help;

    if (compares("GREETING", new_help->keyword))
      greeting = new_help->text;
    else if (compares("MOTD", new_help->keyword))
      motd = new_help->text;
  }
}
