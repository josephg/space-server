local M = {}

local ship = ...

print("seph")
setLayout[[
<|||>
-   -
-   -
VVVVV
<VVV>
]]

setLabel"seph"
setColor(255, 0, 0)
print("\n")

--print(M.layout)
--print("ship!")

function splitSpace(message)
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

-- returns a number between -pi and pi
local function normalize(a)
  a = a % (2*math.pi)
  if a > math.pi then return a - 2*math.pi else return a end
end

local targetAngle = -math.pi/2

local function rotate(amt)
  if amt < 0 then -- right
    ship(5, 1).power = 0
    ship(1, 5).power = 0
    ship(1, 1).power = -amt
    ship(5, 5).power = -amt
  else -- left
    ship(5, 1).power = amt
    ship(1, 5).power = amt
    ship(1, 1).power = 0
    ship(5, 5).power = 0
  end
end

function M.message(msg)
  local thing, status = splitSpace(msg)
  --print('lua message', msg)
 
  local p
  if status == 'on' then p = 1 else p = 0 end

  if thing == 'up' then
    ship(1, 4).power = p
    ship(2, 4).power = p
    ship(3, 4).power = p
    ship(4, 4).power = p
    ship(5, 4).power = p
    ship(2, 5).power = p
    ship(3, 5).power = p
    ship(4, 5).power = p
  elseif thing == 'fire' then
    ship(2, 1).power = p
    ship(3, 1).power = p
    ship(4, 1).power = p
    --ship(1, 2).power = p
    --ship(1, 3).power = p
    --ship(5, 2).power = p
    --ship(5, 3).power = p
  elseif thing == 'a' then
    targetAngle = tonumber(status)
  end
end

-- how much we can change w in one tick.
local fmult = DT / 1000

--local maxAA = 6.103092 * 2
local maxAA = 24.412367

local function turnTowards(targetAngle)
  --print('w', ship.w)
  local turn = normalize(targetAngle - ship.a)

  if math.abs(turn) < 0.1 and math.abs(ship.w) < maxAA * fmult then
    rotate(-ship.w / (maxAA * fmult))
    --setLabel"c"
    return
  end

  if turn * ship.w < 0 then -- we're turning away from the target angle
    if turn > 0 then rotate(1) else rotate(-1) end
    --setLabel"a"
  else
    --setLabel"b"
    local discriminant = ship.w^2 - 2 * maxAA * math.abs(turn)

    if discriminant > 0 then
      -- Too fast. turn away from the target.
      if turn > 0 then rotate(-1) else rotate(1) end
    elseif discriminant < 0 then
      if turn > 0 then rotate(1) else rotate(-1) end
    else
      --print('bingo!')
    end
  end

end

function M.update(radar)
  turnTowards(targetAngle)
end

function M.onClient(client)
  --print "onClient"
  --sendToClient("internet!")
  client.setFocus()
end

return M

