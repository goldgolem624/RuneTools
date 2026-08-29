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

// Panel registry bootstrap. Panels are spliced BEFORE client.html's own <script>, so the
// registry must already exist when a panel's IIFE calls registerTab at load. client.html
// adopts this same object (window.RTX) and owns the consumers (renderPane, tabEntryKicks,
// paneLeave, the poll tick). def = { id, render, open, close, refresh, label, cat, icon }.
window.RTX = window.RTX || {};
RTX.panels = RTX.panels || {};
window.registerTab = window.registerTab || function (def) {
  if (def && def.id) RTX.panels[def.id] = def;
  return def;
};
