#define _USE_MATH_DEFINES
#define _CRT_RAND_S
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

//#include <stdbool.h>
#include "chipmunk/chipmunk.h"

#include "game.h"
#include "net.h"
#include "lua.h"

struct {
  int differ;
  int skip;
} stats = {};

KHASH_SET_INIT_INT(intset);
KHASH_MAP_INIT_INT(bodymap, cpBody *);

typedef struct Model {
  int num_verts;
  cpVect verts[20]; // Just has to be enough for the biggest model.
  char *texture; // or null if the model is flat shaded.
  cpVect offset;
} Model;

Model models[] = {
  { // SHip
    5,
    {{-25, -25}, {-20, 0}, {0, 20}, {20, 0}, {25, -25}},
  },
  { // bullet
    3,
    {{-3, -5}, {0, 5}, {3, -5}},
  },
};

void init_models() {
  for (int i = 0; i < sizeof(models)/sizeof(models[0]); i++) {
    models[i].offset = cpvneg(cpCentroidForPoly(models[i].num_verts, models[i].verts));
  }
}

void add_body_to_set(cpShape *shape, void *s) {
  kh_bodymap_t *bodies = (kh_bodymap_t *)s;
  
  cpBody *body = cpShapeGetBody(shape);
  SpaceBodyData *data = (SpaceBodyData *)body->data;

  int ret;
  khint_t iter = kh_put_bodymap(bodies, data->id, &ret);
  kh_val(bodies, iter) = body;
}

UpdateFrame *snapshot_for_body(cpBody *body, Frame frame) {
  SpaceBodyData *data = (SpaceBodyData *)body->data;

  if (data->snapshotFrame != frame) {
    if (data->snapshotFrame == frame - SNAPSHOT_DELAY) {
      data->lastSnapshot = data->snapshot;
    } else {
      // Little sentinal value to mark the last snapshot as invalid.
      data->lastSnapshot.id = 0;
    }
    
    data->snapshotFrame = frame;
    cpVect pos = cpBodyGetPos(body);
    cpVect v = cpBodyGetVel(body);
    // You suck, VC++.
    UpdateFrame snapshot = {data->id,
      pos.x, pos.y, cpBodyGetAngle(body),
      v.x, v.y, cpBodyGetAngVel(body),
      data->a.x, data->a.y, data->w_a
    };
    
    data->snapshot = snapshot;
  }
  
  return &data->snapshot;
}

void free_snapshot(Snapshot *snapshot) {
  kv_size(snapshot->creates) = 0;
  kv_size(snapshot->updates) = 0;
  kv_size(snapshot->removes) = 0;
}

bool similar(float x, float y) {
  return abs(x - y) < 1e-10;
}

void make_snapshot(Game *game, Client *client, Snapshot *snapshot) {
  snapshot->radar = game->last_radar_frame == game->frame ? (HeatVec *)&game->radar : NULL;
  
  // The bodies that are visible right now
  khash_t(bodymap) *visibleBodies = kh_init_bodymap();
  
  if(client->focusedBody) {
    cpVect base = cpBodyGetPos(client->focusedBody);
    client->viewport = cpBBNew(base.x - VIEWPORT_SIZE/2,
                       base.y - VIEWPORT_SIZE/2,
                       base.x + VIEWPORT_SIZE/2,
                       base.y + VIEWPORT_SIZE/2);
  }

  cpSpaceBBQuery(game->space, client->viewport, CP_ALL_LAYERS, CP_NO_GROUP,
                 add_body_to_set, visibleBodies);
  
  //cpSpaceEachShape(game->space, add_body_to_set, visibleBodies);
                 
  // Iterate through all objects visible last frame and check that they're all
  // visible now.
  for (unsigned int i = 0; i < kv_size(client->visibleObjects); i++) {
    ObjectId id = kv_A(client->visibleObjects, i);
    if (kh_get(bodymap, visibleBodies, id) == kh_end(visibleBodies)) {
      // The object was visible last frame but isn't visible now. Remove it from
      // the client's simulation.
      kv_push(ObjectId, snapshot->removes, id);
    }
  }
  
  // Clear the list of what was visible before. We'll add everything to it that we
  // just saw.
  kv_size(client->visibleObjects) = 0;

  // Iterate through all visible bodies, potentially updating them.
  for (khint_t iter = kh_begin(visibleBodies); iter < kh_end(visibleBodies); iter++) {
    if (!kh_exist(visibleBodies, iter)) continue;
    
    ObjectId id = kh_key(visibleBodies, iter);
    cpBody *body = kh_val(visibleBodies, iter);
    
    kv_push(ObjectId, client->visibleObjects, id);
    
    UpdateFrame *s = snapshot_for_body(body, game->frame);
    SpaceBodyData *data = (SpaceBodyData *)body->data;
    bool sendShipData = false;
    
    khint_t lastSeenIter = kh_get_i(client->lastUpdated, id);

    if (lastSeenIter == kh_end(client->lastUpdated)) {
      // The client has never seen the body before. Add a create frame for it.
      CreateFrame create = {
        id,
        data->type,
        data->model,
        (float)cpBodyGetMass(body),
        (float)cpBodyGetMoment(body),
      };
      
      sendShipData = true;
      
      kv_push(CreateFrame, snapshot->creates, create);
      kv_push(UpdateFrame, snapshot->updates, *s);
      
      int ret;
      lastSeenIter = kh_put_i(client->lastUpdated, id, &ret);
      kh_val(client->lastUpdated, lastSeenIter) = game->frame;
    } else {
      SpaceBodyData *data = (SpaceBodyData *)body->data;
      int lastSeen = kh_val(client->lastUpdated, lastSeenIter);
      if (data->lastSnapshot.id && lastSeen) {
        // We *might* be able to skip updating the object. Lets see.
        
        UpdateFrame *prev = &data->lastSnapshot;
        float mult = SNAPSHOT_DELAY * DT / 1000;
        
        if (s->w != prev->w || s->vx != prev->vx || s->vy != prev->vy
            || !similar(s->x, prev->x + mult * prev->vx)
            || !similar(s->y, prev->y + mult * prev->vy)
            || !similar(s->angle, prev->angle + mult * prev->w)) {
          // Nah - there's been a collision or something.
          
          // I could check the acceleration here, but there's no real point. If an object has accelerated,
          // its velocity should be different anyway.
          kv_push(UpdateFrame, snapshot->updates, *s);
          //printf("Warp!\n");
          stats.differ++;
        } else {
          stats.skip++;
        }
        sendShipData = data->changed;
      } else {
        // Its not on the client's screen. We'll just send an update frame and the client
        // will put it back on screen.
        kv_push(UpdateFrame, snapshot->updates, *s);

        int ret;
        lastSeenIter = kh_put_i(client->lastUpdated, id, &ret);
        kh_val(client->lastUpdated, lastSeenIter) = game->frame;
      }
    }
    
    if(sendShipData && data->type == SHIP) {
      //printf("shipdata %d %d\n", id, game->frame);
      ShipData shipdata = {
        id,
        {},
        {},
        {data->color[0], data->color[1], data->color[2]}
      };
      strcpy(shipdata.layout, data->layout);
      strcpy(shipdata.label, data->label);
      memcpy(shipdata.color, data->color, 3);
      
      kv_push(ShipData, snapshot->shipdata, shipdata);
    }
  }
}

LUA_EXPORT void reset_acceleration(cpBody *body) {
  // Called from lua.
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  
  data->a = cpvzero;
  data->w_a = 0;
}

LUA_EXPORT void add_acceleration(cpBody *body, cpFloat fx, cpFloat fy, cpFloat off_x, cpFloat off_y) {
  // Called from lua.
  //printf("add_accel %f, %f\n", off_x, off_y);
  SpaceBodyData *data = (SpaceBodyData *)body->data;

  cpVect f = cpv(fx, fy);
  cpVect off = cpv(off_x, off_y);
  data->a = cpvadd(data->a, cpvmult(f, body->m_inv));
  data->w_a += body->i_inv*cpvcross(off, f);
  
  //printf("%f\n", data->w_a);
  
  //printf("%f %f %f\n", data->a.x, data->a.y, data->w_a);
}


// ... and add it to the space.
cpBody *instantiate_model(ModelIdx i, ObjectId id, cpSpace *space, cpFloat mass, int type) {
  Model *m = &models[i];
  cpFloat moment = cpMomentForPoly(mass, m->num_verts, m->verts, m->offset);
  cpBody *body = cpSpaceAddBody(space, cpBodyNew(mass, moment));
  
  cpShape *shape = cpSpaceAddShape(space, cpPolyShapeNew(body, m->num_verts, m->verts, m->offset));
  shape->collision_type = type;
  
  SpaceBodyData *data = (SpaceBodyData *)malloc(sizeof(SpaceBodyData));
  SpaceBodyData d = {}; *data = d;
  data->type = type;
  data->model = i;
  data->id = id;
  
  body->data = data;
  
  return body;
}

LUA_EXPORT void fire_gun(Game *g, cpBody *owner, cpFloat jx, cpFloat jy, cpFloat off_x, cpFloat off_y) {
  // Called from lua.
  //SpaceBodyData *ownerData = owner->data;

  cpBody *body = instantiate_model(MODEL_BULLET, g->next_id++, g->space, 2, BULLET);
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  data->spawn_frame = g->frame;
  data->owner = owner;
  data->color[0] = data->color[1] = data->color[2] = 200;
  
  cpVect offset = cpvadd(cpv(off_x, off_y), models[data->model].offset);
  cpVect off_world = cpvrotate(offset, owner->rot); // relative to the center of the ship.
  cpBodySetPos(body, cpvadd(owner->p, off_world));
  cpBodySetVel(body, owner->v);
  cpBodySetAngle(body, cpvtoangle(cpv(jx, jy)) - M_PI_2 + owner->a);
  cpBodySetAngVel(body, owner->w);
  
  cpVect j_world = cpvrotate(cpv(jx, jy), owner->rot);
  cpBodyApplyImpulse(body, j_world, cpvzero);
  cpBodyApplyImpulse(owner, cpvneg(j_world), off_world);
  
  cpBodySetAngVelLimit(body, 1000);
}

void apply_acceleration(cpBody *body) {
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  
  if(data->a.x || data->a.y || data->w_a) {
    cpBodyActivate(body);

    // Acceleration is in ship coordinates.
    cpVect a = cpvrotate(data->a, body->rot);
    //  cpVect off_rot = cpvrotate(cpv(off_x, off_y), body->rot);

    // a.x and a.y are relative to the ship.
    body->v = cpvadd(body->v, cpvmult(a, (float)DT/1000));
//    body->v.x += data->a.x * (float)DT/1000;
//    body->v.y += data->a.y * (float)DT/1000;
//printf("%f %f\n", body->w, data->w_a * (float)DT/1000);
    body->w += data->w_a * (float)DT/1000;
  }
}

typedef struct {
  Game *game;
  kvec_t(cpBody *) purge_list;
} BodyUpdateInfo;

void update_body(cpBody *body, void *vdata) {
  BodyUpdateInfo *info = (BodyUpdateInfo *)vdata;
  
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  if (data->type == BULLET && data->spawn_frame <= info->game->frame - 500) {
    kv_push(cpBody *, info->purge_list, body);
  } else {
    apply_acceleration(body);
  }
  
  cpVect p = cpBodyGetPos(body);
  
  // If objects go out of the game world, push them back in!
  cpVect outOfBounds = cpvzero;
  const cpFloat BOUNDS_MULT = 0.2;
  if(p.x < -3000) {
    outOfBounds.x += p.x + 3000;
  } else if(p.x > 3000) {
    outOfBounds.x += p.x - 3000;
  }
  if(p.y < -3000) {
    outOfBounds.y += p.y + 3000;
  } else if(p.y > 3000) {
    outOfBounds.y += p.y - 3000;
  }
  
  if(outOfBounds.x || outOfBounds.y) {
    body->v.x += -outOfBounds.x * BOUNDS_MULT;
    body->v.y += -outOfBounds.y * BOUNDS_MULT;
  }
  
  cpBodySetPos(body, p);
}

void remove_shape(cpBody *body, cpShape *shape, void *space) {
  cpSpaceRemoveShape((cpSpace *)space, shape);
}

void add_shape_to_radar(cpShape *shape, void *radar_d) {
  HeatVec *radar = (HeatVec *)radar_d;
  cpBody *body = shape->body;
  // If any shapes are in the world twice, they'll appear twice in the radar!
  // (Fix this once its a problem...)
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  if (data->type == SHIP) {
    Heat heat = {
      (float)body->p.x,
      (float)body->p.y,
      100, // heat
    };
    
    kv_push(Heat, *radar, heat);
  }
}

void clear_changed_flag(cpBody *body, void *unused) {
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  if (data->type == SHIP) data->changed = false;
}

void game_update(Game *game) {
  game->frame++;
  
  call_lua_updates(game);
  BodyUpdateInfo info = {game};
  cpSpaceEachBody(game->space, update_body, &info);
  
  for(unsigned int i = 0; i < kv_size(info.purge_list); i++) {
    cpSpaceRemoveBody(game->space, kv_A(info.purge_list, i));
    cpBodyEachShape(kv_A(info.purge_list, i), remove_shape, game->space);
  }
  
  cpSpaceStep(game->space, (cpFloat)DT / 1000);
  
  
//  for (int i = 0; i < kv_size(game->clients); i++) {
//    Client *c = kv_A(game->clients, i);
//  }
  
  if (game->frame % SNAPSHOT_DELAY == 0) {
    // Radar.
    if (game->last_radar_frame < game->frame - 20) {
      kv_size(game->radar) = 0;
      cpSpaceEachShape(game->space, add_shape_to_radar, &game->radar);
      game->last_radar_frame = game->frame;
    }

    // Update clients
    //printf("snapshot!");
    for (unsigned int i = 0; i < kv_size(game->clients); i++) {
      Client *c = kv_A(game->clients, i);
      if (c->avatar == 0) continue;
      Snapshot snapshot = {};
      make_snapshot(game, c, &snapshot);
      // write over network
      write_snapshot(c->stream, &snapshot);
      free_snapshot(&snapshot);
    }
    
    cpSpaceEachBody(game->space, clear_changed_flag, NULL);
  }
}

#ifdef _WIN32
static float rand_float(float max) {
  unsigned int x;
  rand_s(&x);
  return (float)x / UINT_MAX * max;
}
#else
float rand_float(float max) {
  return (float)(random()) / INT_MAX * max;
}
#endif

int bullet_hit_ship_begin(cpArbiter *arb, cpSpace *space, void *data) {
  CP_ARBITER_GET_BODIES(arb, b, s);
  SpaceBodyData *b_data = (SpaceBodyData *)b->data;
  Game *game = (Game *)data;

  // For the first few frames, don't let the bullet collide with the ship that spawned it.
  return (b_data->owner != s || game->frame - b_data->spawn_frame > 5);
}

void bullet_hit_ship_post(cpArbiter *arb, cpSpace *space, void *data) {
  CP_ARBITER_GET_BODIES(arb, b, s);
  cpFloat energy = cpArbiterTotalKE(arb);
  if (!energy) return;
  if (energy > 1000) {
    //printf("%f\n", energy/1000);    
  }
}


Game *game_init() {
  Game *g = (Game *)malloc(sizeof(Game));
  g->space = cpSpaceNew();
  g->frame = 1;
  kv_init(g->clients);
  g->next_id = 100;
  g->next_avatar_id = 100;
  
  g->last_radar_frame = 0;
  kv_init(g->radar);
  
  g->L = init_lua(g);

  for(int i = 0; i < 30; i++) {
    float mass = 50;
    ObjectId id = g->next_id++;
    cpBody *body = instantiate_model(MODEL_SHIP, id, g->space, mass, SHIP);
    
    cpBodySetPos(body, cpv(111 + i/10 * 100, 222 + (i % 10) * 100));
    cpBodySetAngVelLimit(body, 6);
    cpBodySetVelLimit(body, 400);
    
    //SpaceBodyData *data = (SpaceBodyData *)body->data;
    //data->color[0] = random() % 256;
    //data->color[1] = random() % 256;
    //data->color[2] = random() % 256;
    
    // this should probably happen automatically.
    add_ship(g, id, body);
  }

  cpSpaceAddCollisionHandler(g->space, BULLET, SHIP,
                             bullet_hit_ship_begin, NULL, bullet_hit_ship_post, NULL, g);

  return g;
}

uv_buf_t alloc_cb(uv_handle_t *handle, size_t suggested_size) {
  //printf("alloc %zu\n", suggested_size);
  return uv_buf_init((char *)malloc(suggested_size), suggested_size);
}

void close_cb(uv_handle_t *handle) {
  printf("A client disconnected\n");
  
  Client *client = (Client *)handle->data;
  Game *game = client->game;
  
  // O(n), but the number of clients is probably small anyway.
  for (unsigned int i = 0; i < kv_size(game->clients); i++) {
    if (kv_A(game->clients, i) == client) {
      kv_A(game->clients, i) = kv_A(game->clients, --kv_size(game->clients));
    }
  }
}

void on_client_data(Client *client, char *packet, size_t length) {
  uint8_t type = packet[0];
  
  switch (type) {
    case CLIENT_MESSAGE:
      //printf("Client data: '%s'\n", &packet[1]);
      forward_ship_controller_message(client, &packet[1]);
      break;
      
    default:
      printf("Unknown data of type %i\n", type);
      break;
  }
  
  free(packet);
}

Client *client_connected(Game *game, uv_stream_t *stream) {
  printf("Client connected! WEEEE!\n");
  
  Client *c = (Client *)malloc(sizeof(Client));
  c->stream = stream;
  c->state = WAITING_FOR_LOGIN;
  c->username = NULL;
  c->viewport = cpBBNew(0,0,0,0);
  c->avatar = 0;
  c->focusedBody = NULL;
  c->lastUpdated = kh_init(i);
  kv_init(c->visibleObjects);
  
  kv_init(c->readBuffers);
  c->offset = 0;
  c->packetLength = -1;
  
  c->game = game;

  stream->data = c;
  kv_init(c->visibleObjects);
  kv_push(Client *, game->clients, c);
  
  uv_read_start(stream, alloc_cb, read_cb);
  return c;
}
