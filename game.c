#define _USE_MATH_DEFINES
#define _CRT_RAND_S
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

//#include <stdbool.h>
#include "chipmunk/chipmunk.h"

#include "game.h"
#include "net.h"
#include "lua.h"


struct {
  int differ;
  int skip;
} stats = {};

// From http://www.cygnus-software.com/papers/comparingfloats/comparingfloats.htm
bool AlmostEqual2sComplement(float A, float B, int maxUlps) {
  // Make sure maxUlps is non-negative and small enough that the
  // default NAN won't compare as equal to anything.
  assert(maxUlps > 0 && maxUlps < 4 * 1024 * 1024);
  int aInt = *(int*)&A;
  // Make aInt lexicographically ordered as a twos-complement int
  if (aInt < 0)
    aInt = 0x80000000 - aInt;
  // Make bInt lexicographically ordered as a twos-complement int
  int bInt = *(int*)&B;
  if (bInt < 0)
    bInt = 0x80000000 - bInt;
  int intDiff = abs(aInt - bInt);
  if (intDiff <= maxUlps)
    return true;
  return false;
}

#define SIMILARD(x, y) (fabs((x) - (y)) < 1e-10)
#define SIMILARF(x, y) (fabs((x) - (y)) < 1e-4)
//#define SIMILARF(x, y) AlmostEqual2sComplement(x, y, 10)
//#define SIMILARD(x, y) AlmostEqual2sComplement((float)x, (float)y, 1)


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
    {{-20, -20}, {-15, 0}, {0, 15}, {15, 0}, {20, -20}},
  },
  { // bullet
    3,
    {{-3, -5}, {0, 5}, {3, -5}},
  },
  { // Debris1
    4,
    {{-20, -20}, {-15, 0}, {0, 0}, {0, -20}},
  },
  { // debris 2
    4,
    {{0,0}, {15, 0}, {20, -20}, {0, -20}}
  },
  { // debris 3
    3,
    {{-15, 0}, {0, 15}, {15, 0}}
  },
};

void init_models() {
  for (int i = 0; i < sizeof(models)/sizeof(models[0]); i++) {
//    for(int v = 0; v < models[i].num_verts; v++) {
//      models[i].verts[v].x *= .7;
//      models[i].verts[v].y *= .7;
//    }
    
    models[i].offset = cpvneg(cpCentroidForPoly(models[i].num_verts, models[i].verts));
  }
}

static PositionRecord sub(PositionRecord a, PositionRecord b) {
  PositionRecord diff = {
    a.x - b.x,
    a.y - b.y,
    a.a - b.a
  };
  return diff;
}

void update_body_snapshot(cpBody *body, void *framep) {
  Frame *frame = (Frame *)framep;
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  
  cpVect p = cpBodyGetPos(body);
  cpVect v = cpBodyGetVel(body);
  cpFloat a = cpBodyGetAngle(body);
  cpFloat w = cpBodyGetAngVel(body);
  
  bool differs = false;
  if(!SIMILARF(data->cddx, data->a.x)
     || !SIMILARF(data->cddy, data->a.y)
     || !SIMILARF(data->cdda, data->w_a)) {
    differs = true;
  } else {
    // Acceleration is in ship coordinates.
    cpVect a_world = cpvrotate(data->a, cpvforangle(data->ca));
    
    // a.x and a.y are relative to the ship.
    cpVect newv = cpvclamp(cpv(data->cdx + a_world.x * FMULT, data->cdy + a_world.y * FMULT), body->v_limit);
    cpFloat w_limit = cpBodyGetAngVelLimit(body);
    cpFloat newda = cpfclamp(data->cda + data->cdda * FMULT, -w_limit, w_limit);
    
    if(!SIMILARF(v.x, newv.x)
       || !SIMILARF(v.y, newv.y)
       || !SIMILARF(w, newda)) {
      //printf("old: %.2f %.2f  new: %.2f %.2f  predicted: %.2f %.2f\n", data->cdx, data->cdy, v.x, v.y, newv.x, newv.y);
      differs = true;
    } else {
      cpFloat newx = data->cx + v.x * FMULT;
      cpFloat newy = data->cy + v.y * FMULT;
      cpFloat newa = data->ca + w * FMULT;
      
      differs = !SIMILARF(p.x, newx)
                || !SIMILARF(p.y, newy)
                || !SIMILARF(a, newa);
      
      if(differs) {
        //printf("old: %.2f %.2f  new: %.2f %.2f  predicted: %.2f %.2f\n", data->cx, data->cy, p.x, p.y, newx, newy);
        //        printf("old %f new %f predicted %f\n", data->ca, a, newa);
      }
    }
  }
  //differs = true;

  if(differs) {
    stats.differ++;
  } else {
    stats.skip++;
  }
  
  char current = *frame % SNAPSHOT_DELAY;

  uint8_t flag = 1 << current;
  data->relevant_snapshots = (data->relevant_snapshots & ~flag) | (differs << current);
  
  data->cx = p.x; data->cy = p.y; data->ca = a;
  data->cdx = v.x; data->cdy = v.y; data->cda = w;
  data->cddx = data->a.x; data->cddy = data->a.y; data->cdda = data->w_a;
  
  if(differs) {
    PositionRecord sp = {p.x, p.y, a};
    PositionRecord sv = {v.x, v.y, w};
    PositionRecord sa = {data->a.x, data->a.y, data->w_a};
    
    data->snapshot[current].p = sp;
    data->snapshot[current].v = sv;
    data->snapshot[current].a = sa;
  }
  
  if (data->type == SHIP) {
    char prev_frame = (*frame - 1) % SNAPSHOT_DELAY;
    bool differs = data->hp_snapshot[prev_frame] != data->hp;
    data->relevant_hp_snapshot = (data->relevant_hp_snapshot & ~flag) | (differs << current);
    data->hp_snapshot[current] = data->hp;
  }
}


void free_snapshot(Snapshot *snapshot) {
  kv_size(snapshot->creates) = 0;
  kv_size(snapshot->updates) = 0;
  kv_size(snapshot->removes) = 0;
  kv_size(snapshot->shipdata) = 0;
}

void add_body_to_set(cpShape *shape, void *s) {
  kh_bodymap_t *bodies = (kh_bodymap_t *)s;
  
  cpBody *body = cpShapeGetBody(shape);
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  if(data == NULL) return;
  int ret;
  khint_t iter = kh_put_bodymap(bodies, data->id, &ret);
  kh_val(bodies, iter) = body;
}

void make_snapshot(Game *game, Client *client, Snapshot *snapshot) {
  snapshot->radar = game->last_radar_frame == game->frame ? (HeatVec *)&game->radar.objects : NULL;
  
  // The bodies that are visible right now
  khash_t(bodymap) *visibleBodies = kh_init_bodymap();
  
  // Update the client's viewport
  if(client->focusedBody) {
    cpVect base = cpBodyGetPos(client->focusedBody);
    client->viewport = cpBBNew(base.x - VIEWPORT_SIZE/2,
                       base.y - VIEWPORT_SIZE/2,
                       base.x + VIEWPORT_SIZE/2,
                       base.y + VIEWPORT_SIZE/2);
  }

  cpSpaceBBQuery(game->space, client->viewport, CP_ALL_LAYERS, CP_NO_GROUP,
                 add_body_to_set, visibleBodies);
  
  // Find all objects visible last frame that aren't visible now.
  for (khiter_t iter = kh_begin(client->visibleObjects); iter < kh_end(client->visibleObjects); iter++) {
    if (!kh_exist(client->visibleObjects, iter)) continue;
    
    ObjectId id = kh_key(client->visibleObjects, iter);
    if (kh_get(bodymap, visibleBodies, id) != kh_end(visibleBodies)) {
      continue;
    }
    
    // The object was visible last frame but isn't visible now. Send an update so it animates
    // off the client's screen & remove it from the client's simulation.
    kv_push(ObjectId, snapshot->removes, id);
    kh_del(intset, client->visibleObjects, iter);
    
    // It might make sense to send an update packet for the disappearing object as well, since it shouldn't
    // disappear until the snapshot ends.
  }

  // Iterate through all visible bodies, potentially updating them.
  for (khint_t iter = kh_begin(visibleBodies); iter < kh_end(visibleBodies); iter++) {
    if (!kh_exist(visibleBodies, iter)) continue;
    
    ObjectId id = kh_key(visibleBodies, iter);
    cpBody *body = kh_val(visibleBodies, iter);
    
    SpaceBodyData *data = (SpaceBodyData *)body->data;
    bool sendShipData = false;
    
    khint_t visibleIter = kh_get_intset(client->visibleObjects, id);
    if (visibleIter == kh_end(client->visibleObjects)) {
      // The object is not visible for the client. Add it.
      CreateFrame create = {
        id,
        data->type,
        data->model,
        //(float)cpBodyGetMass(body),
        //(float)cpBodyGetMoment(body),
        (float)cpBodyGetVelLimit(body),
        (float)cpBodyGetAngVelLimit(body),
        {body->p.x, body->p.y, body->a},
        {body->v.x, body->v.y, body->w},
        {data->a.x, data->a.y, data->w_a},
      };
      
      sendShipData = true;
      
      kv_push(CreateFrame, snapshot->creates, create);
      
      // Mark the client as having seen the body.
      int ret;
      kh_put_intset(client->visibleObjects, id, &ret);
    } else {
      if(data->relevant_snapshots != 0) {
        // Send an update frame for the body. The net code does the hard work for this one.
        kv_push(SpaceBodyData *, snapshot->updates, data);
      }

      sendShipData = data->changed;
    }
    
    if(sendShipData && data->type == SHIP) {
      //printf("shipdata %d %d\n", id, game->frame);
      ShipData shipdata = {
        id,
        {},
        {},
        {data->color[0], data->color[1], data->color[2]}
      };
      strncpy(shipdata.layout, data->layout, sizeof(data->layout));
      strncpy(shipdata.label, data->label, sizeof(data->label));
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
  cpShapeSetElasticity(shape, 0.15);
  
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

  cpBody *body = instantiate_model(MODEL_BULLET, g->next_id++, g->space, 1.5, BULLET);
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  data->spawn_frame = g->frame;
  
  SpaceBodyData *ownerData = (SpaceBodyData *)owner->data;
  data->owner = ownerData->id;
  //data->color[0] = data->color[1] = data->color[2] = 200;
  
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

    // a.x and a.y are relative to the ship.
    body->v = cpvadd(body->v, cpvmult(a, FMULT));
    
    body->w = cpfclamp(body->w + data->w_a * FMULT, -body->w_limit, body->w_limit);
  }
}

typedef struct {
  Game *game;
  bool can_purge; // Don't use the purge list if can_purge is false.
  kvec_t(cpBody *) purge_list;
} BodyUpdateInfo;

void update_body(cpBody *body, void *vdata) {
  BodyUpdateInfo *info = (BodyUpdateInfo *)vdata;
  
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  if (data->type == BULLET && data->spawn_frame + BULLET_SURVIVAL_TIME <= info->game->frame && info->can_purge) {
    kv_push(cpBody *, info->purge_list, body);
  } else if(data->type == SHIP) {
    apply_acceleration(body);
    
    if(info->can_purge && data->dead) {
      kv_push(cpBody *, info->purge_list, body);
    }
  }
}

void remove_shape(cpBody *body, cpShape *shape, void *space) {
  cpSpaceRemoveShape((cpSpace *)space, shape);
  memset(shape, 0, sizeof(*shape));
  cpShapeFree(shape);
}

void add_shape_to_radar(cpShape *shape, void *radar_d) {
  HeatVec *radar = (HeatVec *)radar_d;
  cpBody *body = shape->body;
  // If any shapes are in the world twice, they'll appear twice in the radar!
  // (Fix this once its a problem...)
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  if(data == NULL) return; // Ignore the walls.
  
  if(data->type == SHIP && data->heat > 0) {
    Heat heat = {
      (float)body->p.x,
      (float)body->p.y,
      data->heat, // heat
    };
    data->heat = 0;
    kv_push(Heat, *radar, heat);
  }
}

void clear_changed_flag(cpBody *body, void *unused) {
  SpaceBodyData *data = (SpaceBodyData *)body->data;
  if (data->type == SHIP) data->changed = false;
}

void game_update(Game *game) {
  game->frame++;
  
  bool is_snapshot_frame = game->frame % SNAPSHOT_DELAY == SNAPSHOT_DELAY - 1;
  call_lua_updates(game);
  BodyUpdateInfo info = {game};
  info.can_purge = is_snapshot_frame;// Only allow purging objects on snapshot frames. Its just easier that way.
  cpSpaceEachBody(game->space, update_body, &info);
  
  for(unsigned int i = 0; i < kv_size(info.purge_list); i++) {
    cpBody *body = kv_A(info.purge_list, i);
    
    SpaceBodyData *data = (SpaceBodyData *)body->data;
    // A bit of a hack. Make sure no clients are focussing on the body. If they are, nuke their focus.
    if (data && data->type == SHIP) {
      for (int c = 0; c < kv_size(game->clients); c++) {
        Client *client = kv_A(game->clients, c);
        if (client->focusedBody == body) {
          client->focusedBody = NULL;
        }
      }
    }
    
    cpSpaceRemoveBody(game->space, body);
    cpBodyEachShape(body, remove_shape, game->space);
    if(data) {
      memset(data, 0, sizeof(*data));
      free(data);
    }
    memset(body, 0, sizeof(*body));
    cpBodyFree(body);
  }
  
  cpSpaceStep(game->space, (cpFloat)DT / 1000);
  
  cpSpaceEachBody(game->space, update_body_snapshot, &game->frame);  
  
  if (is_snapshot_frame) {
    // Radar.
    if (game->last_radar_frame < game->frame - RADAR_FRAME_DELAY) {
      kv_size(game->radar.objects) = 0;
      cpSpaceEachShape(game->space, add_shape_to_radar, &game->radar);
      game->last_radar_frame = game->frame;
    }

    // Update clients
    //printf("snapshot!");
    Snapshot snapshot = {};
    for (unsigned int i = 0; i < kv_size(game->clients); i++) {
      Client *c = kv_A(game->clients, i);
      if (c->avatar == 0) continue;
      make_snapshot(game, c, &snapshot);
      // write over network
      write_snapshot(c->stream, &snapshot);
      free_snapshot(&snapshot);
    }
    
    cpSpaceEachBody(game->space, clear_changed_flag, NULL);
    
    if(game->last_radar_frame == game->frame) {
      kv_size(game->radar.blips) = 0;
    }
  }
  
  if(game->frame % 30 == 0) {
    printf("skips: %d differ: %d\n", stats.skip, stats.differ);
    stats.skip = stats.differ = 0;
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

void ship_die(Game *game, cpBody *ship) {
  SpaceBodyData *data = ship->data;
  //printf("BLah dead ship %d\n", data->id);

  data->hp = 0;
  data->dead = true;
  notify_ship_died(game, data->id);
}

int bullet_hit_ship_begin(cpArbiter *arb, cpSpace *space, void *data) {
  CP_ARBITER_GET_BODIES(arb, b, s);
  SpaceBodyData *b_data = (SpaceBodyData *)b->data;
  SpaceBodyData *s_data = (SpaceBodyData *)s->data;
  Game *game = (Game *)data;

  // For the first few frames, don't let the bullet collide with the ship that spawned it.
  return (b_data->owner != s_data->id || game->frame - b_data->spawn_frame > 5);
}

void bullet_hit_ship_post(cpArbiter *arb, cpSpace *space, void *g) {
  Game *game = (Game *)g;
  CP_ARBITER_GET_BODIES(arb, b, s);
  cpFloat energy = cpArbiterTotalKE(arb);
  if(energy < 1000) return;
  SpaceBodyData *data = s->data;
  if(data->dead) return;
  
  int damage = (int)sqrt(energy/2000);
  // Make the player take damage

  ship_took_damage(game, data->id, damage);
  data->hp -= damage;
  if(data->hp <= 0) {
    ship_die(game, s);
  }
}

void add_wall(cpSpace *space, cpVect a, cpVect b) {
  cpBody *staticBody = cpSpaceGetStaticBody(space);
  cpShape *shape = cpSpaceAddShape(space, cpSegmentShapeNew(staticBody, a, b, 100));
  cpShapeSetElasticity(shape, 1);
  cpShapeSetFriction(shape, 0);
}

Game *game_init() {
  Game *g = (Game *)malloc(sizeof(Game));
  g->space = cpSpaceNew();
  g->frame = 1;
  kv_init(g->clients);
  g->next_id = 100;
  g->next_avatar_id = 100;
  
  g->last_radar_frame = 0;
  kv_init(g->radar.objects);
  kv_init(g->radar.blips);
  
  g->L = init_lua(g);
  
  add_wall(g->space, cpv(-3100, -3100), cpv( 3100, -3100));
  add_wall(g->space, cpv( 3100, -3100), cpv( 3100,  3100));
  add_wall(g->space, cpv( 3100,  3100), cpv(-3100,  3100));
  add_wall(g->space, cpv(-3100,  3100), cpv(-3100, -3100));

  for(int i = 0; i < 30; i++) {
    float mass = 50;
    ObjectId id = g->next_id++;
    cpBody *body = instantiate_model(MODEL_SHIP, id, g->space, mass, SHIP);
    
    cpBodySetPos(body, cpv(111 + i/10 * 100, 222 + (i % 10) * 100));
    cpBodySetAngVelLimit(body, 6);
    cpBodySetVelLimit(body, 500);
    
    SpaceBodyData *data = (SpaceBodyData *)body->data;
    data->hp = 100;
    
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
  c->visibleObjects = kh_init(intset);
  
  kv_init(c->readBuffers);
  c->offset = 0;
  c->packetLength = -1;
  
  c->game = game;

  stream->data = c;
  kv_push(Client *, game->clients, c);
  
  uv_read_start(stream, alloc_cb, read_cb);
  return c;
}
