#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* main header file */
#include "mud.h"

void save_pfile           ( D_MOBILE *dMob );
void save_profile         ( D_MOBILE *dMob );
void save_whois           ( D_MOBILE *dMob );

void save_player(D_MOBILE *dMob)
{
  if (!dMob) return;

  save_pfile(dMob);      /* saves the actual player data         */
  save_profile(dMob);    /* saves the players profile            */
  save_whois(dMob);      /* saves the whois data for that player */
}

void save_pfile(D_MOBILE *dMob)
{
  char pName[20];
  char pfile[256];
  FILE *fp;
  int size, i;

  pName[0] = toupper(dMob->name[0]);
  size = strlen(dMob->name);
  for (i = 1; i < size; i++)
    pName[i] = tolower(dMob->name[i]);
  pName[i] = '\0';

  /* open the pfile so we can write to it */
  sprintf(pfile, "../players/%s.pfile", pName);
  if ((fp = fopen(pfile, "w")) == NULL)
  {
    bug("Unable to write to %s's pfile", dMob->name);
    return;
  }

  /* dump the players data into the file */
  fprintf(fp, "Name            %s~\n", dMob->name);
  fprintf(fp, "Level           %d\n",  dMob->level);
  fprintf(fp, "Password        %s~\n", dMob->password);

  /* terminate the file */
  fprintf(fp, "%s\n", FILE_TERMINATOR);
  fclose(fp);
}

D_MOBILE *load_player(char *player)
{
  FILE *fp;
  D_MOBILE *dMob = NULL;
  char pfile[256];
  char pName[20];
  char *word;
  bool done = FALSE, found;
  int i, size;

  pName[0] = toupper(player[0]);
  size = strlen(player);
  for (i = 1; i < size; i++)
    pName[i] = tolower(player[i]);
  pName[i] = '\0';

  /* open the pfile so we can write to it */
  sprintf(pfile, "../players/%s.pfile", pName);     
  if ((fp = fopen(pfile, "r")) == NULL)
    return NULL;

  /* create new mobile data */
  if (dmobile_free == NULL)
  {
    if ((dMob = malloc(sizeof(*dMob))) == NULL)
    {
      bug("Load_player: Cannot allocate memory.");
      abort();
    }
  }
  else
  {
    dMob         = dmobile_free;
    dmobile_free = dmobile_free->next;
  }
  clear_mobile(dMob);

  /* load data */
  word = fread_word(fp);
  while (!done)
  {
    found = FALSE;
    switch (word[0])
    {
      case 'E':
        if (compares(word, "EOF")) {done = TRUE; found = TRUE; break;}
        break;
      case 'L':
        IREAD( "Level",     dMob->level     );
        break;
      case 'N':
        SREAD( "Name",      dMob->name      );
        break;
      case 'P':
        SREAD( "Password",  dMob->password  );
        break;
    }
    if (!found)
    {
      bug("Load_player: unexpected '%s' in %s's pfile.", word, player);
      free_mobile(dMob);
      return NULL;
    }

    /* read one more */
    if (!done) word = fread_word(fp);
  }

  fclose(fp);
  return dMob;
}

/*
 * This function loads a players profile, and stores
 * it in a mobile_data... DO NOT USE THIS DATA FOR
 * ANYTHING BUT CHECKING PASSWORDS OR SIMILAR.
 */
D_MOBILE *load_profile(char *player)
{
  FILE *fp;
  D_MOBILE *dMob = NULL;
  char pfile[256];
  char pName[20];
  char *word;
  bool done = FALSE, found;
  int i, size;

  pName[0] = toupper(player[0]);
  size = strlen(player);
  for (i = 1; i < size; i++)
    pName[i] = tolower(player[i]);
  pName[i] = '\0';

  /* open the pfile so we can write to it */
  sprintf(pfile, "../players/%s.profile", pName);
  if ((fp = fopen(pfile, "r")) == NULL)
    return NULL;

  /* create new mobile data */
  if (dmobile_free == NULL)
  {
    if ((dMob = malloc(sizeof(*dMob))) == NULL)
    {
      bug("Load_profile: Cannot allocate memory.");
      abort();
    }
  }
  else
  {
    dMob         = dmobile_free;
    dmobile_free = dmobile_free->next;
  }
  clear_mobile(dMob);

  /* load data */
  word = fread_word(fp);
  while (!done)
  {
    found = FALSE;
    switch (word[0])
    {
      case 'E':
        if (compares(word, "EOF")) {done = TRUE; found = TRUE; break;}
        break;
      case 'N':
        SREAD( "Name",      dMob->name      );
        break;
      case 'P':
        SREAD( "Password",  dMob->password  );
        break;
    }
    if (!found)
    {
      bug("Load_player: unexpected '%s' in %s's pfile.", word, player);
      free_mobile(dMob);
      return NULL;
    }

    /* read one more */
    if (!done) word = fread_word(fp);
  }

  fclose(fp);
  return dMob;
}


/*
 * This file stores only data vital to load
 * the character, and check for things like
 * password and other such data.
 */
void save_profile(D_MOBILE *dMob)
{
  char pfile[256];
  char pName[20];
  FILE *fp;
  int size, i;
  
  pName[0] = toupper(dMob->name[0]);
  size = strlen(dMob->name);
  for (i = 1; i < size; i++)
    pName[i] = tolower(dMob->name[i]);
  pName[i] = '\0';
  
  /* open the pfile so we can write to it */
  sprintf(pfile, "../players/%s.profile", pName);
  if ((fp = fopen(pfile, "w")) == NULL)
  {
    bug("Unable to write to %s's pfile", dMob->name);
    return;
  }

  /* dump the players data into the file */
  fprintf(fp, "Name           %s~\n", dMob->name);
  fprintf(fp, "Password       %s~\n", dMob->password);

  /* terminate the file */
  fprintf(fp, "%s\n", FILE_TERMINATOR);
  fclose(fp);
}

void save_whois(D_MOBILE *dMob)
{
}
