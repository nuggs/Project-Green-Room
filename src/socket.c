/*
 * This file contains the socket code, used for accepting
 * new connections as well as reading and writing to
 * sockets, and closing down unused sockets.
 */

#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <sys/ioctl.h>
#include <errno.h>

/* including main header file */
#include "mud.h"

/* global variables */
fd_set        fSet;                  /* the socket list for polling       */
D_SOCKET    * dsock_free = NULL;     /* the socket free list              */
D_SOCKET    * dsock_list = NULL;     /* the linked list of active sockets */
D_MOBILE    * dmobile_free = NULL;   /* the mobile free list              */
D_MOBILE    * dmobile_list = NULL;   /* the mobile list of active mobiles */

/* mccp support */
const unsigned char compress_will   [] = { IAC, WILL, TELOPT_COMPRESS,  '\0' };
const unsigned char compress_will2  [] = { IAC, WILL, TELOPT_COMPRESS2, '\0' };
const unsigned char do_echo         [] = { IAC, WONT, TELOPT_ECHO,      '\0' };
const unsigned char dont_echo       [] = { IAC, WILL, TELOPT_ECHO,      '\0' };

/* local procedures */
void game_loop    ( int control );

/* intialize shutdown state */
bool shut_down = FALSE;
int  control;

/*
 * This is where it all starts, nothing special.
 */
int main(int argc, char **argv)
{
  bool fCopyOver;

  /* get the current time */
  current_time = time(NULL);

  /* note that we are booting up */
  log("Program starting.");

  if (argv[1] && argv[1][0])
  {
    fCopyOver = TRUE;
    control = atoi(argv[2]);
  }
  else fCopyOver = FALSE;

  /* initialize the socket */
  if (!fCopyOver)
    control = init_socket();

  /* load all external data */
  load_muddata(fCopyOver);

  /* main game loop */
  game_loop(control);

  /* close down the socket */
  close(control);

  /* terminated without errors */
  log("Program terminated without errors.");

  return 0;
}

void game_loop(int control)   
{
  D_SOCKET *dsock;
  D_SOCKET *dsock_next;
  struct timeval tv;
  extern fd_set fSet;
  time_t check_timer = 0;
  fd_set rFd;
  bool wait_state;
  int fmax;

  if ((fmax = getdtablesize()) < 1)
  {
    bug("Game_loop: Fatal error in getdtablesize()");
    return;
  }

  /* clear out the file socket set */
  FD_ZERO(&fSet);

  /* add control to the set */
  FD_SET(control, &fSet);

  /* copyover recovery */
  for (dsock = dsock_list; dsock; dsock = dsock->next)
    FD_SET(dsock->control, &fSet);

  /* do this untill the program is shutdown */
  while (!shut_down)
  {
    /* reset the wait_state */
    wait_state = FALSE;

    /* copy the socket set */
    memcpy(&rFd, &fSet, sizeof(fd_set));

    /*
     * Set the default timeout to 0.5 seconds,
     * to make sure we can call the update function
     * roughly once every second (can be a little off).
     */
    tv.tv_sec  = 0;
    tv.tv_usec = 500000;

    /* wait for something to happen */
    if (select(fmax, &rFd, NULL, NULL, &tv) < 0)
      continue;

    /* check for new connections */
    if (FD_ISSET(control, &rFd))
    {
      struct sockaddr_in sock;
      unsigned int socksize;
      int newConnection;

      socksize = sizeof(sock);
      if ((newConnection = accept(control, (struct sockaddr*) &sock, &socksize)) >=0)
        new_socket(newConnection);
    }

    /* poll sockets in the socket list */
    for (dsock = dsock_list; dsock; dsock = dsock_next)
    {
      dsock_next = dsock->next;

      /*
       * If the socket still is in the set,
       * but we are unable to read from it,
       * we close it.   
       */
      if (FD_ISSET(dsock->control, &rFd) && !read_from_socket(dsock))
      {
        close_socket(dsock, FALSE);
        continue;
      }

      /* Is the socket allowed to do a new command ? */
      if (dsock->waitstate > 0)
      {
        if (wait_state) dsock->waitstate--;
        continue;
      }

      /* Ok, check for a new command */
      next_cmd_from_buffer(dsock);

      /* Is there a new command pending ? */
      if (dsock->next_command[0] != '\0')
      {
        /* figure out how to deal with the incoming command */
        switch(dsock->state)
        {
          default:
            bug("Descriptor in bad state.");
            break;
          case STATE_NEW_NAME:
          case STATE_NEW_PASSWORD:
          case STATE_VERIFY_PASSWORD:
          case STATE_ASK_PASSWORD:
            handle_new_connections(dsock, dsock->next_command);
            break;
          case STATE_PLAYING:
            handle_cmd_input(dsock, dsock->next_command);
            break;
        }

        dsock->next_command[0] = '\0';
      }

      /* if the player quits */
      if (dsock->state == STATE_CLOSED) continue;

      /*
       * Send all new data to the socket   
       * and close it if any errors occour.
       */
      if (!flush_output(dsock))
        close_socket(dsock, FALSE);
    }

    /*
     * We get here at least once every 0.5 seconds,
     * so it should be possible to hit once every
     * second with some accuracy.
     */
    if (((current_time = time(NULL)) - check_timer) >= 1)
    {
      wait_state = TRUE;         /* make sure all sockets have their wait_state decreased */
      check_timer = time(NULL);  /* resets the check_timer                                */
      update_handler();          /* calls the top-level update function                   */
    }

    /* recycle sockets */
    recycle_sockets();
  }
}

/*
 * Init_socket()
 *
 * Used at bootup to get a free
 * socket to run the server from.
 */
int init_socket()
{
  struct sockaddr_in my_addr;
  int sockfd, reuse = 1;

  /* let's grab a socket */
  sockfd = socket(AF_INET, SOCK_STREAM, 0);

  /* setting the correct values */
  my_addr.sin_family = AF_INET;
  my_addr.sin_addr.s_addr = INADDR_ANY;
  my_addr.sin_port = htons(MUDPORT);

  /* this actually fixes any problems with threads */
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) == -1)
  {
    perror("Error in setsockopt()");
    exit(1);
  } 

  /* bind the port */
  bind(sockfd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr));

  /* start listening already :) */
  listen(sockfd, 3);

  /* return the socket */
  return sockfd;
}

/* 
 * New_socket()
 *
 * Initializes a new socket, get's the hostname
 * and puts it in the active socket_list.
 */
bool new_socket(int sock)
{
  struct sockaddr_in   sock_addr;
  pthread_attr_t       attr;
  pthread_t            thread_lookup;
  LOOKUP_DATA        * lData;
  D_SOCKET           * sock_new;
  int                  argp = 1;
  socklen_t            size;

  /* initialize threads */
  pthread_attr_init(&attr);   
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  /*
   * allocate some memory for a new socket if
   * there is no free socket in the free_list
   */
  if (dsock_free == NULL)
  {
    if ((sock_new = malloc(sizeof(*sock_new))) == NULL)
    {
      bug("New_socket: Cannot allocate memory for socket.");
      abort();
    }
  }
  else
  {
    sock_new   = dsock_free;
    dsock_free = dsock_free->next;
  }

  /* attach the new connection to the socket list */
  FD_SET(sock, &fSet);

  /* clear out the socket */
  clear_socket(sock_new, sock);

  /* set the socket as non-blocking */
  ioctl(sock, FIONBIO, &argp);

  /* update the linked list of sockets */
  sock_new->next  =  dsock_list;
  dsock_list      =  sock_new;

  /* do a host lookup */
  size = sizeof(sock_addr);
  if (getpeername(sock, (struct sockaddr *) &sock_addr, &size) < 0)
  {
    perror("New_socket: getpeername");
    sock_new->hostname = strdup("unknown");
  }
  else
  {
    /* set the IP number as the temporary hostname */
    sock_new->hostname = strdup(inet_ntoa(sock_addr.sin_addr));

    if (!compares(sock_new->hostname, "127.0.0.1"))
    {
      /* allocate some memory for the lookup data */
      if ((lData = malloc(sizeof(*lData))) == NULL)
      {
        bug("New_socket: Cannot allocate memory for lookup data.");
        abort();
      }

      /* Set the lookup_data for use in lookup_address() */
      lData->buf    =  strdup((char *) &sock_addr.sin_addr);
      lData->dsock  =  sock_new;

      /* dispatch the lookup thread */
      pthread_create(&thread_lookup, &attr, &lookup_address, (void*) lData);
    }
    else sock_new->lookup_status++;
  }

  /* negotiate compression */
  text_to_buffer(sock_new, (char *) compress_will2);
  text_to_buffer(sock_new, (char *) compress_will);

  /* send the greeting */
  text_to_buffer(sock_new, greeting);
  text_to_buffer(sock_new, "What is your name? ");

  /* everything went as it was supposed to */
  return TRUE;
}

/*
 * Close_socket()
 *
 * Will close one socket directly, freeing all
 * resources and making the socket availably on
 * the socket free_list.
 */
void close_socket(D_SOCKET *dsock, bool reconnect)
{
  if (dsock->lookup_status > TSTATE_DONE) return;
  dsock->lookup_status += 2;

  /* remove the socket from the polling list */
  FD_CLR(dsock->control, &fSet);

  if (dsock->state == STATE_PLAYING)
  {
    if (reconnect)
      text_to_socket(dsock, "This connection has been taken over.\n\r");
    else if (dsock->player)
    {
      dsock->player->socket = NULL;
      log("Closing link to %s", dsock->player->name);
    }
  }
  else if (dsock->player)
    free_mobile(dsock->player);

  /* set the closed state */
  dsock->state = STATE_CLOSED;
}

/* 
 * Read_from_socket()
 *
 * Reads one line from the socket, storing it
 * in a buffer for later use. Will also close
 * the socket if it tries a buffer overflow.
 */
bool read_from_socket(D_SOCKET *dsock)
{
  int size;
  extern int errno;

  /* check for buffer overflows, and drop connection in that case */
  size = strlen(dsock->inbuf);
  if (size >= sizeof(dsock->inbuf) - 2)
  {
    text_to_socket(dsock, "\n\r!!!! Input Overflow !!!!\n\r");
    return FALSE;
  }

  /* start reading from the socket */
  for (;;)
  {
    int sInput;
    int wanted = sizeof(dsock->inbuf) - 2 - size;

    sInput = read(dsock->control, dsock->inbuf + size, wanted);

    if (sInput > 0)
    {
      size += sInput;

      if (dsock->inbuf[size-1] == '\n' || dsock->inbuf[size-1] == '\r')
        break;
    }
    else if (sInput == 0)
    {
      log("Read_from_socket: EOF");
      return FALSE;
    }
    else if (errno == EAGAIN || sInput == wanted)
      break;
    else
    {
      perror("Read_from_socket");
      return FALSE;
    }     
  }
  dsock->inbuf[size] = '\0';
  return TRUE;
}

/*
 * Text_to_socket()
 *
 * Sends text directly to the socket,
 * will compress the data if needed.
 */
bool text_to_socket(D_SOCKET *dsock, const char *txt)
{
  int iBlck, iPtr, iWrt = 0, length, control = dsock->control;

  length = strlen(txt);

  /* write compressed */
  if (dsock && dsock->out_compress)
  {
    dsock->out_compress->next_in  = (unsigned char *) txt;
    dsock->out_compress->avail_in = length;

    while (dsock->out_compress->avail_in)
    {
      dsock->out_compress->avail_out = COMPRESS_BUF_SIZE - (dsock->out_compress->next_out - dsock->out_compress_buf);

      if (dsock->out_compress->avail_out)
      {
        int status = deflate(dsock->out_compress, Z_SYNC_FLUSH);

        if (status != Z_OK)
        return FALSE;
      }

      length = dsock->out_compress->next_out - dsock->out_compress_buf;
      if (length > 0)
      {
        for (iPtr = 0; iPtr < length; iPtr += iWrt)
        {
          iBlck = UMIN(length - iPtr, 4096);
          if ((iWrt = write(control, dsock->out_compress_buf + iPtr, iBlck)) < 0)
          {
            perror("Text_to_socket (compressed):");
            return FALSE;
          }
        }
        if (iWrt <= 0) break;
        if (iPtr > 0)
        {
          if (iPtr < length)
            memmove(dsock->out_compress_buf, dsock->out_compress_buf + iPtr, length - iPtr);

          dsock->out_compress->next_out = dsock->out_compress_buf + length - iPtr;
        }
      }
    }
    return TRUE;
  }

  /* write uncompressed */
  for (iPtr = 0; iPtr < length; iPtr += iWrt)
  {
    iBlck = UMIN(length - iPtr, 4096);
    if ((iWrt = write(control, txt + iPtr, iBlck)) < 0)
    {
      perror("Text_to_socket:");
      return FALSE;
    }
  }

  return TRUE;
}

/*
 * Text_to_buffer()
 *
 * Stores outbound text in a buffer, where it will
 * stay untill it is flushed in the gameloop.
 *
 * Will also parse ANSI colors and other tags.
 */
void text_to_buffer(D_SOCKET *dsock, const char *txt)
{
  static char output[8 * MAX_OUTPUT];
  char *ptr;
  bool color = FALSE;
  int size;

  /* clear the output buffer, and set the pointer */
  output[0] = '\0';
  ptr = output;

  /* always start with a leading space */
  if (dsock->top_output == 0)
  {
    dsock->outbuf[0] = '\n';
    dsock->outbuf[1] = '\r';
    dsock->top_output = 2;
  }

  while (*txt != '\0')
  {
    switch(*txt)
    {
      default:
        *ptr++ = *txt++;
        break;
      case '#':
        switch(*++txt)
        {
          default:
            *ptr++ = '#';
            break;
          case 'n':  // stock color
            txt++; color = FALSE;
            *ptr++ = 27;   *ptr++ = '[';   *ptr++ = '0';
            *ptr++ = 'm';
            break;
          case 'd':  // Dark
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '0';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '0';   *ptr++ = 'm';
            break;
          case 'D':  // Bold Dark
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '1';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '0';   *ptr++ = 'm';
            break;
          case 'r':  // Red
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '0';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '1';   *ptr++ = 'm';
            break;
          case 'R':  // Bold Red 
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '1';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '1';   *ptr++ = 'm';
            break;
          case 'g':  // Green
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '0';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '2';   *ptr++ = 'm';
            break;
          case 'G':  // Bold Green
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '1';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '2';   *ptr++ = 'm';
            break;
          case 'y':  // Yellow
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '0';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '3';   *ptr++ = 'm';
            break;
          case 'Y':  // Bold Yellow
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '1';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '3';   *ptr++ = 'm';
            break;
          case 'b':  // Blue
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '0';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '4';   *ptr++ = 'm';
            break;
          case 'B':  // Bold Blue
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '1';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '4';   *ptr++ = 'm';
            break;
          case 'p':  // Pink
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '0';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '5';   *ptr++ = 'm';
            break;
          case 'P':  // Bold Pink
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '1';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '5';   *ptr++ = 'm';
            break;
          case 'c':  // Cyan
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '0';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '6';   *ptr++ = 'm';
            break;
          case 'C':  // Bold Cyan
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '1';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '6';   *ptr++ = 'm';
            break;
          case 'w':  // White
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '0';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '7';   *ptr++ = 'm';
            break;
          case 'W':  // Bold White
            txt++; color = TRUE;
            *ptr++ = 27;    *ptr++ = '[';
            *ptr++ = '1';   *ptr++ = ';';
            *ptr++ = '3';   *ptr++ = '7';   *ptr++ = 'm';
            break;
        }
    }
  }

  /* and terminate it with the standard color (White) */
  if (color)
  {
    *ptr++ = 27;
    *ptr++ = '[';
    *ptr++ = '0';
    *ptr++ = 'm';
  }
  *ptr = '\0';

  size = strlen(output);

  if (dsock->top_output + size >= MAX_OUTPUT)
  {
    bug("Text_to_buffer: ouput overflow on %s.", dsock->hostname);
    return;
  }
  strcpy(dsock->outbuf + dsock->top_output, output);
  dsock->top_output += size;
}

/*
 * Text_to_mobile()
 *
 * If the mobile has a socket, then the data will
 * be send to text_to_buffer().
 */
void text_to_mobile(D_MOBILE *dMob, const char *txt)
{
  if (dMob->socket)
  {
    text_to_buffer(dMob->socket, txt);
    dMob->socket->bust_prompt = TRUE;
  }
}

void next_cmd_from_buffer(D_SOCKET *dsock)
{
  int size = 0, i = 0, j = 0, telopt = 0;

  /* if theres already a command ready, we return */
  if (dsock->next_command[0] != '\0')
    return;

  /* if there is nothing pending, then return */
  if (dsock->inbuf[0] == '\0')
    return;

  /* check how long the next command is */
  while (dsock->inbuf[size] != '\0' && dsock->inbuf[size] != '\n' && dsock->inbuf[size] != '\r')
    size++;

  /* we only deal with real commands */
  if (dsock->inbuf[size] == '\0')
    return;

  /* copy the next command into next_command */
  for ( ; i < size; i++)
  {
    if (dsock->inbuf[i] == (signed char) IAC)
    {
      telopt = 1;
    }
    else if (telopt == 1 && (dsock->inbuf[i] == (signed char) DO || dsock->inbuf[i] == (signed char) DONT))
    {
      telopt = 2;
    }
    else if (telopt == 2)
    {
      telopt = 0;

      if (dsock->inbuf[i] == (signed char) TELOPT_COMPRESS)         /* check for version 1 */
      {
        if (dsock->inbuf[i-1] == (signed char) DO)                  /* start compressing   */
          compressStart(dsock, TELOPT_COMPRESS);
        else if (dsock->inbuf[i-1] == (signed char) DONT)           /* stop compressing    */
          compressEnd(dsock);
      }
      else if (dsock->inbuf[i] == (signed char) TELOPT_COMPRESS2)   /* check for version 2 */
      {
        if (dsock->inbuf[i-1] == (signed char) DO)                  /* start compressing   */
          compressStart(dsock, TELOPT_COMPRESS2);
        else if (dsock->inbuf[i-1] == (signed char) DONT)           /* stop compressing    */
          compressEnd(dsock);
      }
    }
    else if (isprint(dsock->inbuf[i]) && isascii(dsock->inbuf[i]))
    {
      dsock->next_command[j++] = dsock->inbuf[i];
    }
  }
  dsock->next_command[j] = '\0';

  /* skip forward to the next line */
  while (dsock->inbuf[size] == '\n' || dsock->inbuf[size] == '\r')
  {
    dsock->bust_prompt = TRUE;   /* seems like a good place to check */
    size++;
  }

  /* use i as a static pointer */
  i = size;

  /* move the context of inbuf down */
  while (dsock->inbuf[size] != '\0')
  {
    dsock->inbuf[size - i] = dsock->inbuf[size];
    size++;
  }
  dsock->inbuf[size - i] = '\0';
}

bool flush_output(D_SOCKET *dsock)
{
  /* nothing to send */
  if (dsock->top_output <= 0 && !(dsock->bust_prompt && dsock->state == STATE_PLAYING))
    return TRUE;

  /* bust a prompt */
  if (dsock->state == STATE_PLAYING && dsock->bust_prompt)
  {
    text_to_buffer(dsock, "\n\rSocketMud:> ");
    dsock->bust_prompt = FALSE;
  }

  /* reset the top pointer */
  dsock->top_output = 0;

  /*
   * Send the buffer, and return FALSE
   * if the write fails.
   */
  if (!text_to_socket(dsock, dsock->outbuf))
    return FALSE;

  /* Success */
  return TRUE;
}

void handle_new_connections(D_SOCKET *dsock, char *arg)
{
  D_MOBILE *p_new;

  switch(dsock->state)
  {
    default:
      bug("Handle_new_connections: Bad state.");
      break;
    case STATE_NEW_NAME:
      if (dsock->lookup_status != TSTATE_DONE)
      {
        text_to_buffer(dsock, "Making a dns lookup, please have patience.\n\rWhat is your name? ");
        return;
      }
      if (!check_name(arg)) /* check for a legal name */
      {
        text_to_buffer(dsock, "Sorry, that's not a legal name, please pick another.\n\rWhat is your name? ");
        break;
      }
      arg[0] = toupper(arg[0]);
      log("%s is trying to connect.", arg);

      /* Check for a new Player */
      if ((p_new = load_profile(arg)) == NULL)
      {
        if (dmobile_free == NULL)
        {
          if ((p_new = malloc(sizeof(*p_new))) == NULL)
          {
            bug("Handle_new_connection: Cannot allocate memory.");
            abort();
          }
        }
        else
        {
          p_new        = dmobile_free; 
          dmobile_free = dmobile_free->next;
        }
        clear_mobile(p_new);

        /* give the player it's name */
        p_new->name = strdup(arg);

        /* prepare for next step */
        text_to_buffer(dsock, "Please enter a new password: ");
        dsock->state = STATE_NEW_PASSWORD;
      }
      else /* old player */
      {
        /* prepare for next step */
        text_to_buffer(dsock, "What is your password? ");
        dsock->state = STATE_ASK_PASSWORD;
      }
      text_to_buffer(dsock, (char *) dont_echo);

      /* socket <-> player */
      p_new->socket = dsock;
      dsock->player = p_new;
      break;
    case STATE_NEW_PASSWORD:
      if (strlen(arg) < 5 || strlen(arg) > 12)
      {
        text_to_buffer(dsock, "Between 5 and 12 chars please!\n\rPlease enter a new password: ");
        return;
      }
      dsock->player->password = strdup(crypt(arg, dsock->player->name));
      text_to_buffer(dsock, "Please verify the password: ");
      dsock->state = STATE_VERIFY_PASSWORD;
      break;
    case STATE_VERIFY_PASSWORD:
      if (compares(crypt(arg, dsock->player->name), dsock->player->password))
      {
        text_to_buffer(dsock, (char *) do_echo);

        /* put him in the list */
        dsock->player->next = dmobile_list;
        dmobile_list        = dsock->player;

        log("New player: %s has entered the game.", dsock->player->name);

        /* and into the game */
        dsock->state = STATE_PLAYING;
        text_to_buffer(dsock, motd);
      }
      else
      {
        free(dsock->player->password);
        text_to_buffer(dsock, "Password mismatch!\n\rPlease enter a new password: ");
        dsock->state = STATE_NEW_PASSWORD;
      }
      break;
    case STATE_ASK_PASSWORD:
      text_to_buffer(dsock, (char *) do_echo);
      if (compares(crypt(arg, dsock->player->name), dsock->player->password))
      {
        if ((p_new = check_reconnect(dsock->player->name)) != NULL)
        {
          /* attach the new player */
          ex_free_mob(dsock->player);
          dsock->player = p_new;
          p_new->socket = dsock;

          log("%s has reconnected.", dsock->player->name);

          /* and let him enter the game */
          dsock->state = STATE_PLAYING;
          text_to_buffer(dsock, "You take over a body already in use.\n\r");
        }
        else if ((p_new = load_player(dsock->player->name)) == NULL)
        {
          text_to_socket(dsock, "ERROR: Your pfile is missing!\n\r");
          ex_free_mob(dsock->player);
          dsock->player = NULL;
          close_socket(dsock, FALSE);
          return;
        }
        else
        {
          /* attach the new player */
          ex_free_mob(dsock->player);
          dsock->player = p_new;
          p_new->socket = dsock;

          /* put him in the active list */
          p_new->next   =  dmobile_list;
          dmobile_list  =  p_new;

          log("%s has entered the game.", dsock->player->name);

          /* and let him enter the game */
          dsock->state = STATE_PLAYING;
          text_to_buffer(dsock, motd);
        }
      }
      else
      {
        text_to_socket(dsock, "Bad password!\n\r");
        ex_free_mob(dsock->player);
        dsock->player = NULL;
        close_socket(dsock, FALSE);
      }
      break;
  }
}

void clear_socket(D_SOCKET *sock_new, int sock)
{
  static D_SOCKET sock_empty;

  *sock_new                =  sock_empty;
  sock_new->control        =  sock;
  sock_new->state          =  STATE_NEW_NAME;
  sock_new->lookup_status  =  TSTATE_LOOKUP;
  sock_new->player         =  NULL;
  sock_new->waitstate      =  0; 
  sock_new->top_output     =  0;
}

/* does the lookup, changes the hostname, and dies */
void *lookup_address(void *arg)
{
  LOOKUP_DATA *lData = (LOOKUP_DATA *) arg;
  struct hostent *from = 0;
  struct hostent ent;
  char buf[16384];
  int err;

  /* do the lookup and store the result at &from */
  gethostbyaddr_r(lData->buf, sizeof(lData->buf), AF_INET, &ent, buf, 16384, &from, &err);

  /* did we get anything ? */
  if (from && from->h_name)
  {
    free(lData->dsock->hostname);
    lData->dsock->hostname = strdup(from->h_name);
  }

  /* set it ready to be closed or used */
  lData->dsock->lookup_status++;

  /* free the lookup data */
  free(lData->buf);
  free(lData);

  /* and kill the thread */
  pthread_exit(0);
}

void recycle_sockets()
{
  D_SOCKET *dsock, *dsock_next;

  for (dsock = dsock_list; dsock; dsock = dsock_next)
  {
    dsock_next = dsock->next;
    if (dsock->lookup_status != TSTATE_CLOSED) continue;

    /* remove the socket from the socket list */
    if (dsock == dsock_list)
      dsock_list = dsock->next;
    else
    {
      D_SOCKET *prev;

      for (prev = dsock_list; prev && prev->next != dsock; prev = prev->next)
        ;
      if (prev)
        prev->next = dsock->next;
      else
        bug("Recycle_sockets: Closed socket not in list");
    }

    /* close the socket */
    close(dsock->control);

    /* free the memory */
    free(dsock->hostname);

    /* stop compression */
    compressEnd(dsock);

    /* put the socket in the free_list */
    dsock->next = dsock_free;
    dsock_free  = dsock;
  }
}
