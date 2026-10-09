# Yama-bruh audio reference

`drum-worklet.js` is an **unmodified** copy of the production worklet from
[`lucian-labs/yama-bruh`](https://github.com/lucian-labs/yama-bruh), commit
`3eca861383e63e6c507c30402a204499faf79887` (`www/drum-worklet.js`).
Retrieved from the local checkout on 2026-10-09.

- Source SHA-256: `fbd348754caf0682654b625069bd28706d81265fca825086192d41fefed0a752`
- License: MIT, Copyright (c) 2025 Lucian Labs; full notice in `LICENSE`.

`tests/yama_reference.mjs` executes this source in a minimal AudioWorklet
environment at 44,100 Hz. It uses the worklet's actual bank overrides and
per-sample FM, noise, envelope and clap code, rather than transcribing its
formulas into the test or using the separate UI defaults in `drums.js`.

The comparison allows fixed-point approximation, DC removal and one output
gain per sound. It checks decay timing, energy distribution and spectral
shape independently of gain. This validates the port against this worklet;
it does not establish equivalence to Yamaha hardware.
