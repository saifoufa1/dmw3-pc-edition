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
# starter SPRITE "Name"            partner choices for `partner choose` (first = default)
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
#   warp STAGE [X Y]    wait TICKS    save    run EVENT    stop
#
# Opening chapter (DW3): Junpei logs into Digimon Online, arrives on the Asuka Server, downloads
# his partner Digimon, meets Teddy and Kail, and is challenged by Tamer Genji, his first battle.
# Story beats/order follow the original event bank USDMG200 (blocks 0-12). Positions are ours:
# the original lobby interior is not linked yet, so the opening plays on the Asuka City plaza.

player "Junpei"
start S200 1000 236
chapter 1 "Chapter 1 - Asuka City"
talkbank 00 S200 S201 S202 S203 S218 S219
gflag 6004 1                     # original story phase of the first visit (NPC variants with show [6004=1])

starter P006AGUM "Agumon"        # USDMG200 block 4 team (Monmon, Agumon, Renamon)
starter P008GIRU "Guilmon"       # block 5 team (Kumamon, Guilmon, Patamon)
starter P010PATA "Patamon"       # block 3 team (Kotemon, Renamon, Patamon)

gate S200 S400 "I should finish things in town first..." if !genji_won
gate S200 * "I need my partner Digimon data first." if !partner_chosen

# ---- story NPCs ---------------------------------------------------------------------------------
actor service S200 talk P044OPFS 968 212 1 "Tamer Service" line dmg200:6:1
actor teddy   S200 talk P013TAKU 1040 262 6 "Teddy" line dmg200:4:21 if partner_chosen !friends_split
actor kail    S200 talk P011MINO 1062 250 6 "Kail" line dmg200:4:20 if partner_chosen !friends_split
actor genji   S200 fight P074JIJI 1088 288 1 "Tamer Genji" level 2 enemy P167SAGM "Genji's Blue Agumon" if friends_split !genji_won
actor genji2  S200 talk P074JIJI 1088 288 1 "Tamer Genji" line dmg200:12:4 if genji_won
actor bagumon S200 fight P171BAGM 196 770 6 "Black Agumon" level 3 if genji_won !bagumon_won

# ---- 1. login -----------------------------------------------------------------------------------
event login on start
  blackout on
  title dmg200:1:1
  say dmg200:0:1
  say dmg200:0:2
  say dmg200:0:3
  blackout off
  chapter 1 "Chapter 1 - Asuka City"
  objective "Get your partner Digimon from the Tamer Service"
  save
end

# ---- 2. partner download (talk to the Tamer Service) ---------------------------------------
event download on talk service if !partner_chosen repeat
  ask dmg200:2:1 dmg200:2:2 dmg200:2:3
  if answer=2
    say dmg200:7:1
    say dmg200:7:2
    stop
  endif
  say dmg200:4:1
  partner choose
  set partner_chosen
  say dmg200:4:2
  # Teddy and Kail log in (they appear now: actors with `if partner_chosen`)
  say dmg200:4:3
  say dmg200:4:4
  say dmg200:4:5
  say dmg200:4:6
  say dmg200:4:7
  say dmg200:4:8
  say dmg200:4:9
  say dmg200:4:10
  say dmg200:4:11
  say dmg200:4:12
  say dmg200:4:13
  say dmg200:4:14
  # the partner team greets Junpei (lines of the chosen team)
  if answer=1
    say dmg200:4:15
    say dmg200:4:16
    say dmg200:4:17
  endif
  if answer=2
    say dmg200:5:15
    say dmg200:5:16
    say dmg200:5:17
  endif
  if answer=3
    say dmg200:3:15
    say dmg200:3:16
    say dmg200:3:17
  endif
  say dmg200:4:18
  say dmg200:4:19
  say dmg200:4:20
  say dmg200:4:21
  set friends_split
  objective "Explore Asuka City - someone near the plaza wants to battle"
  save
end

event service_after on talk service if partner_chosen repeat
  say dmg200:18:1
end

# ---- 3. first story battle: Tamer Genji (touch him to fight) -------------------------------
event genji_meet on touch genji repeat
  if !genji_met
    say dmg200:11:1
    say dmg200:11:2
    say dmg200:11:3
    set genji_met
  endif
  fight genji
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
    objective "Leave town by the west bridge - a wild Digimon blocks the way"
  else
    say "Tamer Genji" "Ho ho ho. Not bad for a first try.|Rest a little and come back, boy."
  endif
  save
end

# ---- 4. second story battle: the wild Black Agumon at the west bridge ----------------------
event bagumon_fight on touch bagumon repeat
  say "Black Agumon" "Grrr... A new tamer?|Show me what your partner can do!"
  fight bagumon
  if won
    set bagumon_won
    say "<player>" "Yeah, I won!|Now nothing blocks the west bridge."
    objective "Cross the west bridge and leave Asuka City"
  else
    say "<player>" "Ouch... It's stronger than it looks.|Let's try again!"
  endif
  save
end

# ---- 5. end of the opening chapter ----------------------------------------------------------
event chapter1_end on stage S400 if bagumon_won
  title "Chapter 1 complete" "To be continued..."
  chapter 2 "Chapter 2 - Outside the city"
  objective "Your first goal: Seiryu City, straight east of Asuka City"
  save
end
