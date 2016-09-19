/**
 * @file cnodeserver.c
 * @author Vicent Ferrer Guasch <vicent.ferrer@cumucore.com>
 * @date 19 Sep 2016
 * @brief Asynchronous CNode server
 *
 * The example from the Erlang tutorial http://erlang.org/doc/tutorial/cnode.html
 * rewritten to use libevent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <event2/event.h>

#include "erl_interface.h"
#include "ei.h"

#include "complex.h"

#define BUFSIZE 1000

typedef struct{
  int          fd;                           /* fd to Erlang node */
  ErlConnect   conn;                         /* Connection data */
  struct event *ev;
}ConnT;

/* In a serious implementation, connList would be a hash table */
typedef struct{
  struct event_base *evbase;
  struct event      *listen_ev;
  ConnT             connList[30];
}ServerT;

void process_msg(evutil_socket_t fd, short event, void *arg){
  int got;                                 /* Result of receive */
  unsigned char buf[BUFSIZE];              /* Buffer for incoming message */
  ErlMessage emsg;                         /* Incoming message */
  ServerT *self = (ServerT *)arg;

  ETERM *fromp, *tuplep, *fnp, *argp, *resp;
  int res;

  got = erl_receive_msg(fd, buf, BUFSIZE, &emsg);
  if (got == ERL_TICK) {
    /* ignore */
  } else if (got == ERL_ERROR) {
    fprintf(stderr, "Error received from node %s. Closing\n",
            self->connList[fd].conn.nodename);
    event_del(self->connList[fd].ev);
    close(fd);
    return;
  } else {
    printf("Message from node %s\n", self->connList[fd].conn.nodename);

    if (emsg.type == ERL_REG_SEND) {
      fromp = erl_element(2, emsg.msg);
      tuplep = erl_element(3, emsg.msg);
      fnp = erl_element(1, tuplep);
      argp = erl_element(2, tuplep);

      if (strncmp(ERL_ATOM_PTR(fnp), "foo", 3) == 0) {
        res = foo(ERL_INT_VALUE(argp));
      } else if (strncmp(ERL_ATOM_PTR(fnp), "bar", 3) == 0) {
        res = bar(ERL_INT_VALUE(argp));
      }

      resp = erl_format("{cnode, ~i}", res);
      erl_send(fd, fromp, resp);

      erl_free_term(emsg.from); erl_free_term(emsg.msg);
      erl_free_term(fromp); erl_free_term(tuplep);
      erl_free_term(fnp); erl_free_term(argp);
      erl_free_term(resp);
    }
  }
}

void accept_new_node(evutil_socket_t ss, short event, void *arg){
  int fd;                                  /* fd to Erlang node */
  ErlConnect conn;                         /* Connection data */
  ServerT *self = (ServerT *)arg;

  if ((fd = erl_accept(ss, &conn)) == ERL_ERROR)
      erl_err_quit("erl_accept");
  fprintf(stderr, "Connected to %s\n\r", conn.nodename);
  self->connList[fd].fd = fd;
  self->connList[fd].conn = conn;

  self->connList[fd].ev = event_new(self->evbase, fd, EV_READ|EV_PERSIST,
                                    (event_callback_fn) process_msg, arg);
  evutil_make_socket_nonblocking(fd);
  event_add(self->connList[fd].ev, NULL);
}

int my_listen(int port) {
  int listen_fd = 0;
  struct sockaddr_in addr = {0};
  int on = 1;

  if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    return (-1);

  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(listen_fd, (struct sockaddr*) &addr, sizeof(addr)) < 0)
    return (-1);

  listen(listen_fd, 5);
  return listen_fd;
}

int main(int argc, char **argv) {
  int port;                                /* Listen port number */
  int listen;                              /* Listen socket */

  ServerT self = {0};

  self.evbase = event_base_new();

  port = atoi(argv[1]);

  /*Initialize Erl_Interface*/
  erl_init(NULL, 0);

  /*Initialize C-node*/
  if (erl_connect_init(1, "secretcookie", 0) == -1)
    erl_err_quit("erl_connect_init");

  /* Make a listen socket */
  if ((listen = my_listen(port)) <= 0)
    erl_err_quit("my_listen");

  if (erl_publish(port) == -1)
    erl_err_quit("erl_publish");

  self.listen_ev = event_new(self.evbase, listen, EV_READ|EV_PERSIST,
                              (event_callback_fn) accept_new_node, &self);
  evutil_make_socket_nonblocking(listen);
  event_add(self.listen_ev, NULL);

  event_base_dispatch(self.evbase);

  event_del(self.listen_ev);
  close(listen);
  event_base_free(self.evbase);

  return 1;
}
