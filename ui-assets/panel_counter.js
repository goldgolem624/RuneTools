// RuneToolsX panel: Counter (per-character, saved via bridge counterLoad/counterSave).
// Spliced inline into client.html; bare classic script sharing one global scope.

  let counterVal = 0, counterLoadedPid = -1, counterStep = 1;
  function loadCounter() {
    if (counterLoadedPid === myPid()) return;
    counterLoadedPid = myPid(); counterVal = 0;
    try { const o = JSON.parse((bridge() && bridge().counterLoad && bridge().counterLoad(myPid())) || '{}'); if (o && typeof o.value === 'number') counterVal = o.value | 0; } catch (e) {}
  }
  function saveCounter() { try { bridge().counterSave(myPid(), JSON.stringify({ value: counterVal })); } catch (e) {} }
  function counterUpdate() { const v = $('ctrVal'); if (v) v.textContent = counterVal.toLocaleString(); }
  function counterInc(d) { counterVal += d; saveCounter(); counterUpdate(); }
  function counterReset() { counterVal = 0; saveCounter(); counterUpdate(); }
  function counterSetTo(n) { if (!isFinite(n)) return; counterVal = n | 0; saveCounter(); counterUpdate(); }
  function renderCounter() {
    const c = $('content');
    if ($('ctrWrap')) { counterUpdate(); return; }
    c.innerHTML = ''; loadCounter();
    const wrap = document.createElement('div'); wrap.id = 'ctrWrap'; wrap.className = 'ctr-wrap';
    const val = document.createElement('div'); val.id = 'ctrVal'; val.className = 'ctr-val';
    wrap.appendChild(val);

    const panel = document.createElement('div'); panel.className = 'ctr-panel';

    const incRow = document.createElement('div'); incRow.className = 'ctr-incrow';
    const minus = document.createElement('button'); minus.className = 'ctr-btn big'; minus.textContent = '-';
    minus.addEventListener('click', () => counterInc(-counterStep));
    const plus = document.createElement('button'); plus.className = 'ctr-btn big'; plus.textContent = '+';
    plus.addEventListener('click', () => counterInc(counterStep));
    incRow.appendChild(minus); incRow.appendChild(plus);
    panel.appendChild(incRow);

    const stepRow = document.createElement('div'); stepRow.className = 'ctr-field';
    const stepLab = document.createElement('span'); stepLab.className = 'ctr-lab'; stepLab.textContent = 'Step';
    const stepIn = document.createElement('input'); stepIn.type = 'number'; stepIn.className = 'ctr-input'; stepIn.value = String(counterStep); stepIn.min = '1';
    stepIn.addEventListener('change', () => { const n = parseInt(stepIn.value, 10); counterStep = (isFinite(n) && n > 0) ? n : 1; stepIn.value = String(counterStep); });
    stepRow.appendChild(stepLab); stepRow.appendChild(stepIn);
    panel.appendChild(stepRow);

    const setRow = document.createElement('div'); setRow.className = 'ctr-field';
    const setIn = document.createElement('input'); setIn.type = 'number'; setIn.className = 'ctr-input'; setIn.placeholder = 'Set value...';
    const setBtn = document.createElement('button'); setBtn.className = 'ctr-btn ctr-set'; setBtn.textContent = 'Set';
    const doSet = () => { const n = parseInt(setIn.value, 10); if (isFinite(n)) { counterSetTo(n); setIn.value = ''; } };
    setBtn.addEventListener('click', doSet);
    setIn.addEventListener('keydown', e => { if (e.key === 'Enter') doSet(); });
    setRow.appendChild(setIn); setRow.appendChild(setBtn);
    panel.appendChild(setRow);

    const reset = document.createElement('button'); reset.className = 'ctr-btn ctr-reset'; reset.textContent = 'Reset to 0';
    reset.addEventListener('click', counterReset);
    panel.appendChild(reset);

    wrap.appendChild(panel);
    c.appendChild(wrap);
    counterUpdate();
  }
