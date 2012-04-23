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

KHASH_MAP_INIT_INT(i, int)

typedef enum {
  SHIP,
  BULLET,
  ASTEROID,
  PLANET,
  SUN
} EntityType;

#pragma pack(1)
typedef struct {
  ObjectId id;
  uint8_t type;
  uint8_t model;
  float mass;
  float moment;
} CreateFrame;

typedef struct {
  ObjectId id;
  
  // Using 4-byte floats to cut down on network bandwidth.
  float x, y, angle, vx, vy, w, ax, ay, aw;
} UpdateFrame;

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
  kvec_t(UpdateFrame) updates;
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
  
  // Map from object ID -> the frame# when the client last had an update about that
  // object. 0 if the object is not currently visible but has been visible in the
  // past. Objects are missing from the hash if the client has never seen them.
  khash_t(i) *lastUpdated;
  // This is a list of objects which were visible to the client on the previous
  // snapshot. It is a cache of the objects in lastUpdatedObject which are not zero.
  kvec_t(ObjectId) visibleObjects;
  
  // Stuff for reframing input data
  kvec_t(uv_buf_t) readBuffers;
  int32_t offset;
  int32_t packetLength;
  
  struct Game *game;
} Client;

struct lua_State;
typedef struct Game {
  cpSpace *space;
  int frame;
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

typedef struct {
  ObjectId id;
  UpdateFrame snapshot;
  UpdateFrame lastSnapshot;
  Frame snapshotFrame;
  
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
