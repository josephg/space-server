print "ohhi from lua"

local M = {}

local ffi = require 'ffi'
ffi.cdef[[
  void foo();
  typedef double cpFloat;
  typedef struct {cpFloat x, y;} cpVect;

  typedef struct cpBody cpBody;
  typedef struct Client Client;
  typedef struct Game Game;
  typedef uint32_t ObjectId;

  void reset_acceleration(cpBody *body);
  
  void add_acceleration(cpBody *body, cpFloat fx, cpFloat fy, cpFloat off_x, cpFloat off_y);

  void set_client_avatar(Client *client, ObjectId id);

  void send_to_client(Client *client, const char *message, size_t len);

  void fire_gun(Game *game, cpBody *body, cpFloat vx, cpFloat vy, cpFloat off_x, cpFloat off_y);

  void set_client_focus(Client *client, cpBody *body);
  void set_client_viewport(Client *client, cpFloat x, cpFloat y);

  void set_ship_layout(cpBody *body, const unsigned char *layout);
  void set_ship_label(cpBody *body, const unsigned char *label);
  void set_ship_color(cpBody *body, uint8_t r, uint8_t g, uint8_t b);
  void set_heat(cpBody *body, float heat);

  typedef void (*fptr)();
  struct cpBody {
    fptr velocity_func;
    fptr position_func;
    
    cpFloat m;
    cpFloat m_inv;
    cpFloat i;
    cpFloat i_inv;
    
    cpVect p;
    cpVect v;
    cpVect f;
    
    cpFloat a;
    cpFloat w;
    cpFloat t;
    
    cpVect rot;

    void *data;
    
    cpFloat v_limit;
    cpFloat w_limit;
    
    cpVect v_bias;
    cpFloat w_bias;
    
    void *space;
    
    void *shapeList;
    void *arbiterList;
    void *constraintList;
    
    cpBody *root;
    cpBody *next;
    cpFloat idleTime;
  };

  struct Game {
    void *space;
    int frame;
  };
]]
local C = ffi.C

game = ffi.cast('Game*', game)
  
local ships = {}

local owners = {
  seph = 100,
  sam = 101,

  connor = 102,
  tom = 103, 
  pat = 104,
  sidney = 105,
  taighe = 106,
  alan = 107,
  andrew = 108,
  matt = 109,
  ziyad = 110,
  joe = 111,
  daniel = 112,
  cameron = 113,
  paul = 114,
  alex = 115,
  serkan = 116,
  atanas = 117
}

local engine_mult = 4000
local gun_impulse = 500
local gun_cooldown = 7

local clients = {} -- Map from id -> list of client objects

local function makeSendToClient(id)
  return function(message)
    if clients[id] == nil then return end
    for client in pairs(clients[id]) do
      C.send_to_client(client, message, #message)
    end
  end
end

local function Part(x, y, kind, dir)
  --print('part', x, y, kind)
  local priv = {
    kind = kind,
    dir = dir,
    x = x,
    y = y
  }
  local pub = {
    power = 0
  }
  setmetatable(pub, {
    __index = priv,
    __newindex = function(t,k,v)
      error('cannot change ' .. k)
    end
  })
  return pub
end

local function parseShip(str)
  local x = 1
  local y = 1
  local ship = {}

  local function addPart(...)
    ship[x * 100 + y] = Part(x, y, ...)
  end

  for c in str:gmatch"." do
    --print("c is '".. c.. "'")
    local f = ({
      ["\n"] = function()
        y = y + 1
        x = 0
      end,
      ["G"] = function()
        local dir
        addPart("gun", {0, 1})
      end,
      ["^"] = function()
        addPart("engine", { 0,  1})
      end,
      ["<"] = function()
        addPart("engine", {-1,  0})
      end,
      [">"] = function()
        addPart("engine", { 1,  0})
      end,
      ["V"] = function()
        addPart("engine", { 0, -1})
      end,
      [" "] = function() end
    })[c]
    if f then f() else error("unknown ship part '" .. c .. "'") end
    x = x + 1
  end
  return ship
end

function M.addShip(id, body)
  if body then
    body = ffi.cast('cpBody*', body)
  else
    body = ships[id].body
  end
  --print("body", body)

  --C._BodySetAngVel(body, (id - 100) * 3.14159 / 3.3)

  if ships[id] and ships[id].shutdown then
    ships[id].shutdown()
  end
  ships[id] = nil

  local f, err = loadfile('ships/' .. id .. '.lua')
  if not f then
    print(err .. ". Using default ship instead.")
    f, err = loadfile('ships/default.lua')
    if not f then
      error("could not compile default ship: " .. err)
    end
  end

  local playership = {}
  local ship = {id=id, body=body, parts={}}

  setfenv(f, {
    print = function(...)
      --if id == 101 then return end
      print(id, ...)
    end,
    sendToClient = makeSendToClient(id),
    math = math,
    table = table,
    string = string,
    pairs = pairs,
    ipairs = ipairs,
    tonumber = tonumber,
    ENGINE_MULT = engine_mult,
    GUN_IMPULSE = gun_impulse,
    GUN_COOLDOWN = gun_cooldown,
    DT = 33,
    SNAPSHOT_INTERVAL = 5,
    
    setLabel = function(label)
      C.set_ship_label(ship.body, label)
    end,
    setLayout = function(layout)
      C.set_ship_layout(ship.body, layout)
      ship.parts = parseShip(layout)
    end,
    setColor = function(r, g, b)
      C.set_ship_color(ship.body, r, g, b)
    end
  })

  local safeKeys = {p=true, v=true, a=true, w=true, i=true, i_inv=true, m=true, m_inv=true, rot=true}
  setmetatable(playership, {
    __newindex = function() error("can't change me bro") end,
    __index = function(table, key)
      if key == 'parts' then
        return function() return next, ship.parts, nil end
      elseif safeKeys[key] then
        return body[key]
      else
        error('Cannot access key ' .. key)
      end
    end,
    __call = function(table, x, y)
      return ship.parts[x * 100 + y]
    end
  })

  local status, result = pcall(f, playership)
  if status == false then
    error("could not start ship: " .. result)
  end
  ship.controller = result

  ships[id] = ship
end

local function tileToPixel(e)
  return (e.x - 3) * 10, (3 - e.y) * 10
end

local function clamp(x, min, max)
  if x < min then return min elseif x > max then return max else return x end
end

function M.update()
  for i,s in pairs(ships) do
    local heat = 0
    if s.controller.update then s.controller.update() end

    C.reset_acceleration(s.body)
    for _,e in pairs(s.parts) do
      p = clamp(e.power, 0, 1)
      if e.kind == "engine" and e.power > 0 then
        p = p * engine_mult
        C.add_acceleration(s.body, p * -e.dir[1], p * -e.dir[2], tileToPixel(e))
        heat = heat + e.power
      elseif e.kind == "gun" and e.power > 0.1 then
        priv = getmetatable(e).__index

        if not priv.lastFired then priv.lastFired = -100 end
        p = p * gun_impulse
        if priv.lastFired + gun_cooldown < game.frame then
          C.fire_gun(game, s.body, p * e.dir[1], p * e.dir[2], tileToPixel(e))
          priv.lastFired = game.frame
          heat = heat + 10
        end
      end
    end

    C.set_heat(s.body, heat)
  end
end

local function splitSpace(message)
  local location = message:find(' ')
  if location then
    local first = message:sub(1, location - 1)
    local second = message:sub(location + 1)
    return first, second
  else
    -- No space.
    return message
  end
end

function M.shipDied(id)
  -- We should probably tell the controller about it as well.
  print("lua ship", id, "died")
  ships[id] = nil
end

function M.shipMessage(client, id, message)
  if id == 0 then
    -- The message is requesting login. Ha.
    name, data = splitSpace(message)
    id = owners[name]
    if id then
      clients[id] = clients[id] or {}
      clients[id][client] = true
      C.set_client_avatar(client, id)
      if ships[id] and ships[id].controller and ships[id].controller.onClient then
        lua_c = {
          setFocus = function(x, y)
            if x then
              C.set_client_viewport(client, x, y)
            else
              -- Called with no arguments means the camera should just follow the client.
              C.set_client_focus(client, ships[id].body)
            end
          end
        }
        ships[id].controller.onClient(lua_c)
      end
    else
      print("Unknown user " .. name .. " attempting to connect!?")
      return
    end
  end

  local s = ships[id]

  if message == 'reload' then
    M.addShip(id)
  end

  if s and s.controller.message then s.controller.message(message) end
end

function M.clientClosed(client, id)
  clients[id][client] = nil
end


return M

