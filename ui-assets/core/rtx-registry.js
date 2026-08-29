// rtx-registry.js: Tab registry: DUNG_ENABLED/NETPROBE_ENABLED, TABS, RTX + registerTab, CAT_META, SKILL_NAMES, fmtGp, XP tables, TAB_GROUPS, tabCat, railCategories.
// Loads after: rtx-ui.js (uiCatHidden at call time only); loads BEFORE rtx_vars.js and the panels so registerTab exists when each panel IIFE registers.
// Plain script, page globals by design: every top-level name here is a page global that panels and the other core files use.
  const DUNG_ENABLED = true;
  // Server Packets (panel_netprobe.js): the inbound protocol view + live framer feed.
  // false -> hidden, true -> shown.
  const NETPROBE_ENABLED = false;
  const TABS = [
    // Character
    { id: 'player',   label: 'Skills',       cat: 'Character', icon: '<path d="M5 20v-5M12 20V8M19 20v-9"/>' },
    { id: 'info',     label: 'Player State', cat: 'Character', icon: '<path d="M12 19s-6-4-6-8.5A3 3 0 0 1 12 8a3 3 0 0 1 6 2.5C18 15 12 19 12 19z"/>' },
    { id: 'perks',    label: 'Perks',        cat: 'Combat', icon: '<path d="M12 2l3 6 6 .5-4.5 4 1.4 6L12 15.6 6.1 18.5l1.4-6L3 8.5 9 8z"/>' },
    { id: 'buffs',    label: 'Buffs',        cat: 'Combat', icon: '<path d="M12 3l7 2.5V11c0 4.5-3 7.5-7 9-4-1.5-7-4.5-7-9V5.5z"/>' },
    { id: 'familiar', label: 'Familiar',     cat: 'Combat', icon: '<circle cx="8.5" cy="7" r="1.7"/><circle cx="15.5" cy="7" r="1.7"/><circle cx="5.5" cy="11" r="1.7"/><circle cx="18.5" cy="11" r="1.7"/><path d="M12 11.5c2.6 0 4.5 1.8 4.5 4 0 1.7-1.2 2.8-2.7 2.8-1 0-1.3-.5-1.8-.5s-.8.5-1.8.5c-1.5 0-2.7-1.1-2.7-2.8 0-2.2 1.9-4 4.5-4z"/>' },
    { id: 'dung', label: 'Dungeoneering', cat: 'Character', icon: '<circle cx="9" cy="9" r="3.2"/><path d="M11.5 11.5 L18 18"/><path d="M15.5 15.5 L17.5 13.5"/><path d="M17 17 L19 15"/>' },
    { id: 'reputation',label: 'Reputation',   cat: 'Character', icon: '<path d="M12 3l7 2.5V11c0 4.5-3 7.5-7 9-4-1.5-7-4.5-7-9V5.5z"/><path d="M9.5 11.5l1.8 1.8L15 9.5"/>' },
    { id: 'wardrobe', label: 'Wardrobe', cat: 'Character', icon: '<path d="M12 3l3 3-3 3-3-3z"/><path d="M5 21V9l7-3 7 3v12"/><path d="M12 9v12"/>' },
    { id: 'rituals',  label: 'Necromancy',   cat: 'Combat', icon: '<circle cx="12" cy="12" r="8"/><path d="M12 4v16M5.3 8l13.4 8M18.7 8L5.3 16"/>' },
    { id: 'toolbelt', label: 'Toolbelt',     cat: 'Character', icon: '<path d="M3 10h18v8a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z"/><path d="M8 10V7a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v3M10 14h4"/>' },
    { id: 'abilities',label: 'Ability Bar',  cat: 'Combat', icon: '<rect x="3" y="9" width="18" height="6" rx="1.5"/><path d="M7 9v6M11 9v6M15 9v6"/>' },
    { id: 'tasks',    label: 'Slayer & Reaper', cat: 'Progression', icon: '<path d="M9 6h11M9 12h11M9 18h8"/><path d="M3.5 5.5l1 1 2-2.2M3.5 11.5l1 1 2-2.2M3.5 17.5l1 1 2-2.2"/>' },
    { id: 'leagues', label: 'Leagues', cat: 'Progression', icon: '<path d="M8 21h8M12 17v4"/><path d="M7 4h10v6a5 5 0 0 1-10 0V4z"/><path d="M7 6H4v2a4 4 0 0 0 3 3.87M17 6h3v2a4 4 0 0 1-3 3.87"/>' },
    { id: 'dailies',  label: 'D&D Tracker', cat: 'Progression', icon: '<rect x="3" y="5" width="18" height="16" rx="2"/><path d="M8 3v4M16 3v4M3 10h18"/><path d="M9 15l2 2 4-4"/>' },
    { id: 'xptracker',label: 'XP Tracker',   cat: 'Progression', icon: '<path d="M4 19V5"/><path d="M4 19h16"/><path d="M7 15l3.5-4 3 2.5L18 8"/><path d="M18 8h-3.5M18 8v3.5"/>' },
    { id: 'xpmeter',  label: 'XP Meter',     cat: 'Progression', icon: '<path d="M4 17h16"/><path d="M6 17v-4"/><path d="M11 17V9"/><path d="M16 17v-6"/>' },
    { id: 'chatlog',  label: 'Chat Log',     cat: 'Utility', icon: '<path d="M4 5h16v10H9l-4 4V15H4z"/><path d="M8 9h8M8 12h5"/>' },
    // Items
    { id: 'inventory', label: 'Inventory', cat: 'Items', icon: '<rect x="4" y="4" width="6.5" height="6.5" rx="1"/><rect x="13.5" y="4" width="6.5" height="6.5" rx="1"/><rect x="4" y="13.5" width="6.5" height="6.5" rx="1"/><rect x="13.5" y="13.5" width="6.5" height="6.5" rx="1"/>' },
    { id: 'equipment', label: 'Equipment', cat: 'Items', icon: '<path d="M8 4l4 2 4-2 3 2.5-3 3v9.5H8V9.5l-3-3z"/>' },
    { id: 'bank',      label: 'Bank',      cat: 'Items', icon: '<path d="M3 10l9-6 9 6M4 10h16M6 10v8M12 10v8M18 10v8M3 20h18"/>' },
    { id: 'metalbank', label: 'Metal Bank', cat: 'Items', icon: '<path d="M4 13h16l-1.5 7h-13z"/><path d="M7 13V8a5 5 0 0 1 10 0v5"/><path d="M9.5 13V9a2.5 2.5 0 0 1 5 0v4"/>' },
    { id: 'groupbank', label: 'Group Bank', cat: 'Items', icon: '<path d="M3 21h18"/><path d="M5 21V10l7-5 7 5v11"/><path d="M9 21v-6h6v6"/>' },
    { id: 'storage',   label: 'Storage',   cat: 'Items', icon: '<rect x="3" y="6" width="18" height="14" rx="1.5"/><path d="M3 10h18M9 6V4h6v2M10 15h4"/>' },
    { id: 'baitbox',   label: 'Bait Box',  cat: 'Items', icon: '<rect x="4" y="8" width="16" height="12" rx="1.5"/><path d="M4 12h16M9 8V5h6v3M10 15h4"/>' },
    { id: 'artefacts', label: 'Workbench Storage', cat: 'Archaeology', icon: '<path d="M5 20h14M7 20V9l5-5 5 5v11"/><path d="M10 20v-5h4v5"/>' },
    { id: 'components', label: 'Components', cat: 'Items', icon: '<path d="M12 3l7 4v8l-7 4-7-4V7z"/><circle cx="12" cy="11" r="3"/>' },
    { id: 'machines', label: 'Machines', cat: 'Items', icon: '<circle cx="12" cy="12" r="3"/><path d="M12 5V3m0 18v-2m7-7h2M3 12h2m11.5-4.5l1.4-1.4M6.1 17.9l1.4-1.4m0-9L6.1 6.1m11.8 11.8l-1.4-1.4"/>' },
    { id: 'exchange',  label: 'Grand Exchange', cat: 'Items', icon: '<path d="M4 9h13M14 6l3 3-3 3M20 15H7M10 12l-3 3 3 3"/>' },
    { id: 'containers', label: 'Containers', cat: 'Items', icon: '<rect x="3" y="3" width="18" height="6" rx="1"/><rect x="3" y="11" width="18" height="6" rx="1"/><path d="M7 6h2M7 14h2"/>' },
    { id: 'shopcaps', label: 'Shop Limits', cat: 'Items', icon: '<path d="M4 9 5.5 4h13L20 9"/><path d="M4 9h16v2a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2z"/><path d="M5.5 13v7h13v-7"/><path d="M9 17h6"/>' },
    { id: 'currencies', label: 'Currencies', cat: 'Items', icon: '<circle cx="12" cy="12" r="8"/><path d="M12 7.5v9"/><path d="M14.5 9.5c-.5-.8-1.4-1.2-2.5-1.2-1.4 0-2.5.7-2.5 1.7 0 2.3 5 1.5 5 3.8 0 1-1.1 1.7-2.5 1.7-1.1 0-2-.4-2.5-1.2"/>' },
    // Quests
    { id: 'quests',   label: 'Browse Quests', cat: 'Quests', icon: '<path d="M6 3h12a1 1 0 0 1 1 1v16l-3-2-3 2-3-2-3 2V4a1 1 0 0 1 1-1z"/><path d="M9 8h6M9 12h6"/>' },
    { id: 'questfocus', label: 'Focused Quest', cat: 'Quests', icon: '<path d="M6 3h12a1 1 0 0 1 1 1v16l-3-2-3 2-3-2-3 2V4a1 1 0 0 1 1-1z"/><circle cx="12" cy="10" r="3"/>' },
    // Achievements
    { id: 'achievements', label: 'Achievements', cat: 'Progression', icon: '<circle cx="12" cy="9" r="5"/><path d="M8.5 13.5L7 21l5-3 5 3-1.5-7.5"/><path d="M12 6.5l.9 1.8 2 .3-1.4 1.4.3 2-1.8-1-1.8 1 .3-2L9.1 8.6l2-.3z"/>' },
    { id: 'combatmastery', label: 'Combat Mastery', cat: 'Progression', icon: '<path d="M13 4l7 7-2 2-7-7z"/><path d="M11 6L3.5 13.5 3 17l3.5-.5L14 9"/><path d="M5 14l3 3"/>' },
    { id: 'areatasks', label: 'Area Tasks', cat: 'Progression', icon: '<path d="M9 3 3 5.5v15L9 18l6 2.5 6-2.5v-15L15 5.5 9 3z"/><path d="M9 3v15M15 5.5v15"/>' },
    { id: 'gimtasks', label: 'Group Iron Tasks', cat: 'Progression', icon: '<circle cx="8" cy="8" r="2.6"/><circle cx="16" cy="8" r="2.6"/><path d="M3.5 19c0-2.7 2-4.5 4.5-4.5s4.5 1.8 4.5 4.5M13 18.8c.3-2.4 2.1-4 4.3-4 .9 0 1.7.2 2.4.7"/>' },
    // Clues
    { id: 'clues',    label: 'Clue Scrolls',  cat: 'Clues', icon: '<path d="M7 4h10a1 1 0 0 1 1 1v13a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z"/><path d="M9 8h6M9 11h6M9 14h4"/>' },
    { id: 'hideyholes', label: 'Hidey-holes', cat: 'Clues', icon: '<rect x="3" y="8" width="18" height="12" rx="1.5"/><path d="M3 12 L21 12"/><path d="M10 8 L10 4 L14 4 L14 8"/><circle cx="12" cy="16" r="1.2"/>' },
    { id: 'globetrotter', label: 'Globetrotter', cat: 'Clues', icon: '<circle cx="12" cy="12" r="9"/><ellipse cx="12" cy="12" rx="4" ry="9"/><path d="M3 12h18M4.5 7.5h15M4.5 16.5h15"/>' },
    { id: 'cluestats', label: 'Clue Stats',   cat: 'Clues', icon: '<path d="M4 20V5"/><path d="M4 20h16"/><rect x="7" y="12" width="3" height="5"/><rect x="12" y="8" width="3" height="9"/><rect x="17" y="4" width="3" height="13"/>' },
    // Collections
    { id: 'pets',     label: 'Pets',         cat: 'Collections', icon: '<circle cx="12" cy="15" r="4"/><circle cx="6.5" cy="10" r="1.7"/><circle cx="10" cy="7" r="1.7"/><circle cx="14" cy="7" r="1.7"/><circle cx="17.5" cy="10" r="1.7"/>' },
    { id: 'farmcol',  label: 'Farm Collections', cat: 'Collections', icon: '<path d="M4 8v12M9 8v12M14 8v12M19 8v12"/><path d="M3 12h17M3 16h17"/><circle cx="12" cy="5" r="1.6"/>' },
    { id: 'bosses',   label: 'Bosses',       cat: 'Collections', icon: '<path d="M12 3 L19 7 L19 13 C19 17 16 20 12 21 C8 20 5 17 5 13 L5 7 Z"/><circle cx="9.5" cy="11" r="1.3"/><circle cx="14.5" cy="11" r="1.3"/><path d="M9 15.5 C10 16.5 14 16.5 15 15.5"/>' },
    { id: 'collections', label: 'Clue Collections', cat: 'Collections', icon: '<path d="M4 5h16v14H4z"/><path d="M8 9h8M8 12h8M8 15h5"/>' },
    // Archaeology
    { id: 'archmysteries', label: 'Mysteries', cat: 'Archaeology', icon: '<circle cx="11" cy="11" r="7"/><path d="M21 21l-4-4"/><path d="M11 8a2 2 0 0 1 1.5 3.3c-.6.5-1 .8-1.2 1.4M11 15h.01"/>' },
    { id: 'mystfocus', label: 'Focused Mystery', cat: 'Archaeology', icon: '<circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="2.5"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3"/>' },
    { id: 'materials', label: 'Material Storage', cat: 'Archaeology', icon: '<path d="M12 2 2 7l10 5 10-5-10-5z"/><path d="M2 17l10 5 10-5"/><path d="M2 12l10 5 10-5"/>' },
    { id: 'archresearch', label: 'Research', cat: 'Archaeology', icon: '<path d="M4 5h11l5 5v9a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z"/><path d="M15 5v5h5"/><path d="M7 13h8M7 16h5"/>' },
    { id: 'archcol',  label: 'Artefact Collections', cat: 'Archaeology', icon: '<path d="M12 2 2 7l10 5 10-5-10-5z"/><path d="M6 9.5v5l6 3 6-3v-5"/><path d="M12 12v8"/>' },
    { id: 'relics',   label: 'Active Relics', cat: 'Archaeology', icon: '<path d="M12 2l2.5 5 5.5.8-4 3.9 1 5.5-5-2.6-5 2.6 1-5.5-4-3.9 5.5-.8z"/>' },
    { id: 'archshop', label: 'Guild Shop', cat: 'Archaeology', icon: '<path d="M4 9 5.5 4h13L20 9"/><path d="M4 9h16v2a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2z"/><path d="M5.5 13v7h13v-7"/><path d="M9.5 20v-4h5v4"/>' },
    // World
    { id: 'worldmap', label: 'World Map', cat: 'World', icon: '<path d="M9 5 3 7v12l6-2 6 2 6-2V5l-6 2-6-2z"/><path d="M9 5v12M15 7v12"/><circle cx="12" cy="11" r="1.6"/>' },
    { id: 'fairyrings', label: 'Fairy Rings', cat: 'World', icon: '<circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3"/>' },
    { id: 'resdungeons', label: 'Resource Dungeons', cat: 'World', icon: '<path d="M4 20V9l8-6 8 6v11"/><path d="M9 20v-6h6v6"/><path d="M12 3v3"/>' },
    { id: 'anachronia', label: 'Anachronia', cat: 'World', icon: '<path d="M12 3 4 9v12h16V9l-8-6z"/><path d="M9 21v-6h6v6M4 9h16"/>' },
    { id: 'scene',    label: 'Scene',   cat: 'World', icon: '<circle cx="9" cy="8" r="3"/><path d="M3.5 19c0-3 2.5-5 5.5-5s5.5 2 5.5 5"/><path d="M16 6.2a3 3 0 0 1 0 5.6M20.5 19c0-2.2-1.3-3.9-3.3-4.6"/>' },
    { id: 'farming',  label: 'Farming', cat: 'World', icon: '<path d="M12 21V9M12 9c0-3 2-5 5-5 0 3-2 5-5 5M12 12C12 9 10 7 7 7c0 3 2 5 5 5M5 21h14"/>' },
    { id: 'pof',      label: 'Farm Pens', cat: 'World', icon: '<path d="M4 8v12M9 8v12M14 8v12M19 8v12"/><path d="M3 12h17M3 16h17"/>' },
    { id: 'kingdom',  label: 'Miscellania', cat: 'World', icon: '<path d="M3 17l2-9 4.5 4L12 5l2.5 7L19 8l2 9z"/><path d="M5 20h14"/>' },
    { id: 'overlay',  label: 'Overlay', cat: 'HUD', icon: '<rect x="3" y="3" width="18" height="18" rx="1.5"/><path d="M3 9h18M3 15h18M9 3v18M15 3v18"/>' },
    { id: 'markers',  label: 'Markers', cat: 'HUD', icon: '<path d="M12 2C8.7 2 6 4.7 6 8c0 4.5 6 12 6 12s6-7.5 6-12c0-3.3-2.7-6-6-6z"/><circle cx="12" cy="8" r="2.2"/>' },
    { id: 'rendering', label: 'Rendering', cat: 'HUD', icon: '<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/><path d="M4 4l16 16"/>' },
    { id: 'lodestones', label: 'Lodestones', cat: 'World', icon: '<circle cx="12" cy="12" r="3.2"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M5 5l2 2M17 17l2 2M19 5l-2 2M7 17l-2 2"/>' },
    { id: 'scarabs',   label: 'Corrupted Scarabs', cat: 'World', icon: '<ellipse cx="12" cy="13" rx="5" ry="6"/><path d="M12 7V4M8 9L5 6M16 9l3-3M7 13H3M21 13h-4M8 18l-3 3M16 18l3 3"/>' },
    { id: 'obelisks',  label: 'Soul Obelisk', cat: 'World', icon: '<path d="M10 3h4l1.5 13h-7z"/><path d="M7 19h10M9 16h6"/>' },
    // Boss & minigame
    { id: 'bossinfo', label: 'Boss Info',   cat: 'Combat', icon: '<path d="M12 3 L19 7 L19 13 C19 17 16 20 12 21 C8 20 5 17 5 13 L5 7 Z"/><path d="M12 8v5M12 16h.01"/>' },
    { id: 'portsinfo', label: 'Ports',      cat: 'World', icon: '<path d="M12 3v13"/><path d="M12 6a3 3 0 1 0 .01 0"/><path d="M5 13c0 4 3.5 7 7 8 3.5-1 7-4 7-8"/><path d="M3 13h4M17 13h4"/>' },
    // Utility
    { id: 'alerts',    label: 'Alerts',    cat: 'HUD', icon: '<path d="M18 16v-5a6 6 0 1 0-12 0v5l-1.5 2h15z"/><path d="M10 20a2 2 0 0 0 4 0"/>' },
    { id: 'ticks',     label: 'Ticks',     cat: 'HUD', icon: '<circle cx="12" cy="12" r="8"/><path d="M12 8.5V12l2.5 1.5"/>' },
    { id: 'auras', label: 'Auras', cat: 'HUD', icon: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>' },
    { id: 'metronome', label: 'Metronome', cat: 'HUD', icon: '<path d="M10 3h4l4 18H6z"/><path d="M8.5 14h7"/><path d="M12 18V8"/>' },
    { id: 'stopwatch', label: 'Stopwatch', cat: 'Utility', icon: '<circle cx="12" cy="13" r="8"/><path d="M12 13V9"/><path d="M9 2h6M12 2v3"/>' },
    { id: 'notes',     label: 'Notes',     cat: 'Utility', icon: '<rect x="5" y="3" width="14" height="18" rx="1.5"/><path d="M8 8h8M8 12h8M8 16h5"/>' },
    { id: 'counter',   label: 'Counter',   cat: 'Utility', icon: '<path d="M9 3L7 21M17 3l-2 18M4 8h16M3 16h16"/>' },
    { id: 'screenshot',label: 'Screenshot', cat: 'Utility', icon: '<rect x="3" y="7" width="18" height="13" rx="2"/><circle cx="12" cy="13.5" r="3.5"/><path d="M8 7l1.5-2.5h5L16 7"/>' },
    { id: 'wiki',      label: 'Wiki',      cat: 'Utility', icon: '<path d="M4 5a2 2 0 0 1 2-2h13v16H6a2 2 0 0 0-2 2z"/><path d="M4 19a2 2 0 0 1 2-2h13"/><path d="M9 7h6M9 10.5h6"/>' },
    { id: 'fullscreen',label: 'Fullscreen', cat: 'Utility', icon: '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>' },
    // Developer
    { id: 'interfaces', label: 'Interfaces', cat: 'Developer', icon: '<rect x="3" y="4" width="18" height="16" rx="1.5"/><path d="M3 9h18M8 9v11"/>' },
    { id: 'vars',       label: 'Vars',       cat: 'Developer', icon: '<rect x="4" y="4" width="16" height="16" rx="2"/><path d="M8 9h8M8 13h8M8 17h4"/>' },
    // Server Packets: gated by NETPROBE_ENABLED, above.
    ...(NETPROBE_ENABLED ? [{ id: 'netprobe',   label: 'Server Packets', cat: 'Developer', icon: '<path d="M4 7h16M4 12h16M4 17h16"/><circle cx="7" cy="7" r="1"/><circle cx="11" cy="12" r="1"/><circle cx="9" cy="17" r="1"/>' }] : []),
    { id: 'cs2',        label: 'CS2 Scripts', cat: 'Developer', icon: '<path d="M8 6l-4 6 4 6M16 6l4 6-4 6M13.5 5l-3 14"/>' },
    { id: 'sounds',     label: 'Sounds', cat: 'Developer', icon: '<path d="M11 5 6 9H3v6h3l5 4z"/><path d="M15.5 8.5a5 5 0 0 1 0 7"/><path d="M18.5 5.5a9 9 0 0 1 0 13"/>' },
    { id: 'cachex',     label: 'Cache Explorer', cat: 'Developer', icon: '<path d="M4 7c0-1.7 3.6-3 8-3s8 1.3 8 3-3.6 3-8 3-8-1.3-8-3z"/><path d="M4 7v10c0 1.7 3.6 3 8 3s8-1.3 8-3V7"/><path d="M4 12c0 1.7 3.6 3 8 3s8-1.3 8-3"/>' },
    { id: 'menuswap',   label: 'Right-click menu', cat: 'Utility', icon: '<path d="M4 5h16v4H4z"/><path d="M4 11h16v8H4z"/><path d="M7 14h7"/><path d="M7 17h5"/>' },
    { id: 'health',     label: 'Health Check', cat: 'Developer', icon: '<path d="M3 12h4l2-6 4 12 2-6h6"/>' },
    { id: 'events',     label: 'Events',     cat: 'Developer', icon: '<path d="M4 6h16M4 12h10M4 18h13"/><circle cx="19" cy="12" r="2"/>' },
    { id: 'uisettings', label: 'Preferences', cat: 'Settings', icon: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18"/><path d="M8 14h8M8 17h5"/>' },
    { id: 'system',     label: 'System',     cat: 'Settings', icon: '<circle cx="12" cy="12" r="3.2"/><path d="M12 2.5v3M12 18.5v3M2.5 12h3M18.5 12h3M5.4 5.4l2.1 2.1M16.5 16.5l2.1 2.1M18.6 5.4l-2.1 2.1M7.5 16.5l-2.1 2.1"/>' },
  ];

  // ==================== Panel registry (registerTab) ====================
  // Every panel_*.js is an IIFE that calls registerTab({ id, render, open, close, refresh,
  // label, cat, icon }) at load. This file is spliced BEFORE rtx_vars.js and the panels, so
  // registerTab here is the one the panels call (rtx_vars.js keeps a fallback for a page that
  // loads without the core). The consumers, renderPane (render), tabEntryKicks (open),
  // paneLeave (close) and the poll tick (refresh), each consult RTX.panels[id] first and fall
  // through to the legacy if-chains for anything not registered. A new panel is one file:
  // register with label/cat/icon and it is appended to TABS; entries listed in TABS keep
  // their rail order and may omit render info.
  const RTX = window.RTX = window.RTX || {};
  RTX.panels = RTX.panels || {};
  function tabAdopt(d) {
    if (d.label && !TABS.some(t => t.id === d.id)) TABS.push({ id: d.id, label: d.label, cat: d.cat || 'Utility', icon: d.icon || '' });
  }
  function registerTab(def) {
    if (def && def.id) { RTX.panels[def.id] = def; tabAdopt(def); }
    return def;
  }
  for (const k in RTX.panels) tabAdopt(RTX.panels[k]);   // anything registered before this file ran

  // Rail parent categories, in rail order; only categories with tabs render.
  const CAT_META = [
    { id: 'Character',   icon: '<circle cx="12" cy="8" r="4"/><path d="M4 21v-1a8 8 0 0 1 16 0v1"/>' },
    { id: 'Combat',      icon: '<path d="M4 20l6-6M14 4l6 6-9 9-6-6z"/><path d="M4 20l2-2M13 11l-2 2"/>' },
    { id: 'Items',       icon: '<path d="M4 8h16l-1 12H5z"/><path d="M8 8V6a4 4 0 0 1 8 0v2"/>' },
    { id: 'Quests',      icon: '<path d="M6 3h12a1 1 0 0 1 1 1v16l-3-2-3 2-3-2-3 2V4a1 1 0 0 1 1-1z"/><path d="M9 8h6M9 12h6"/>' },
    { id: 'Progression', icon: '<circle cx="12" cy="9" r="5"/><path d="M8.5 13.5L7 21l5-3 5 3-1.5-7.5"/><path d="M12 7.2l.6 1.2 1.3.2-1 1 .2 1.3-1.1-.6-1.1.6.2-1.3-1-1 1.3-.2z"/>' },
    { id: 'Clues',       icon: '<path d="M7 4h10a1 1 0 0 1 1 1v13a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z"/><path d="M9 8h6M9 11h6M9 14h4"/>' },
    { id: 'Collections', icon: '<path d="M7 4h10v3a5 5 0 0 1-10 0z"/><path d="M5 4H4v1a3 3 0 0 0 3 3M19 4h1v1a3 3 0 0 1-3 3"/><path d="M10 12h4v3h-4z"/><path d="M8 20h8M9.5 20l.5-2.5h4l.5 2.5"/>' },
    { id: 'Archaeology', icon: '<path d="M4 9c4.5-4 11.5-4 16 0"/><path d="M12 6v15"/>' },
    { id: 'World',       icon: '<circle cx="12" cy="12" r="9"/><path d="M3 12h18"/><path d="M12 3a13 13 0 0 1 0 18 13 13 0 0 1 0-18z"/>' },
    { id: 'HUD',         icon: '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M7 9h4M7 13h6"/><circle cx="17" cy="11" r="1.5"/>' },
    { id: 'Utility',     icon: '<circle cx="12" cy="12" r="8"/><path d="M12 8v4l3 2"/>' },
    { id: 'Settings',    icon: '<circle cx="12" cy="12" r="3"/><path d="M12 3v2m0 14v2m9-9h-2M5 12H3m14.5-6.5l-1.4 1.4M7.9 16.1l-1.4 1.4m0-11l1.4 1.4m9.2 9.2l1.4 1.4"/>' },
    { id: 'Developer',   icon: '<path d="M8 8l-4 4 4 4M16 8l4 4-4 4M13 5l-2 14"/>' },
    { id: 'Plugins',     icon: '<rect x="3" y="3" width="8" height="8" rx="1"/><rect x="13" y="3" width="8" height="8" rx="1"/><rect x="3" y="13" width="8" height="8" rx="1"/><rect x="13" y="13" width="8" height="8" rx="1"/>' },
  ];

  const SKILL_NAMES = ['Attack','Defence','Strength','Hitpoints','Ranged',
    'Prayer','Magic','Cooking','Woodcutting','Fletching','Fishing',
    'Firemaking','Crafting','Smithing','Mining','Herblore','Agility',
    'Thieving','Slayer','Farming','Runecrafting','Hunter','Construction',
    'Summoning','Dungeoneering','Divination','Invention','Archaeology',
    'Necromancy'];

  // Compact gp formatting (20,099,750 -> "20.1m"); trailing ".0" stripped.
  function fmtGp(n) {
    n = Number(n) || 0;
    const sign = n < 0 ? '-' : ''; n = Math.abs(n);
    if (uiCfg().numFmt === 'full') return sign + n.toLocaleString();
    // Precision by magnitude: billions x.yy, millions x.y, thousands x.y (.0 dropped).
    if (n >= 1e9) return sign + (n / 1e9).toFixed(2) + 'b';
    if (n >= 1e6) return sign + (n / 1e6).toFixed(1) + 'm';
    if (n >= 1e3) { let t = (n / 1e3).toFixed(1); if (t.endsWith('.0')) t = t.slice(0, -2); return sign + t + 'k'; }
    return sign + n.toLocaleString();
  }

  // Standard RS3 XP-for-level table (1..126). xp(L) = floor((1/4) *
  // sum_{n=1}^{L-1} floor(n + 300 * 2^(n/7))). Indexed by level.
  const XP_TABLE = (() => {
    const t = [0, 0];
    let pts = 0;
    for (let lvl = 1; lvl < 126; ++lvl) {
      pts += Math.floor(lvl + 300 * Math.pow(2, lvl / 7));
      t[lvl + 1] = Math.floor(pts / 4);
    }
    return t;
  })();
  // Elite-skill table (Invention only), level-indexed 1..150, from cache enum 10699.
  const ELITE_XP_TABLE = [0,0,830,1861,2902,3980,5126,6380,7787,9400,11275,13605,16372,19656,23546,28134,33520,39809,47109,55535,65209,77190,90811,106221,123573,143025,164742,188893,215651,245196,277713,316311,358547,404634,454796,509259,568254,632019,700797,774834,854383,946227,1044569,1149696,1261903,1381488,1508756,1644015,1787581,1939773,2100917,2283490,2476369,2679917,2894505,3120508,3358307,3608290,3870846,4146374,4435275,4758122,5096111,5449685,5819299,6205407,6608473,7028964,7467354,7924122,8399751,8925664,9472665,10041285,10632061,11245538,11882262,12542789,13227679,13937496,14672812,15478994,16313404,17176661,18069395,18992239,19945833,20930821,21947856,22997593,24080695,25259906,26475754,27728955,29020233,30350318,31719944,33129852,34580790,36073511,37608773,39270442,40978509,42733789,44537107,46389292,48291180,50243611,52247435,54303504,56412678,58575824,60793812,63067521,65397835,67785643,70231841,72737330,75303019,77929820,80618654,83370445,86186124,89066630,92012904,95025896,98106559,101255855,104474750,107764216,111125230,114558777,118065845,121647430,125304532,129038159,132849323,136739041,140708338,144758242,148889790,153104021,157401983,161784728,166253312,170808801,175452262,180184770,185007406,189921255,194927409];

  function xpTable(elite) { return elite ? ELITE_XP_TABLE : XP_TABLE; }
  function levelFromXp(xp, elite) {
    const t = xpTable(elite);
    let lv = 1;
    for (let L = 2; L < t.length; ++L) {
      if (xp >= t[L]) lv = L; else break;
    }
    return lv;
  }
  function xpToNext(xp, elite) {
    const t = xpTable(elite);
    for (let L = 2; L < t.length; ++L) {
      if (t[L] > xp) return { next: t[L] - xp, level: L };
    }
    return { next: 0, level: t.length - 1 };  // at/over the table max
  }


  // ---- Tasks tab -> panel_tasks.js (spliced inline at load) ----
  // ---- Vars Watcher -> panel_varswatcher.js (spliced inline at load) ----
  // ---- Bank -> panel_bank.js (spliced inline at load) ----
  // ---- Inventory & Equipment tab -> panel_inventory.js (spliced inline at load) ----
  // ---- Two-level rail: category icons, then that category's children once selected.
  //      (rail removed: the menu bar + dropdowns replaced it) ----
  // ---- Panel consolidation (2026-08-03): related tabs share ONE rail entry; the members
  // render as a sub-tab strip ABOVE the content area (#subbar, outside #content so panel
  // repaints never wipe it). Every member keeps its tab id, so per-tab fetch/leave hooks,
  // tick gates and localStorage keys all keep working; only the navigation collapses.
  // A group's `cat` overrides its members' categories (allTabs applies it).
  const TAB_GROUPS = [
    { id: 'quests',  label: 'Quests',         cat: 'Quests',      tabs: ['quests', 'questfocus'],                                subs: ['All Quests', 'Focused'] },
    { id: 'achieve', label: 'Achievements',   cat: 'Progression', tabs: ['achievements', 'combatmastery', 'areatasks', 'gimtasks'], subs: ['Achievements', 'Combat Mastery', 'Area Tasks', 'GIM Journey'] },
    { id: 'collect', label: 'Collection Log', cat: 'Collections', tabs: ['collections', 'pets', 'farmcol', 'bosses', 'archcol'], subs: ['Collections', 'Pets', 'Farm Breeds', 'Boss KC', 'Artefacts'] },
    { id: 'cluehub', label: 'Clues',          cat: 'Clues',       tabs: ['clues', 'hideyholes', 'globetrotter', 'cluestats'],    subs: ['Clue Guide', 'Hidey-holes', 'Globetrotter', 'Stats'] },
    { id: 'arch',    label: 'Archaeology',    cat: 'Archaeology', tabs: ['archmysteries', 'artefacts', 'mystfocus', 'materials', 'archresearch', 'relics', 'archshop'], subs: ['Mysteries', 'Artefacts', 'Focused', 'Materials', 'Research', 'Relics', 'Guild Shop'] },
    { id: 'banks',   label: 'Banks',          cat: 'Items',       tabs: ['bank', 'metalbank', 'groupbank', 'baitbox'],           subs: ['Bank', 'Metal Bank', 'Group Bank', 'Bait Box'] },
    { id: 'inv',     label: 'Inventory',      cat: 'Items',       tabs: ['inventory', 'equipment'],                              subs: ['Backpack', 'Equipment'] },
    { id: 'economy', label: 'Economy',        cat: 'Items',       tabs: ['exchange', 'currencies', 'shopcaps'],                  subs: ['Grand Exchange', 'Currencies', 'Shop Caps'] },
    { id: 'overlay', label: 'Overlay',        cat: 'HUD',       tabs: ['overlay', 'markers', 'rendering'],                     subs: ['Overlay', 'Markers', 'Rendering'] },
    { id: 'wevents', label: 'World Events',   cat: 'World',       tabs: ['scarabs', 'obelisks'],                                 subs: ['Scarabs', 'Obelisks'] },
    { id: 'farm',    label: 'Farming',        cat: 'World',       tabs: ['farming', 'pof'],                                      subs: ['Patches', 'Animal Pens'] },
    { id: 'utils',   label: 'Utilities',      cat: 'Utility',     tabs: ['notes', 'counter', 'stopwatch'],                       subs: ['Notes', 'Counter', 'Stopwatch'] },
    { id: 'taskhub', label: 'Tasks & Dailies', cat: 'Progression',  tabs: ['tasks', 'dailies'],                                    subs: ['Slayer & Reaper', 'Dailies'] },
    { id: 'xp',      label: 'XP',             cat: 'Progression', tabs: ['xptracker', 'xpmeter'],                                subs: ['Tracker', 'Meter'] },
    { id: 'invent',  label: 'Invention',      cat: 'Items',       tabs: ['components', 'machines'],                              subs: ['Components', 'Machines'] },
  ];
  const TAB_GROUP_OF = {};   // tab id -> group
  for (const g of TAB_GROUPS) for (const id of g.tabs) TAB_GROUP_OF[id] = g;
  // The member to open when the rail entry is clicked: last used, else the group's first.
  function groupEntryTab(g) {
    let last = null;
    try { last = localStorage.getItem('rtxGrpLast:' + g.id); } catch (e) {}
    const id = (last && g.tabs.indexOf(last) >= 0) ? last : g.tabs[0];
    return allTabs().find(t => t.id === id) || allTabs().find(t => t.id === g.tabs[0]);
  }
  function tabCat(id) { const t = allTabs().find(x => x.id === id); return t ? (t.cat || 'General') : null; }
  function railCategories() {
    const present = new Set(allTabs().map(t => t.cat || 'General'));
    return CAT_META.filter(c => present.has(c.id) && !uiCatHidden(c.id));
  }
