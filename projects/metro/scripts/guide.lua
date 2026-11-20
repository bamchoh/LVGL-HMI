-- The in-car display of the demo: plays a train that runs up and down the line and fills the
-- tags the screen shows. Runs every 200 ms (see "scripts" in project.json).
--
-- The station names, numbers and connections are those of the Osaka Metro Midosuji Line.
-- The minutes between the stations and the side of the doors are made up for the demo.

if not G then
  G = {}
  -- { number, kanji, kana, English, minutes to the next station, doors (1 = this side, 2 = the other), connections }
  -- a connection: { colour (see the chips of the screen: 1-7 metro lines, 8 other railways), Japanese, English }
  G.stations = {
    { 11, "江坂", "えさか", "Esaka", 2, 2, { { 8, "北大阪急行線", "Kita-Osaka Kyuko" } } },
    { 12, "東三国", "ひがしみくに", "Higashimikuni", 2, 1, {} },
    { 13, "新大阪", "しんおおさか", "Shin-Osaka", 2, 2, { { 8, "JR線", "JR Lines" }, { 8, "新幹線", "Shinkansen" } } },
    { 14, "西中島南方", "にしなかじまみなみがた", "Nishinakajima-Minamigata", 3, 1, { { 8, "阪急線", "Hankyu" } } },
    { 15, "中津", "なかつ", "Nakatsu", 2, 2, {} },
    { 16, "梅田", "うめだ", "Umeda", 2, 2,
      { { 1, "谷町線", "Tanimachi" }, { 2, "四つ橋線", "Yotsubashi" }, { 8, "JR線", "JR Lines" }, { 8, "阪急線", "Hankyu" }, { 8, "阪神線", "Hanshin" } } },
    { 17, "淀屋橋", "よどやばし", "Yodoyabashi", 2, 2, { { 8, "京阪線", "Keihan" } } },
    { 18, "本町", "ほんまち", "Hommachi", 2, 1, { { 2, "四つ橋線", "Yotsubashi" }, { 3, "中央線", "Chuo" } } },
    { 19, "心斎橋", "しんさいばし", "Shinsaibashi", 2, 2, { { 6, "長堀鶴見緑地線", "Nagahori Tsurumi-ryokuchi" }, { 2, "四つ橋線", "Yotsubashi" } } },
    { 20, "なんば", "なんば", "Namba", 2, 1,
      { { 2, "四つ橋線", "Yotsubashi" }, { 4, "千日前線", "Sennichimae" }, { 8, "南海線", "Nankai" }, { 8, "近鉄線", "Kintetsu" }, { 8, "JR線", "JR Lines" } } },
    { 21, "大国町", "だいこくちょう", "Daikokucho", 2, 2, { { 2, "四つ橋線", "Yotsubashi" } } },
    { 22, "動物園前", "どうぶつえんまえ", "Dobutsuen-mae", 2, 1, { { 5, "堺筋線", "Sakaisuji" }, { 8, "JR線", "JR Lines" }, { 8, "南海線", "Nankai" }, { 8, "阪堺線", "Hankai" } } },
    { 23, "天王寺", "てんのうじ", "Tennoji", 2, 2, { { 1, "谷町線", "Tanimachi" }, { 8, "JR線", "JR Lines" }, { 8, "近鉄線", "Kintetsu" }, { 8, "阪堺線", "Hankai" } } },
    { 24, "昭和町", "しょうわちょう", "Showacho", 2, 1, {} },
    { 25, "西田辺", "にしたなべ", "Nishitanabe", 2, 1, {} },
    { 26, "長居", "ながい", "Nagai", 2, 2, { { 8, "JR線", "JR Lines" } } },
    { 27, "あびこ", "あびこ", "Abiko", 3, 2, {} },
    { 28, "北花田", "きたはなだ", "Kitahanada", 2, 1, {} },
    { 29, "新金岡", "しんかなおか", "Shinkanaoka", 2, 1, {} },
    { 30, "なかもず", "なかもず", "Nakamozu", 0, 2, { { 8, "南海線", "Nankai" }, { 8, "泉北線", "Semboku" } } },
  }
  G.at = 1            -- the station the train is at, or the one it has left
  G.dir = 1           -- 1 = towards Nakamozu, -1 = towards Esaka
  G.phase = 2         -- 0 running, 1 arriving, 2 at the station
  G.t = 0             -- ms in this phase
  G.last = millis()
  G.RUN, G.ARRIVE, G.STOP, G.TURN = 9000, 4000, 5000, 8000
  G.LANG_MS = 4000    -- how long each language is shown ("auto")
end

local st = G.stations
local now = millis()
local dt = now - G.last
G.last = now
if dt < 0 or dt > 2000 then dt = 200 end

local function terminal() return G.dir == 1 and #st or 1 end
local function turn_round() G.dir = -G.dir; G.phase = 2; G.t = 0 end

-- what the crew asks for (the popup)
local cmd = tag.cmd
if cmd ~= 0 then
  tag.cmd = 0
  if cmd == 1 then                                   -- on to the next station
    if G.at == terminal() then turn_round() end
    G.at = G.at + G.dir; G.phase = 2; G.t = 0
  elseif cmd == 2 then                               -- the other way
    if G.phase ~= 2 then G.at = G.at + G.dir end     -- (between two stations: from the one ahead)
    turn_round()
  elseif cmd == 3 then                               -- back to the first station
    G.at = 1; G.dir = 1; G.phase = 2; G.t = 0
  end
end

-- the journey
if not tag.paused then
  G.t = G.t + dt
  if G.phase == 2 then
    if G.at == terminal() then
      if G.t >= G.TURN then turn_round() end
    elseif G.t >= G.STOP then G.phase = 0; G.t = 0 end
  elseif G.phase == 0 then
    if G.t >= G.RUN then G.phase = 1; G.t = 0 end
  elseif G.t >= G.ARRIVE then
    G.at = G.at + G.dir; G.phase = 2; G.t = 0
  end
end

-- the language: 0 kanji, 1 kana, 2 English
local mode = tag.lang_mode
local lang
if mode == 1 then lang = 0 elseif mode == 2 then lang = 2 else lang = (now // G.LANG_MS) % 3 end
local en = lang == 2
local function name_of(s, l) return s[2 + l] end
local function pick(ja, kana, english) return en and english or (lang == 1 and kana or ja) end

-- the station in the header: where the train is, or the next one
local shown = G.phase == 2 and G.at or G.at + G.dir
local s = st[shown]
local dest = st[terminal()]

tag.lang = lang
tag.phase = G.phase
if G.phase == 0 then tag.progress = math.floor(G.t / G.RUN * 80)
elseif G.phase == 1 then tag.progress = 80 + math.floor(G.t / G.ARRIVE * 20)
else tag.progress = 0 end

tag.line_name = pick("御堂筋線", "みどうすじせん", "Midosuji Line")
tag.dest = en and ("For " .. dest[4]) or (name_of(dest, lang) .. " ゆき")
tag.car = en and "Car 6" or "6号車"
if G.phase == 0 then tag.head = pick("次は", "つぎは", "Next")
elseif G.phase == 1 then tag.head = pick("まもなく", "まもなく", "Arriving at")
elseif G.at == terminal() then tag.head = pick("終点", "しゅうてん", "Last stop")
else tag.head = pick("ただいま", "ただいま", "Now at") end
local name = name_of(s, lang)
tag.name = name
tag.name_small = utf8.len(name) > (en and 12 or 8) and 1 or 0
tag.name_sub = en and s[2] or s[4]
tag.num = tostring(s[1])

-- the route: from the station the train is at (or has left) onwards
local slots, minutes = 0, 0
for i = 0, 6 do
  local k = G.at + G.dir * i
  local x = st[k]
  if x then
    slots = slots + 1
    tag["s" .. i .. "_name"] = name_of(x, lang)
    tag["s" .. i .. "_num"] = tostring(x[1])
    if i == 0 then tag.s0_min = ""
    else
      minutes = minutes + st[G.dir == 1 and k - 1 or k][5]
      tag["s" .. i .. "_min"] = en and (minutes .. " min") or ("約" .. minutes .. "分")
    end
  else
    tag["s" .. i .. "_name"] = ""; tag["s" .. i .. "_num"] = ""; tag["s" .. i .. "_min"] = ""
  end
end
tag.slots = slots

-- connections at the station in the header
local x = s[7]
tag.xfer_title = #x == 0 and "" or (en and "Transfer" or pick("のりかえ", "のりかえ", ""))
for i = 1, 5 do
  local c = x[i]
  tag["x" .. i .. "_color"] = c and c[1] or 0
  tag["x" .. i .. "_text"] = c and (en and c[3] or c[2]) or ""
end

-- the doors (the other side on the way back)
local door = s[6]
if G.dir == -1 then door = 3 - door end
tag.door = G.phase == 0 and 0 or door
if door == 1 then tag.door_text = pick("こちら側の\nドアが開きます", "こちらがわの\nドアが ひらきます", "Doors on this side\nwill open")
else tag.door_text = pick("反対側の\nドアが開きます", "はんたいがわの\nドアが ひらきます", "Doors on the opposite\nside will open") end

local ticker = mode == 2 and "This screen is a demo of the HML runtime.   The minutes and the side of the doors are made up.   Touch the top of the screen for the settings.        "
  or "この画面は HML ランタイムのデモです。　所要時間とドアの向きは架空のものです。　画面の上部に触れると設定が開きます。　　　　"
if tag.ticker ~= ticker then tag.ticker = ticker end      -- (a new text starts the scrolling again)
local clock = os.date("%H:%M")
if tag.clock ~= clock then tag.clock = clock end
