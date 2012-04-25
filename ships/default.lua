local M = {}

local ship = ...

setLayout[[
  G  
<   >
     
<   >
 VVV 
]]
-- Set me!
setLabel"noob"
setColor(100, 100, 100) -- Grey.

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

function M.message(msg)
  local thing, status = splitSpace(msg)
  --print('lua message', msg)
 
  local p
  if status == 'on' then p = 1 else p = 0 end

  if     thing == 'left' then 
    ship(5, 2).power = p/2
    ship(1, 4).power = p/2
  elseif thing == 'right' then 
    ship(1, 2).power = p/2
    ship(5, 4).power = p/2
  elseif thing == 'up' then
    ship(2, 5).power = p
    ship(3, 5).power = p
    ship(4, 5).power = p
  elseif thing == 'fire' then
    ship(3, 1).power = p
  end
end


function M.update(radar)

end


function M.onClient(client)
  client.setFocus()
end

return M

