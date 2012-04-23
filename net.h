//
//  net.h
//  server
//
//  Created by Joseph Gentle on 12/04/12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#ifndef server_net_h
#define server_net_h
#include "uv/uv.h"
#include "game.h"

enum {
  SNAPSHOT = 100,
  SERVER_LUA_MESSAGE = 101, // This is for server-> client lua messages.
  SET_AVATAR = 102,
  
  CLIENT_MESSAGE = 200,
};

void send_set_avatar(Client *client);
void write_snapshot(uv_stream_t *stream, Snapshot *snapshot);
void read_cb(uv_stream_t *stream, ssize_t nread, uv_buf_t buf);
#endif
