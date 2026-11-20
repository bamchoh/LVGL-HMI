-- The production line of the demo: plays four machines on a conveyor and fills the tags the
-- screen shows. Runs every 200 ms (see "scripts" in project.json). No device is connected.
--
-- States (m1_state .. m4_state, line_state): 0 stopped, 1 running, 2 waiting, 3 fault.

if not L then
  L = {}
  L.names = { "投入機", "加工機", "検査機", "搬出機" }
  L.texts = { [0] = "停止中", "運転中", "待機中", "故障" }
  L.CYCLE = 3000        -- ms for one piece (the products on the screen take as long from machine to machine)
  L.AGV = 5000          -- ms for the vehicle, one way
  L.FAULT = 12000       -- a fault clears by itself after this (or with "復旧する")
  L.WAIT = 4000         -- how long the loader waits for material now and then
  L.fault = { 0, 0, 0, 0 }     -- ms left of the fault of each machine (0 = none)
  L.wait = 0                   -- ms left of the wait for material
  L.acc = 0                    -- ms into the piece being made
  L.agv_t = 0
  L.on = { 0, 0, 0, 0 }        -- ms each machine has been running, and since the start
  L.all = 0
  L.last = millis()
end

local now = millis()
local dt = now - L.last
L.last = now
if dt < 0 or dt > 2000 then dt = 200 end

-- the popup of a machine: make a fault, clear it
local sel = tag.sel
local cmd = tag.cmd
if cmd ~= 0 then
  tag.cmd = 0
  if cmd == 1 then L.fault[sel] = L.FAULT * 5 end      -- (made by hand: stays long enough to look at)
  if cmd == 2 then L.fault[sel] = 0 end
end

local run = tag.run
local faulty = false
for i = 1, 4 do
  if L.fault[i] > 0 then
    if run then L.fault[i] = math.max(0, L.fault[i] - dt) end
    faulty = faulty or L.fault[i] > 0
  end
end
if L.wait > 0 and run then L.wait = math.max(0, L.wait - dt) end

-- the vehicle: 1 = takes the full pallet away, 2 = comes back empty, 0 = stands at the pallet
local agv = tag.agv
if agv ~= 0 then
  L.agv_t = L.agv_t + dt
  if L.agv_t >= L.AGV then
    L.agv_t = 0
    if agv == 1 then agv = 2; tag.shipped = tag.shipped + 4 else agv = 0 end
  end
end
local pallet = tag.pallet
if pallet >= 4 and agv == 0 then pallet = 0; agv = 1; L.agv_t = 0 end
local full = pallet >= 4                                -- the vehicle is still away: the line waits

local line_run = run and not faulty and L.wait == 0 and not full
if line_run then
  L.acc = L.acc + dt
  if L.acc >= L.CYCLE then
    L.acc = L.acc - L.CYCLE
    tag.m1_count = tag.m1_count + 1
    tag.m2_count = tag.m2_count + 1
    tag.m3_count = tag.m3_count + 1
    if math.random() < 0.06 then tag.ng_count = tag.ng_count + 1       -- the inspection takes it out
    else tag.m4_count = tag.m4_count + 1; pallet = pallet + 1 end
    -- now and then something happens by itself
    local r = math.random()
    if r < 0.03 then L.fault[math.random(1, 4)] = L.FAULT
    elseif r < 0.08 then L.wait = L.WAIT end
  end
end
tag.pallet = pallet
tag.agv = agv
tag.line_run = line_run

-- the states
local line_state = 0
if run then line_state = faulty and 3 or (line_run and 1 or 2) end
tag.line_state = line_state
tag.line_text = L.texts[line_state]
L.all = L.all + dt
for i = 1, 4 do
  local s = 0
  if run then
    if L.fault[i] > 0 then s = 3 elseif line_run then s = 1 else s = 2 end
  end
  tag["m" .. i .. "_state"] = s
  if s == 1 then L.on[i] = L.on[i] + dt end
end

-- the machine in the popup
local s = tag["m" .. sel .. "_state"]
tag.sel_name = L.names[sel]
tag.sel_state = s
tag.sel_text = L.texts[s]
tag.sel_count = tag["m" .. sel .. "_count"]
tag.sel_cycle = s == 1 and L.CYCLE / 1000 or 0
tag.sel_avail = L.all > 0 and math.floor(L.on[sel] / L.all * 100 + 0.5) or 0

local clock = os.date("%H:%M:%S")
if tag.clock ~= clock then tag.clock = clock end
