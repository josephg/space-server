//
//  lua.h
//  server
//
//  Created by Joseph Gentle on 15/04/12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#ifndef server_lua_h
#define server_lua_h

#ifdef __cplusplus
extern "C" {
#endif
#include "luajit/lua.h"
#include "luajit/lualib.h"
#include "luajit/lauxlib.h"
#ifdef __cplusplus
}
#endif

#include "game.h"

#ifdef _WIN32
#define LUA_EXPORT extern "C" __declspec(dllexport)
#else
#define LUA_EXPORT
#endif

lua_State *init_lua(Game *game);
void add_ship(Game *game, ObjectId id, cpBody *ship);
void forward_ship_controller_message(Client *client, char *message);
void call_lua_updates(Game *game);
void client_closed(Client *client);
#endif
