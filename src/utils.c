/*
 * This file contains all sorts of utility functions used
 * all sorts of places in the code.
 */
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>

/* include main header file */
#include "mud.h"

/*
 * Check to see if a given name is
 * legal, returning FALSE if it
 * fails our high standards...
 */
bool check_name(const char *name)
{
  int size, i;

  if ((size = strlen(name)) < 3 || size > 12)
    return FALSE;

  for (i = 0 ;i < size; i++)
    if (!isalpha(name[i])) return FALSE;

  return TRUE;
}

void clear_mobile(D_MOBILE *dMob)
{
  static D_MOBILE clear_mobile;

  *dMob            =  clear_mobile;
  dMob->name       =  NULL;
  dMob->password   =  NULL;
  dMob->level      =  LEVEL_PLAYER;
}

void free_mobile(D_MOBILE *dMob)
{
  D_MOBILE *xMob;

  if (dMob == dmobile_list)
    dmobile_list = dMob->next;
  else
  {
    for (xMob = dmobile_list; xMob && xMob->next != dMob; xMob = xMob->next)
      ;

    if (xMob == NULL)
    {
      bug("Free_mobile: Mobile not found.");
      return;
    }
    xMob->next = dMob->next;
  }

  /* reset the socket */
  if (dMob->socket)
    dMob->socket->player = NULL;

  ex_free_mob(dMob);
}

void ex_free_mob(D_MOBILE * dMob)
{
  /*
   * clear out all strings,
   * remember NULL strings are OK.
   */
  free(dMob->name);
  free(dMob->password);

  /* put it back in the free list */
  dMob->next   = dmobile_free;
  dmobile_free = dMob;
}

void communicate(D_MOBILE *dMob, char *txt, int range)
{
  D_MOBILE *xMob;
  char buf[MAX_BUFFER];
  char message[MAX_BUFFER];

  /* capitalize leading letter */
  txt[0] = toupper(txt[0]);

  switch(range)
  {
    default:
      bug("Communicate: Bad Range %d.", range);
      return;
    case COMM_LOCAL:  // everyone is in the same room for now...
      sprintf(message, "%s says '%s'.\n\r", dMob->name, txt);
      sprintf(buf, "You say '%s'.\n\r", txt);
      text_to_mobile(dMob, buf);
      for (xMob = dmobile_list; xMob; xMob = xMob->next)
      {
        if (xMob == dMob) continue;
        text_to_mobile(xMob, message);
      }
      break;
    case COMM_LOG:
      sprintf(message, "[LOG: %s]\n\r", txt);
      for (xMob = dmobile_list; xMob; xMob = xMob->next)
      {
        if (!IS_ADMIN(xMob)) continue;
        text_to_mobile(xMob, message);
      }
      break;
  }
}

/*
 * Loading of help files, areas, etc, at boot time.
 */
void load_muddata(bool fCopyOver)
{  
  load_helps();

  /* copyover */
  if (fCopyOver)
    copyover_recover();
}

char *get_time()
{
  static char buf[16];
  char *strtime;
  int i;

  strtime = ctime(&current_time);
  for (i = 0; i < 15; i++)   
    buf[i] = strtime[i + 4];
  buf[15] = '\0';

  return buf;
}

/* Recover from a copyover - load players */
void copyover_recover()
{     
  D_MOBILE *dMob;
  D_SOCKET *dsock;
  FILE *fp;
  unsigned char telopt;
  char name [100];
  char host[MAX_BUFFER];
  int desc;
      
  log("Copyover recovery initiated");
   
  if ((fp = fopen(COPYOVER_FILE, "r")) == NULL)
  {  
    log("Copyover file not found. Exitting.");
    exit (1);
  }
      
  /* In case something crashes - doesn't prevent reading */
  unlink(COPYOVER_FILE);
    
  for (;;)
  {  
    fscanf(fp, "%d %s %s %c\n", &desc, name, host, &telopt);
    if (desc == -1)
      break;

    dsock = malloc(sizeof(*dsock));
    clear_socket(dsock, desc);
  
    dsock->hostname     =  strdup(host);
    dsock->next         =  dsock_list;
    dsock_list          =  dsock;
 
    /* re-enable compression if it was enabled before */
    if (telopt == TELOPT_COMPRESS || telopt == TELOPT_COMPRESS2)
      compressStart(dsock, telopt);

    /* load player data */
    if ((dMob = load_player(name)) != NULL)
    {
      /* attach to socket */
      dMob->socket     =  dsock;
      dsock->player    =  dMob;
  
      /* attach to mobile list */
      dMob->next       =  dmobile_list;
      dmobile_list     =  dMob;  
    }
    else /* ah bugger */
    {
      close_socket(dsock, FALSE);
      continue;
    }
   
    /* Write something, and check if it goes error-free */
    if (!text_to_socket(dsock, "\n\r <*>  And before you know it, everything has changed  <*>\n\r"))
    { 
      close_socket(dsock, FALSE);
      continue;
    }
  
    /* make sure the socket can be used */
    dsock->bust_prompt    =  TRUE;
    dsock->lookup_status  =  TSTATE_DONE;
    dsock->state          =  STATE_PLAYING;
  }
  fclose(fp);
}     

D_MOBILE *check_reconnect(char *player)
{
  D_MOBILE *dMob;

  for (dMob = dmobile_list; dMob; dMob = dMob->next)
  {
    if (compares(dMob->name, player))
    {
      if (dMob->socket)
        close_socket(dMob->socket, TRUE);
      return dMob;
    }
  }
  return NULL;
}
