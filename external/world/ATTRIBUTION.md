# WORLD attribution

The audio analysis/synthesis routines under this directory are sourced
from the [WORLD voice-synthesis system](https://github.com/mmorise/World)
by Masanori Morise et al., distributed under the BSD-3-Clause license
(see `LICENSE.txt`).

WORLD performs F0 estimation (`Harvest`), spectral-envelope extraction
(`CheapTrick`), aperiodicity analysis (`D4C`), and additive
re-synthesis (`Synthesis`). We use it for formant-preserving pitch
shifting in scene 12 (Sung Direct).

Citation:

> Morise, M., Yokomori, F., & Ozawa, K. (2016). *WORLD: a
> vocoder-based high-quality speech synthesis system for real-time
> applications.* IEICE Transactions on Information and Systems,
> E99-D(7), 1877-1884.

Pinned version: v1.0.1 (declared in `external/CMakeLists.txt`).
