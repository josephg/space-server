#include "uv/uv.h"
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UV_ASSERT_OK(loop, x) if((x)) {\
  uv_err_t _err = uv_last_error(loop);\
  fprintf(stderr, "uv error %s: %s\n", uv_err_name(_err), uv_strerror(_err));\
}

void tick(uv_timer_t *handle, int status) {
  Game *game = (Game *)handle->data;
  game_update(game);
}

void connection(uv_stream_t *server, int status) {
  // Maybe move this function into game.c?
  printf("Incoming connection %d\n", status);
  
  uv_tcp_t *client = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
  uv_tcp_init(server->loop, client);

  UV_ASSERT_OK(server->loop, uv_accept(server, (uv_stream_t *)client));
  uv_tcp_keepalive(client, 1, 50);
  uv_tcp_nodelay(client, 0);
  
  Game *game = (Game *)server->data;
  client_connected(game, (uv_stream_t *)client);
}

int main() {
  init_models();
  uv_loop_t *loop = uv_default_loop();
  
  if(loop == NULL) {
    fprintf(stderr, "Loop is null - error initializing libuv\n");
    return 1;
  }
  
  uv_tcp_t tcp;
  memset(&tcp, 0xff, sizeof(tcp));
  uv_tcp_init(loop, &tcp);
  
  struct sockaddr_in sock = {};
  sock.sin_port = htons(8765);
  sock.sin_family = AF_INET;
  printf("Listening on port %d\n", ntohs(sock.sin_port));
  
  UV_ASSERT_OK(loop, uv_tcp_bind(&tcp, sock));
  UV_ASSERT_OK(loop, uv_listen((uv_stream_t *)&tcp, 128, connection));

  Game *game = game_init();
  tcp.data = game;
  
  uv_timer_t timer;
  timer.data = game;
  uv_timer_init(loop, &timer);
  uv_timer_start(&timer, tick, DT, DT);
  uv_set_process_title("Space server");
  
  //  uv_tcp_
  uv_run(loop);

  return 0;
}

