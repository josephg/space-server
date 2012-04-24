#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//#include <readline/readline.h>
#include "lua.h"
#include "net.h"

void _BodySetAngVel(cpBody *body, cpFloat value) {
  cpBodyActivate(body);
  cpBodyAssertSane(body);
  body->w = value;
}

void call_lua_updates(Game *game) {
  lua_State *L = game->L;
  
  if (!L) {
    return;
  }
  
  lua_getfield(L, -1, "update");
  int error = lua_pcall(L, 0, 0, 0);
  if (error) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

lua_State *init_lua(Game *game) {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  
  int error = luaL_loadfile(L, "ship.lua");
  if (error) {
    fprintf(stderr, "Error loading ship.lua: %s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
    lua_close(L);
    return NULL;	
  }
  
  lua_getfenv(L, -1);
  lua_pushlightuserdata(L, game);
  //  Game **storedGame = lua_newuserdata(L, sizeof(Game *));
  //  *storedGame = game;
  lua_setfield(L, -2, "game");
  
  lua_pop(L, 1); // pop the function's environment off the stack
  
  error = lua_pcall(L, 0, 1, 0);
  if (error) {
    fprintf(stderr, "Error running ship.lua: %s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
    lua_close(L);
    return NULL;
  }
  
  // Leaving the result (the ship module) on the stack.
  
  return L;
}

void add_ship(Game *game, ObjectId id, cpBody *ship) {
  lua_State *L = game->L;
  
  if (!L) {
    return;
  }
  
  // Duplicate the controller
  lua_getfield(L, -1, "addShip");
  lua_pushinteger(L, id);
  //cpBody **body = (cpBody **)lua_newuserdata(L, sizeof(cpBody *));
  //*body = ship;
  lua_pushlightuserdata(L, ship);
  int error = lua_pcall(L, 2, 0, 0);
  if (error) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

LUA_EXPORT void set_ship_layout(cpBody *body, char *layout) {
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  if(data->type != SHIP) return;
  if(strncmp(data->layout, layout, sizeof(data->layout))) {
    data->changed = true; // Don't set changed unless something actually changed!
    strncpy(data->layout, layout, sizeof(data->layout));
  }
}

LUA_EXPORT void set_ship_label(cpBody *body, char *label) {
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  if(data->type != SHIP) return;
  if(strncmp(data->label, label, sizeof(data->label))) {
    strncpy(data->label, label, sizeof(data->label));
    data->changed = true;
  }
}

LUA_EXPORT void set_ship_color(cpBody *body, uint8_t r, uint8_t g, uint8_t b) {
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  if(data->type != SHIP) return;
  if(data->color[0] != r || data->color[1] != g || data->color[2] != b) {
    data->color[0] = r;
    data->color[1] = g;
    data->color[2] = b;
    data->changed = true;
  }
}

LUA_EXPORT void set_client_avatar(Client *client, ObjectId id) {
  client->avatar = id;
  send_set_avatar(client);
}

LUA_EXPORT void set_client_focus(Client *client, cpBody *body) {
  client->focusedBody = body;
}

LUA_EXPORT void set_client_viewport(Client *client, cpFloat x, cpFloat y) {
  client->focusedBody = NULL;
  client->viewport = cpBBNew(x - VIEWPORT_SIZE/2, y - VIEWPORT_SIZE/2, x + VIEWPORT_SIZE/2, y + VIEWPORT_SIZE/2);
}

LUA_EXPORT void set_heat(cpBody *body, float heat) {
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  data->heat += heat / RADAR_FRAME_DELAY;
}

void forward_ship_controller_message(Client *client, char *message) {
  Game *game = client->game;
  ObjectId id = client->avatar;
  lua_State *L = game->L;
  
  if (!L) {
    return;
  }
  
  lua_getfield(L, -1, "shipMessage");
  lua_pushlightuserdata(L, client);
  lua_pushinteger(L, id);
  lua_pushstring(L, message);
  int error = lua_pcall(L, 3, 0, 0);
  if (error) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
  }  
}

void client_closed(Client *client) {
  Game *game = client->game;
  ObjectId id = client->avatar;
  lua_State *L = game->L;

  if (!id || !L) {
    return;
  }
  
  lua_getfield(L, -1, "clientClosed");
  lua_pushlightuserdata(L, client);
  lua_pushinteger(L, id);
  int error = lua_pcall(L, 2, 0, 0);
  if (error) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}
/*
int repl() {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  
  char *line;
  
  int error;
  while ((line = readline("> ")) != NULL) {
    add_history(line);
    
    char buf[1000] = "return ";
    strcpy(&buf[strlen(buf)], line);
    error = luaL_loadbuffer(L, buf, strlen(buf), "line");
    
    if (error) {
      error = luaL_loadbuffer(L, line, strlen(line), "line");
      if (error) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        continue;
      }
    }
    
    free(line);
    
    //lua_pushnumber(L, 1.0);
    //lua_pushnumber(L, 5.0);
    error = lua_pcall(L, 0, 1, 0);
    
    if (error) {
      fprintf(stderr, "%s\n", lua_tostring(L, -1));
      lua_pop(L, 1);
      continue;
    }
    
    int type = lua_type(L, -1);
    switch(type) {
      case LUA_TSTRING:
        printf("%s\n", lua_tostring(L, -1));
        break;
      case LUA_TBOOLEAN:
        printf(lua_toboolean(L, -1) ? "true" : "false");
        break;
      case LUA_TNUMBER:
        printf("%g\n", lua_tonumber(L, -1));
        break;
      case LUA_TTABLE:
        // Iterate through all the elements in the table, douchebags.
        printf("table\n");
        
        break;
      case LUA_TNIL:
        break;
      default:
        printf("%s\n", lua_typename(L, -1));
        break;
    }
    lua_pop(L, 1);
  }
  
  lua_close(L);
  
  return 0;
}
*/