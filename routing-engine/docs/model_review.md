# Physical model review and limits

For length L metres and reference speed v km/h, reference time is 3.6L/v seconds.
Travel time multiplies that reference by congestion k >= 1. Reference consumption
is L/100000 times the configured litres-or-kWh per 100 km. Only additional delay
(t - t_ref)/3600 receives hourly auxiliary consumption. Thus an all-in baseline
is not charged twice. A speed limit is only a proxy for reference speed;
fallback speed is explicit configuration.

When both endpoint elevations and vehicle mass/conversion are present,
gravitational mechanical energy is m*9.80665*delta_h / 3600000 kWh. Uphill energy
is divided by effective wheel kWh per fuel/battery unit. EV conversion must be
in (0,1]; combustion conversion has different units (wheel kWh/litre).
Downhill direct-assistance and recovered-electricity fractions must sum to <=1,
so the same gravity share cannot be credited twice. Explicit regeneration is EV
only; hybrid reference consumption implicitly includes its ordinary benefit.

Per-edge final consumption is clamped at zero for nonnegative additive routing.
This is not a battery-state model and does not claim net charging on a descent.
The all-in reference rate does not isolate propulsion from baseline auxiliaries;
a downhill credit is therefore an approximation. No speed/acceleration dynamics,
cold-start, HVAC/weather response, charge acceptance, battery capacity or access
restrictions are modeled. CO2e equals consumed units times the configured factor;
its operational/upstream accounting boundary must be chosen consistently.
EV CO2e is not asserted to be zero tailpipe or full lifecycle emissions.

## Segment splitting

Constant-speed, constant-grade proportional subdivision is invariant: reference
time, delay, gravity and baseline consumption are homogeneous in length, and
min/max clamping preserves this scaling. Tests verify that property.

Arbitrary aggregation is not invariant. With an illustrative EV rate
18 kWh/100km, mass 1800kg, conversion 0.9, assist 0.5 and recovery 0.3, one 2km
edge descending 100m clamps to zero Wh. Represent a flat first kilometre followed
by the descending kilometre as two edges and the flat portion consumes 180Wh.
Endpoint-only elevation also hides a hill followed by a descent. This known
limitation is preserved explicitly: fixing it requires elevation profiles and/or
energy-state routing, not changing expected test values. Avoid comparing energy
estimates across graph segmentations as if they were scientifically equivalent.
Implausible but finite gradients are not automatically clipped; ingestion must
validate elevation quality. Elevation is disabled in shipped demo profiles.

## External evidence and validation boundary

FASTSim's official guide describes parameterization followed by calibration
against measurements: [Modeling a Vehicle](https://natlabrockies.github.io/fastsim/modeling-vehicles/).
Its drive-cycle model uses time-varying speed/grade and derives acceleration and
power: [Drive Cycles](https://natlabrockies.github.io/fastsim/drive-cycle/).
EcoRoute's simpler equations omit those dynamics. These sources support the
need for calibration; they do not validate EcoRoute or supply its demo numbers.

Executed checks verify units, bounds, all vehicle categories, elevation
arithmetic, uniform splitting, nonuniform splitting limitations and overflow.
No representative measured consumption dataset was supplied. Empirical error
against real vehicles is NOT RUN. Demo values must be replaced by sourced,
versioned profiles before presenting estimates as real-world predictions.