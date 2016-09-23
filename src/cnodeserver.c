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
  ei_cnode          ec;
  ConnT             connList[30];
}ServerT;

void handle_msg(int fd, erlang_msg *emsg, ei_x_buff *buf1){
  int i = 0, j= 0, arity = 0;
  unsigned long arg = 0;
  unsigned char funAtom[MAXATOMLEN+1] = {0};
  int res;
  char *s;

  ei_decode_version(buf1->buff, &i, NULL);

  j = i;
  s = malloc(BUFSIZE);
  bzero(s, BUFSIZE);
  if(ei_s_print_term(&s, buf1->buff, &j)==-1){
    fprintf(stderr,"Error 1\n");
    return;
  }
  printf("Received term : %s\n", s);
  free(s);

  if (emsg->msgtype == ERL_REG_SEND) {
    ei_decode_tuple_header(buf1->buff, &i, &arity);
    if(arity != 3){
      fprintf(stderr, "Wrong message format 1, arity =%d\n", arity);
      return;
    }
    ei_skip_term(buf1->buff, &i); /* call atom*/
    ei_skip_term(buf1->buff, &i); /* from*/
    ei_decode_tuple_header(buf1->buff, &i, &arity);
    if(arity != 2){
      fprintf(stderr, "Wrong message format 2\n");
      return;
    }
    ei_decode_atom(buf1->buff, &i, funAtom);
    ei_decode_ulong(buf1->buff, &i, &arg);

    if (strncmp(funAtom, "foo", 3) == 0) {
      res = foo(arg);
    } else if (strncmp(funAtom, "bar", 3) == 0) {
      res = bar(arg);
    }

    /*Send Response*/
    ei_x_buff rsp;
    ei_x_new(&rsp);
    ei_x_format(&rsp, "{cnode, ~i}", res);
    if(ei_send(fd, &emsg->from, rsp.buff, rsp.index)<0){
      fprintf(stderr,"ei_send\n");
    }
    ei_x_free(&rsp);
  }
}

void process_msg(evutil_socket_t fd, short event, void *arg){
  int got;                                 /* Result of receive */
  ServerT *self = (ServerT *)arg;

  ei_x_buff buf1;
  erlang_msg emsg = {0};                         /* Incoming message */

  ei_x_new(&buf1);
  got = ei_xreceive_msg(fd, &emsg, &buf1);
  if (got == ERL_TICK) {
    /* ignore */
      /*Send Req*/
      ei_x_buff req;
      ei_x_new(&req);
      ei_x_format(&req, "{cnode, ~p, {efoo, ~i}}", ei_self(&self->ec), 10);
      if(ei_reg_send(&self->ec, fd, "test", req.buff, req.index)<0){
          fprintf(stderr,"ei_send\n");
      }
      ei_x_free(&req);
  } else if (got == ERL_ERROR) {
    fprintf(stderr, "Error received from node %s. Closing\n",
            self->connList[fd].conn.nodename);
    event_del(self->connList[fd].ev);
    close(fd);
    return;
  } else if (got == ERL_MSG){
    printf("Message from node %s\n", self->connList[fd].conn.nodename);
    handle_msg(fd, &emsg, &buf1);
  }
  ei_x_free(&buf1);
}

void accept_new_node(evutil_socket_t ss, short event, void *arg){
  int fd;                                  /* fd to Erlang node */
  ErlConnect conn;                         /* Connection data */
  ServerT *self = (ServerT *)arg;

  if ((fd = ei_accept(&self->ec, ss, &conn)) == ERL_ERROR)
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

  /* /\*Initialize Erl_Interface*\/ */
  /* erl_init(NULL, 0); */

  /*Initialize C-node*/
  if (ei_connect_init(&self.ec, "c1", "secretcookie", 0) == -1)
    erl_err_quit("ei_connect_init");

  /* Make a listen socket */
  if ((listen = my_listen(port)) <= 0)
    erl_err_quit("my_listen");

  if (ei_publish(&self.ec, port) == -1)
    erl_err_quit("ei_publish");

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
