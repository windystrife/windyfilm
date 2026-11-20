/* dev.js - shared chrome of the phone Develop mockups 08..12 (stage 1 approval images; not app code).
 * wfDev({title, sub, bg, pos, imgH, img, label, chip, tab, panel}) fills .screen: status bar, header, picture, the 8
 * Develop tabs, the tool panel. sl / gh / rw / sws / seg build the panel rows: our own controls and names
 * (apps-handoff option-model sections 12 and 13). */
const WF_TABS = [
  ['Film', '<rect x="6" y="4" width="12" height="16" rx="2"/><path d="M9 4v16"/>'],
  ['Print', '<rect x="4" y="5" width="16" height="14" rx="1"/><rect x="7" y="8" width="10" height="7"/>'],
  ['Tone', '<circle cx="12" cy="12" r="8"/><path d="M12 4v16M12 4a8 8 0 0 1 0 16z" fill="currentColor"/>'],
  ['Grain', '<circle cx="7" cy="8" r="1"/><circle cx="12" cy="6" r="1"/><circle cx="17" cy="9" r="1"/><circle cx="9" cy="13" r="1"/><circle cx="15" cy="14" r="1"/><circle cx="7" cy="18" r="1"/><circle cx="13" cy="18" r="1"/><circle cx="18" cy="17" r="1"/>'],
  ['Glow', '<circle cx="12" cy="12" r="3.5"/><circle cx="12" cy="12" r="7.5" stroke-dasharray="2 2.5"/>'],
  ['Lens', '<circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="4"/>'],
  ['Gate', '<rect x="3" y="4" width="18" height="16" rx="1.5"/><rect x="7" y="8.5" width="10" height="7" rx=".5"/><path d="M5.5 6.2h1.4M9.5 6.2h1.4M13.5 6.2h1.4M17.3 6.2h1M5.5 17.8h1.4M9.5 17.8h1.4M13.5 17.8h1.4M17.3 17.8h1"/>'],
  ['Finish', '<rect x="3" y="4" width="18" height="16" rx="1"/><path d="M7 16h6"/>'],
];

function wfDev(o) {
  const s = document.querySelector('.screen'), imgH = o.imgH || 176, ttTop = 76 + imgH;
  const bg = o.bg ? `background-image:url(${o.bg});background-position:${o.pos || 'center'};` : '';
  const tabs = WF_TABS.map(([n, svg]) => `<div${n === o.tab ? ' class="on"' : ''}><svg viewBox="0 0 24 24">${svg}</svg>${n}</div>`).join('');
  s.insertAdjacentHTML('afterbegin',
    `<div class="status"><span>${o.time || '14:52'}</span><div class="icons"><i class="wifi"></i><i class="sig"></i><i class="bat"></i></div></div>` +
    `<div class="hdr"><span class="ico"><svg viewBox="0 0 24 24"><path d="M15 5l-7 7 7 7"/></svg></span>` +
    `<div><div class="ttl">${o.title}</div><div class="sub">${o.sub}</div></div><div style="flex:1"></div>` +
    `<span class="ico"><svg viewBox="0 0 24 24"><path d="M9 7L4 12l5 5"/><path d="M4 12h10a6 6 0 0 1 0 12"/></svg></span>` +
    `<span class="chip am" style="height:28px;border-radius:14px;padding:0 12px">Print</span></div>` +
    `<div class="dimg" style="height:${imgH}px;${bg}">${o.img || ''}` +
    (o.label ? `<span class="l">${o.label}</span>` : '') +
    (o.chip === false ? '' : `<span class="chip r">${o.chip || 'Hold: before'}</span>`) + `</div>` +
    `<div class="dtt" style="top:${ttTop}px">${tabs}</div>` +
    `<div class="dpn" style="top:${ttTop + 50}px">${o.panel}</div>`);
  s.insertAdjacentHTML('beforeend', '<div class="gesture"></div>');
}

// slider: the fill runs from `zero` (default: 0 clamped into the range) to the value
function sl(name, v, min, max, txt, cls, zero) {
  const c = x => Math.max(0, Math.min(1, (x - min) / (max - min)));
  const z = c(zero === undefined ? 0 : zero), p = c(v), a = Math.min(z, p), w = Math.abs(p - z);
  const mid = zero !== undefined || (min < 0 && max > 0);
  const tick = mid ? `<i class="z" style="left:${z * 100}%"></i>` : '';
  return `<div class="sl ${cls || ''}"><span class="n">${name}</span><div class="tr">${tick}` +
    `<div class="f" style="left:${a * 100}%;width:${w * 100}%"></div><div class="k" style="left:${p * 100}%"></div></div>` +
    `<span class="v">${txt}</span></div>`;
}

function gh(title, right, cls) {
  const r = right ? (right[0] === '<' ? right : `<span class="h">${right}</span>`) : '';
  return `<div class="gh"><span class="sec ${cls || ''}">${title}</span>${r}</div>`;
}

function seg(items, on, style) {
  const st = style ? ` style="${style}"` : '';
  return `<div class="seg"${st}>` + items.map(i => `<span${i === on ? ' class="on"' : ''}>${i}</span>`).join('') + '</div>';
}

function rw(name, ctl, cls) {
  return `<div class="rw ${cls || ''}"><span class="n">${name}</span>${ctl}</div>`;
}

// one or two switches in a row: sws([name, on], [name, on])
function sws(...pairs) {
  return '<div class="rw2">' + pairs.map(([n, on]) => `<span><span>${n}</span><i class="sw${on ? ' on' : ''}"></i></span>`).join('') + '</div>';
}
