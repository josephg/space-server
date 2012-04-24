//
//  game.h
//  server
//
//  Created by Joseph Gentle on 12/04/12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#ifndef server_game_h
#define server_game_h

#define DT 33
#define SNAPSHOT_DELAY 5
#define VIEWPORT_SIZE 1024
static const float FMULT = (float)DT/1000;

#include "chipmunk/chipmunk.h"

#include "khash.h"
#include "kvec.h"
#include "uv/uv.h"
#include <stdint.h>

#ifndef __cplusplus
#include <stdbool.h>
#endif

typedef uint32_t ObjectId;
typedef uint32_t Frame;

KHASH_SET_INIT_INT(intset);

typedef enum {
  SHIP,
  BULLET,
  ASTEROID,
  PLANET,
  SUN
} EntityType;

typedef struct {
  float x, y, a;
} PositionRecord;

#pragma pack(1)
typedef struct {
  ObjectId id;
  uint8_t type;
  uint8_t model;
  //float mass; // No longer used.
  //float moment;
  float v_limit;
  float a_limit;
  PositionRecord p, v, a;
} CreateFrame;

// For the radar
typedef struct {
  float x, y;
  float heat;
} Heat;

typedef struct {
  ObjectId id;
  char layout[5*6];
  char label[8];
  uint8_t color[3];
} ShipData;
#pragma pack()

// VC++ you really suck.
typedef kvec_t(Heat) HeatVec;

typedef struct {
  HeatVec *radar;
  kvec_t(CreateFrame) creates;
  kvec_t(struct SpaceBodyData_t *) updates;
  kvec_t(ObjectId) removes;
  kvec_t(ShipData) shipdata;
} Snapshot;

struct uv_stream_s;

struct Game;

typedef enum {
    WAITING_FOR_LOGIN,
    OK
} ClientState;

typedef struct Client {
  struct uv_stream_s *stream;
  
  ClientState state;
  
  char *username;
  
  cpBB viewport;
  
  ObjectId avatar;
  // If this is set, updates are centered around it.
  cpBody *focusedBody;
  
  // Set of all the objects that were visible to the client during the last snapshot
  // frame.
  khash_t(intset) *visibleObjects;

  // Stuff for reframing input data
  kvec_t(uv_buf_t) readBuffers;
  int32_t offset;
  int32_t packetLength;
  
  struct Game *game;
} Client;

struct lua_State;
typedef struct Game {
  cpSpace *space;
  Frame frame;
  kvec_t(Client *) clients;
  int next_id;
  uint32_t next_avatar_id;
  
  int last_radar_frame;
  kvec_t(Heat) radar;
  
  struct lua_State *L;
} Game;


typedef enum {
  MODEL_SHIP,
  MODEL_BULLET
} ModelIdx;

typedef struct SpaceBodyData_t{
  ObjectId id;
  
  // The difference between the object's values and its values in the previous frame.
  // This mirrors what clients would predict watching the body.

//  cpFloat x, y, angle;
//  cpFloat dx, dy, da;
//  cpFloat ddx, ddy, dda;

  // Where the client thinks everything is
  cpFloat cx, cy, ca;
  cpFloat cdx, cdy, cda;
  float cddx, cddy, cdda; // Ship coordinates
  
  // 1 in each bit if the snapshot data for that frame should be sent in the next snapshot
  // frame.
  uint8_t relevant_snapshots;
  struct {
    PositionRecord p;
    PositionRecord v;
    PositionRecord a;
  } snapshot[SNAPSHOT_DELAY];
  
  // Set by a ship's engines
  cpVect a;
  cpFloat w_a;
  
  EntityType type;
  ModelIdx model;
  
  union {
    // For bullets.
    struct {
      int spawn_frame;
      cpBody *owner;
    };
    
    struct {
      // For ships
      int hp;
      char layout[5*6];
      char label[8];
      uint8_t color[3];
      bool changed;
    };
  };
  
  // Ships are dead when they run out of HP. Bullets die as soon as they hit something.
  bool dead;
} SpaceBodyData;

void init_models();
void close_cb(uv_handle_t *handle);
Game *game_init();
void game_update(Game *game);
void on_client_data(Client *client, char *packet, size_t length);
Client *client_connected(Game *game, struct uv_stream_s *stream);

#endif
