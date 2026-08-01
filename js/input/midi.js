// Web MIDI input: keyboards and controllers connected over USB/Bluetooth

export async function initMidi(handlers) {
  if (!navigator.requestMIDIAccess) return { supported: false, inputs: 0 };
  let access;
  try {
    access = await navigator.requestMIDIAccess({ sysex: false });
  } catch {
    return { supported: false, inputs: 0 };
  }

  const attach = input => {
    input.onmidimessage = msg => {
      const [st, d1, d2] = msg.data;
      const type = st & 0xf0;
      handlers.activity && handlers.activity();
      if (type === 0x90 && d2 > 0) {
        handlers.noteOn(d1, d2 / 127);
      } else if (type === 0x80 || (type === 0x90 && d2 === 0)) {
        handlers.noteOff(d1);
      } else if (type === 0xb0) {
        if (d1 === 64) handlers.sustain(d2 >= 64);
        else if (d1 === 123 || d1 === 120) handlers.allOff && handlers.allOff();
      } else if (type === 0xe0) {
        // 14-bit pitch bend -> ±2 semitones
        const raw = ((d2 << 7) | d1) - 8192;
        handlers.bend((raw / 8192) * 2);
      }
    };
  };

  const refresh = () => {
    let n = 0;
    access.inputs.forEach(input => { attach(input); n++; });
    handlers.devices && handlers.devices(n);
    return n;
  };

  access.onstatechange = refresh;
  return { supported: true, inputs: refresh() };
}
