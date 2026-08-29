// Shared var ids used by more than one panel. Spliced before the first panel (Dock.cpp kFiles).
// Every consumer still falls back to its literal when this file is absent.
const VB = {
  PREMIER: 50572,          // premier membership tier (varp 10287 & 0x20)
  PENGUIN_POINTS: 4163,    // total penguin points, hard cap 250
};
const VP = {
  LEAGUE: 12314,           // active league number, 0 = none
  FAMILIAR_POUCH: 1831,    // summoned pouch item id, -1/0 = none
  SPELL_POINTS: 1787,      // familiar special-move points, max 60
  ACCOUNT_MODE: 4818,      // bit0 ironman, bit1 hardcore, bit19 in-GIM, bit25 unranked group
};
