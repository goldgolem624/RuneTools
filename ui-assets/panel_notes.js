// RuneToolsX panel: Notes (per-character, saved via bridge notesLoad/notesSave).
(function () {

  let notesData = null, notesLoadedPid = -1, _notesSaveT = 0, notesStore = null;
  // made on first use: the store helpers come from core/rtx-boot.js, spliced after the panels
  function notesSt() {
    return notesStore || (notesStore = acctStore(() => rtxData.sync('host.notesLoad'),
                                                 t => rtxData.sync('act.notesSave', t), notesReload));
  }
  function loadNotes() {
    if (notesLoadedPid === myPid() && notesData) return;
    notesLoadedPid = myPid(); notesData = [];
    try {
      const a = JSON.parse(acctStoreRead(notesSt()) || '[]');
      if (Array.isArray(a)) notesData = a;
    } catch (e) { notesData = []; }
  }
  // The account behind this client changed (or became known) since the notes were read: a pending
  // save holds the old account's notes, so it goes too.
  function notesReload() {
    clearTimeout(_notesSaveT); _notesSaveT = 0;
    notesLoadedPid = -1; loadNotes(); paintNotes(false);
  }
  function saveNotesNow() { try { acctStoreWrite(notesSt(), JSON.stringify(notesData)); } catch (e) {} }
  function saveNotes() { clearTimeout(_notesSaveT); _notesSaveT = setTimeout(saveNotesNow, 400); }
  function noteId() { return 'n' + Date.now().toString(36) + Math.floor(Math.random() * 1e4).toString(36); }
  // A refused save reads the notes again, dropping the new one: focusing then would put the typing
  // into another note's title.
  function addNote() {
    loadNotes();
    const n = { id: noteId(), title: '', body: '' };
    notesData.unshift(n); saveNotesNow(); paintNotes(notesData[0] === n);
  }
  function delNote(id) { notesData = notesData.filter(n => n.id !== id); saveNotesNow(); paintNotes(false); }
  function renderNotes() {
    const c = $('content');
    if ($('notesWrap')) return;   // built once; a rebuild would drop input focus
    c.innerHTML = ''; loadNotes();
    const wrap = document.createElement('div'); wrap.id = 'notesWrap'; wrap.className = 'notes-wrap';
    const head = document.createElement('div'); head.className = 'notes-head';
    const count = document.createElement('span'); count.id = 'notesCount'; count.className = 'notes-count';
    const add = document.createElement('button'); add.className = 'notes-add'; add.textContent = '+ Add note';
    add.addEventListener('click', addNote);
    head.appendChild(count); head.appendChild(add);
    const list = document.createElement('div'); list.id = 'notesList'; list.className = 'notes-list';
    wrap.appendChild(head); wrap.appendChild(list);
    c.appendChild(wrap);
    paintNotes(false);
  }
  function paintNotes(focusFirst) {
    const list = $('notesList'); if (!list) return;
    const cnt = $('notesCount'); if (cnt) cnt.textContent = notesData.length + (notesData.length === 1 ? ' note' : ' notes');
    list.innerHTML = '';
    if (!notesData.length) {
      const e = document.createElement('div'); e.className = 'empty'; e.textContent = 'No notes yet. Click "Add note".';
      list.appendChild(e); return;
    }
    notesData.forEach((n, idx) => {
      const card = document.createElement('div'); card.className = 'note-card';
      const top = document.createElement('div'); top.className = 'note-top';
      const title = document.createElement('input'); title.className = 'note-title'; title.value = n.title || ''; title.placeholder = 'Title';
      title.addEventListener('input', () => { n.title = title.value; saveNotes(); });
      const del = document.createElement('button'); del.className = 'note-del'; del.title = 'Delete note'; del.innerHTML = '&times;';
      del.addEventListener('click', () => delNote(n.id));
      top.appendChild(title); top.appendChild(del);
      const body = document.createElement('textarea'); body.className = 'note-body'; body.value = n.body || ''; body.placeholder = 'Write a note...';
      body.addEventListener('input', () => { n.body = body.value; saveNotes(); });
      card.appendChild(top); card.appendChild(body);
      list.appendChild(card);
      if (focusFirst && idx === 0) title.focus();
    });
  }

registerTab({ id: 'notes', render: renderNotes });
})();
