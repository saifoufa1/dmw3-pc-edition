# DMW3 PC Edition - story script, chapter 1 (our own file: edit freely, it is plain text).
#
# The dialogue itself is NOT in this file: lines are references to the original messages that the
# game reads at runtime from YOUR disc data (DAT/COUNTRY/USA). Use `dw3check --dump-text DMG200`
# or `dw3check --dump-text TALK00` (console only - never commit that output) to see the ids.
#   dmg200:4:3     event bank USDMG200.BIN, block 4, message 3
#   talk00:0x13    NPC talk bank USTALK00.BIN, message 0x13
#   "Our own text" short fallback lines written by us ('|' = next page, \n = new line)
#
# ---- top-level lines ------------------------------------------------------------------------
# player "Junpei"                  name used for the player-name codes in the original text
# start STAGE X Y                  where a new game begins
# chapter N "Title"                chapter shown under the objective
# talkbank NN STAGE...             these stages use USTALKNN.BIN for the original NPC lines
# gflag HEX VALUE                  initial value of an ORIGINAL game flag (WSTAG show/talk conditions)
# starter SPRITE "Name"            partner choices for `partner choose`; `beam` uses them when there is no team
# packblock A 3 B 4 ... default N  "$" in a message ref (dmg200:$:15) = the starter pack's own event block
# newgame flags|done|objective ... what the PC Edition New Game starts with (flags set, events marked as done,
#                                  objective shown). The team comes from data/starter_sets.cfg (not downloaded yet).
# gate STAGE TARGET "message" if CONDS     exit STAGE -> TARGET is closed while CONDS hold
# actor ID STAGE talk|fight|boss SPRITE X Y DIR "Name" [level N] [line REF] [enemy SPRITE "Name"] [if CONDS]
#                                  a story NPC, present while CONDS hold. fight/boss = touch to fight;
#                                  enemy = the Digimon that battles for this NPC (default: the NPC itself).
# event NAME on TRIGGER [if CONDS] [repeat]   ...commands...   end
#   triggers: start | talk ACTOR | touch ACTOR | win ACTOR | lose ACTOR | stage STAGE
#             | enter STAGE [X Y W H]
#   events run once unless `repeat`.
# ---- conditions -----------------------------------------------------------------------------
#   flag  !flag  won  lost  answer=N  g:6004=1 (original game flag)
# ---- commands -------------------------------------------------------------------------------
#   say REF ["Speaker" "fallback text"]   |  say "Speaker" "our own text"   ("<player>" = player name)
#   ask QUESTION OPTION1 OPTION2 ...       (sets answer=1..n)
#   if CONDS / else / endif
#   set FLAG...  clear FLAG...  gset HEX VALUE
#   objective "text"    chapter N "title"    title REF|"text"...    blackout on|off
#   partner choose (sets answer=1..n)  |  partner SPRITE "Name"
#   fight ACTOR  (waits for the battle; then `won` / `lost`)
#   warp STAGE [X Y]    place X Y (move the player in this stage)    wait TICKS    save    run EVENT    stop
#   beam                (partner download: the team beams in around the player, then follows him)
#
# Opening chapter (DW3), following the original order (event bank USDMG200 blocks 0-12, reference shots 33-50):
# Junpei (= the account name) logs into Digimon Online and arrives in the Asuka City Main Lobby (S210) WITHOUT
# Digimon. The Tamer Service clerk at the counter downloads the starter pack: the 3 Digimon beam in on the online
# mark on the floor, Teddy and Kail log in, everyone says hello. The lobby's lower-right exit (Central Sector,
# S200) stays closed until then. In the Central Sector Tamer Genji challenges him: the tutorial battle against
# his Kuwagamon. After that fight the story STOPS for now (owner review point): free play in Asuka City.

player "Junpei"
start S210 735 398               # Main Lobby, in front of the Login Room door (reference shot 38)
chapter 1 "Chapter 1 - Asuka City"
talkbank 00 S200 S201 S202 S203 S210 S218 S219 S225 S230
gflag 6002 1                     # original story phase: lobby before the partner download (WSTAG210 show [6002=1])
                                 # 6004 = after the download (set by the event; S210 + S200 NPC variants [6004=1])

# Single-partner test runs (--new-game without a starter pack) beam in these three.
starter P006AGUM "Agumon"
starter P008GIRU "Guilmon"
starter P010PATA "Patamon"
# The download scene has one event block per pack (same scene, the team's own greetings): "$" in a message ref.
packblock A 3 B 4 C 5 default 4

# PC Edition New Game (title menu -> sign-up -> starter pack -> Main Lobby S210): only the login cutscene is
# skipped (the sign-up screens replace it); the partner download and Tamer Genji are played.
newgame done login
newgame objective "Get your partner Digimon at the Tamer Service counter"

gate S210 S200 dmg200:8:1 if !partner_chosen     # "Oh yeah! I need my partner Digimon data!" (shot 39)

# ---- story NPCs ---------------------------------------------------------------------------------
actor service S210 talk P044OPFS 384 255 1 "Tamer Service" line dmg200:6:1
actor teddy   S210 talk P013TAKU 330 326 5 "Teddy" line dmg200:$:21 if partner_chosen !friends_split
actor kail    S210 talk P011MINO 344 314 5 "Kail" line dmg200:$:20 if partner_chosen !friends_split
actor genji   S200 talk P074JIJI 1040 280 5 "Tamer Genji" level 1 enemy M051KUWA "Kuwagamon" if !genji_won
actor genji2  S200 talk P074JIJI 1040 280 5 "Tamer Genji" line dmg200:12:4 if genji_won

# ---- 1. login (only for --new-game test runs; the PC flow's sign-up replaces it) ------------------
event login on start
  blackout on
  title dmg200:1:1
  say dmg200:0:1
  say dmg200:0:2
  say dmg200:0:3
  blackout off
  chapter 1 "Chapter 1 - Asuka City"
  objective "Get your partner Digimon at the Tamer Service counter"
  save
end

# ---- 2. partner download (talk to the Tamer Service, reference shots 42-47) -------------------
event download on talk service if !partner_chosen repeat
  ask dmg200:2:1 dmg200:2:2 dmg200:2:3
  if answer=2
    say dmg200:7:1
    say dmg200:7:2
    stop
  endif
  say dmg200:$:1
  place 284 308                  # "Please wait at the online mark on the floor."
  say dmg200:$:2
  beam
  set partner_chosen
  gset 6004 1
  # Teddy and Kail log in (actors with `if partner_chosen`)
  say dmg200:$:3
  say dmg200:$:4
  say dmg200:$:5
  say dmg200:$:6
  say dmg200:$:7
  say dmg200:$:8
  say dmg200:$:9
  say dmg200:$:10
  say dmg200:$:11
  say dmg200:$:12
  say dmg200:$:13
  say dmg200:$:14
  say dmg200:$:15
  say dmg200:$:16
  say dmg200:$:17
  say dmg200:$:18
  say dmg200:$:19
  say dmg200:$:20
  say dmg200:$:21
  set friends_split
  objective "Go to the Central Sector - the lower-right exit of the lobby"
  save
end

event service_after on talk service if partner_chosen repeat
  say dmg200:18:1
end

# ---- 3. tutorial battle: Tamer Genji and his Kuwagamon (reference shots 48-50) ---------------
event genji_meet on stage S200 if partner_chosen !genji_won repeat
  if !genji_met
    say dmg200:11:1
    say dmg200:11:2
    say dmg200:11:3
    set genji_met
  endif
  fight genji
  run genji_after
end

event genji_talk on talk genji if !genji_won repeat
  say dmg200:11:3
  fight genji
  run genji_after
end

event genji_after on start if never repeat
  if won
    say dmg200:12:1
    say dmg200:12:2
    say dmg200:12:3
    say dmg200:12:4
    say dmg200:12:5
    say dmg200:12:6
    say dmg200:12:7
    say dmg200:12:8
    set genji_won
    # STOP: the story pauses here (owner review point). Free play from now on.
    chapter 1 "Chapter 1 - to be continued"
    objective "Free play - the story continues in a later version"
  else
    say "Tamer Genji" "Ho ho ho. Not bad for a first try.|Rest a little and talk to me again, boy."
  endif
  save
end
