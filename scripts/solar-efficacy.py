#!/usr/bin/env python3
"""Derive the luminous efficacy of sunlight that view/Exposure.hpp uses (M1-15).

    solar-efficacy.py [--hsrs FILE] [--vlambda FILE]

Why this exists: the renderer carries light as radiance in W/(m^2 sr), and the
exposure a camera applies is defined on luminance, cd/m^2. The factor between
the two is a luminous efficacy, in lm/W, and it depends on the spectrum of the
light. Register decision 175 replaced the fixed 179 lm/W of ADR 0014 -- which
Radiance defines for "equal energy white 380-780nm" (src/common/color.h), a
different light from the Sun -- with the efficacy of sunlight itself, derived
here the way pbrt-v4 and Bruneton's precomputed atmosphere derive photometric
quantities: the SI definition, 683 lm/W times the CIE photopic luminous
efficiency function V(lambda), integrated over the actual spectrum.

    efficacy = 683 lm/W * integral(E(lambda) V(lambda) dlambda) / TSI

Inputs, both downloaded when not given as files, and both identified by their
SHA-256 in the output so a second machine can tell whether it read the same
data:

  * **TSIS-1 Hybrid Solar Reference Spectrum**, 0.1 nm bandwidth, 202-2730 nm,
    from LASP's LISIRD (Coddington et al. 2021, Geophys. Res. Lett. 48,
    e2020GL091709). Absolutely calibrated, and consistent with a total solar
    irradiance of 1360.8 W/m^2 -- the Kopp & Lean (2011) value that
    astro/Sun.hpp holds as 1361 W/m^2. V(lambda) is zero outside 360-830 nm,
    so the spectrum's truncation at 2730 nm does not touch the numerator.
  * **CIE 1924 photopic V(lambda)**, 1 nm, 360-830 nm, from the Colour &
    Vision Research Laboratory (CVRL) database, the function the SI
    definition of the candela uses.

The denominator is the project's own 1361 W/m^2, not the spectrum's integral
(which stops at 2730 nm): the renderer multiplies radiance computed from 1361
by this factor, so the factor must be illuminance per watt of *that* total.

Cross-check, measured 2026-09-25 and recorded in M1-15's task document: the
older ASTM E-490 spectrum gives 97.59 lm/W on its own total of 1366.1 W/m^2,
1.4 % below this. TSIS-1 is chosen because it is the newer, absolutely
calibrated reference and sits on the same total irradiance as the constant it
multiplies.

Trapezoidal integration on the spectrum's own 0.025 nm grid, with V(lambda)
interpolated linearly between its 1 nm points. Needs nothing beyond the
standard library.
"""

import argparse
import hashlib
import urllib.request

HSRS_URL = "https://lasp.colorado.edu/lisird/latis/dap/tsis1_hsrs_p1nm.csv"
# CVRL serves plain http; its https endpoint timed out from this machine on
# 2026-09-25. The SHA-256 printed below is what makes the download checkable.
VLAMBDA_URL = "http://www.cvrl.org/database/data/lum/vl1924e_1.csv"

MAXIMUM_LUMINOUS_EFFICACY = 683.0  # lm/W, SI definition of the candela (2019)
TOTAL_SOLAR_IRRADIANCE = 1361.0    # W/m^2, astro/Sun.hpp kSolarIrradianceAtOneAu


def read(source: str | None, url: str) -> bytes:
    if source:
        with open(source, "rb") as f:
            return f.read()
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def parse_hsrs(data: bytes) -> list[tuple[float, float]]:
    rows = []
    for line in data.decode("ascii").splitlines()[1:]:  # header: wavelength, irradiance, ...
        fields = line.split(",")
        rows.append((float(fields[0]), float(fields[1])))  # nm, W/(m^2 nm)
    return rows


def parse_vlambda(data: bytes) -> list[tuple[float, float]]:
    rows = []
    for line in data.decode("ascii").splitlines():
        if line.strip():
            wavelength, value = line.split(",")
            rows.append((float(wavelength), float(value)))
    return rows


def interpolate(table: list[tuple[float, float]], x: float) -> float:
    # The table is on a 1 nm grid starting at a whole nanometre.
    first = table[0][0]
    last = table[-1][0]
    if x < first or x > last:
        return 0.0
    i = min(int(x - first), len(table) - 2)
    (x0, y0), (x1, y1) = table[i], table[i + 1]
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--hsrs", help="a local copy of the TSIS-1 HSRS csv")
    parser.add_argument("--vlambda", help="a local copy of CVRL's vl1924e_1.csv")
    args = parser.parse_args()

    hsrs_bytes = read(args.hsrs, HSRS_URL)
    vlambda_bytes = read(args.vlambda, VLAMBDA_URL)
    spectrum = parse_hsrs(hsrs_bytes)
    vlambda = parse_vlambda(vlambda_bytes)

    weighted = 0.0
    band = 0.0
    for (w0, e0), (w1, e1) in zip(spectrum, spectrum[1:]):
        step = w1 - w0
        band += 0.5 * (e0 + e1) * step
        weighted += 0.5 * (e0 * interpolate(vlambda, w0) + e1 * interpolate(vlambda, w1)) * step
    illuminance = MAXIMUM_LUMINOUS_EFFICACY * weighted

    print(f"TSIS-1 HSRS     sha256 {hashlib.sha256(hsrs_bytes).hexdigest()}"
          f"  {spectrum[0][0]:.3f}-{spectrum[-1][0]:.3f} nm, {len(spectrum)} rows")
    print(f"CIE 1924 V      sha256 {hashlib.sha256(vlambda_bytes).hexdigest()}"
          f"  {vlambda[0][0]:.0f}-{vlambda[-1][0]:.0f} nm, {len(vlambda)} rows")
    print(f"spectrum integral over its band   {band:.3f} W/m^2")
    print(f"illuminance at 1 AU               {illuminance:.1f} lx")
    print(f"efficacy against {TOTAL_SOLAR_IRRADIANCE} W/m^2      "
          f"{illuminance / TOTAL_SOLAR_IRRADIANCE:.4f} lm/W")


if __name__ == "__main__":
    main()
