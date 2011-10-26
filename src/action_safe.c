/*
 * This file handles non-fighting player actions.
 */
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* include main header file */
#include "mud.h"

void cmd_say(D_MOBILE *dMob, char *arg)
{
  if (arg[0] == '\0')
  {
    text_to_mobile(dMob, "Say what?\n\r");
    return;
  }
  communicate(dMob, arg, COMM_LOCAL);
}

void cmd_quit(D_MOBILE *dMob, char *arg)
{
  char buf[MAX_BUFFER];

  /* log the attempt */
  sprintf(buf, "%s has left the game.", dMob->name);
  log(buf);

  save_player(dMob);

  dMob->socket->player = NULL;
  free_mobile(dMob);
  close_socket(dMob->socket, FALSE);
}

void cmd_shutdown(D_MOBILE *dMob, char *arg)
{
  shut_down = TRUE;
}

void cmd_commands(D_MOBILE *dMob, char *arg)
{
  BUFFER *buf = buffer_new(MAX_BUFFER);
  int i, col = 0;

  bprintf(buf, "    - - - - ----==== The full command list ====---- - - - -\n\n\r");
  for (i = 0; tabCmd[i].cmd_name[0] != '\0'; i++)
  {
    if (dMob->level < tabCmd[i].level) continue;

    bprintf(buf, " %-16.16s", tabCmd[i].cmd_name);
    if (!(++col % 4)) bprintf(buf, "\n\r");
  }
  if (col % 4) bprintf(buf, "\n\r");
  text_to_mobile(dMob, buf->data);
  buffer_free(buf);
}

void cmd_who(D_MOBILE *dMob, char *arg)
{
  D_MOBILE *xMob;
  D_SOCKET *dsock;
  BUFFER *buf = buffer_new(MAX_BUFFER);

  bprintf(buf, " - - - - ----==== Who's Online ====---- - - - -\n\r");
  for (dsock = dsock_list; dsock; dsock = dsock->next)
  {
    if (dsock->state != STATE_PLAYING) continue;
    if ((xMob = dsock->player) == NULL) continue;

    bprintf(buf, " %-12s   %s\n\r", xMob->name, dsock->hostname);
  }
  bprintf(buf, " - - - - ----======================---- - - - -\n\r");
  text_to_mobile(dMob, buf->data);
  buffer_free(buf);
}

void cmd_help(D_MOBILE *dMob, char *arg)
{
  if (arg[0] == '\0')
  {
    HELP_DATA *pHelp;
    BUFFER *buf = buffer_new(MAX_BUFFER);
    int col = 0;

    bprintf(buf, "      - - - - - ----====//// HELP FILES  \\\\\\\\====---- - - - - -\n\n\r");
    for (pHelp = help_list; pHelp; pHelp = pHelp->next)
    {
      bprintf(buf, " %-19.18s", pHelp->keyword);
      if (!(++col % 4)) bprintf(buf, "\n\r");
    }
    if (col % 4) bprintf(buf, "\n\r");
    bprintf(buf, "\n\r Syntax: help <topic>\n\r");
    text_to_mobile(dMob, buf->data);
    buffer_free(buf);
    return;
  }
  if (!check_help(dMob, arg))
  {
    text_to_mobile(dMob, "Sorry, no such helpfile.\n\r");
    return;
  }
}

void cmd_compress(D_MOBILE *dMob, char *arg)
{
  /* no socket, no compression */
  if (!dMob->socket)
    return;

  /* enable compression */
  if (!dMob->socket->out_compress)
  {
    text_to_mobile(dMob, "Trying compression.\n\r");
    text_to_buffer(dMob->socket, (char *) compress_will2);
    text_to_buffer(dMob->socket, (char *) compress_will);
  }
  else /* disable compression */
  {
    if (!compressEnd(dMob->socket))
    {
      text_to_mobile(dMob, "Failed.\n\r");
      return;
    }
    text_to_mobile(dMob, "Compression disabled.\n\r");
  }
}

void cmd_save(D_MOBILE *dMob, char *arg)
{
  save_player(dMob);
  text_to_mobile(dMob, "Saved.\n\r");
}

void cmd_copyover(D_MOBILE *dMob, char *arg)
{ 
  FILE *fp;
  D_SOCKET *dsock, *dsock_next;
  char buf[100];
  
  if ((fp = fopen(COPYOVER_FILE, "w")) == NULL)
  {
    text_to_mobile(dMob, "Copyover file not writeable, aborted.\n\r");
    return;
  }

  sprintf(buf, "\n\r <*>            The world starts spinning             <*>\n\r");
  
  /* For each playing descriptor, save its state */
  for (dsock = dsock_list; dsock ; dsock = dsock_next)
  {
    dsock_next = dsock->next;
 
    if (dsock->state != STATE_PLAYING)
    {
      text_to_socket(dsock, "\n\rSorry, we are rebooting. Come back in a few minutes.\n\r");
      close_socket(dsock, FALSE);
    }
    else
    {
      fprintf(fp, "%d %s %s %c\n",
        dsock->control, dsock->player->name, dsock->hostname, dsock->compressing);

      /* save the player */
      save_player(dsock->player);

      text_to_socket(dsock, buf);
      compressEnd(dsock);
    }
  }
  
  fprintf (fp, "-1\n");
  fclose (fp);

  /* close any pending sockets */
  recycle_sockets();
  
  /* exec - descriptors are inherited */
  sprintf(buf, "%d", control);
  execl(EXE_FILE, "SocketMud", "copyover", buf, (char *) NULL, (char *) NULL);

  /* Failed - sucessful exec will not return */
  text_to_mobile(dMob, "Copyover FAILED!\n\r");
}

void cmd_linkdead(D_MOBILE *dMob, char *arg)
{
  D_MOBILE *xMob;
  char buf[MAX_BUFFER];

  for (xMob = dmobile_list; xMob; xMob = xMob->next)
  {
    if (!xMob->socket)
    {
      sprintf(buf, "%s is linkdead.\n\r", xMob->name);
      text_to_mobile(dMob, buf);
    }
  }
}
