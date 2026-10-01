# carta-backend

The server behind the CARTA image viewer: it opens images through loaders and answers the frontend's
requests about them. This file holds only the language settled so far, around the cube histogram, line
profiles, region profiles and the statistics reported from a plane, a cube or a region; the rest of the
backend's vocabulary is not written down here yet.

## Language

### The cube histogram

**Cube histogram**:
The histogram of every pixel of one Stokes of a cube, together with the statistics its bins were laid
over. Made once per Stokes and kept. The statistics are never asked for apart from it.
_Avoid_: cube stats, whole-image histogram

**Walk**:
A loader's own way of answering a question about a whole cube, or about many regions, by reading the
pixels once where asking one plane or one region at a time would read them again. Not every loader has
one, and one that has may decline a particular request.
_Avoid_: batched walk, fast path, accelerator

**Route**:
How an answer about many pixels is made: by a loader's walk, or a piece at a time -- plane by plane
for a cube histogram, box by box for line profiles. The routes are asked in turn, and one that
declines leaves the answer to the next. Each half of a cube histogram takes its own route, so a loader
that walks the statistics may decline the bins -- a cube of one constant value, whose range is empty,
is the case that does. A one-pass cube histogram has one route and no halves.
_Avoid_: path, strategy, method

### Line profiles

**Line profiles**:
The mean spectrum of each of the boxes that approximate a line of some width, side by side along it:
what a position-velocity image is made from. A box that catches no valid pixel in a channel has no
mean there.
_Avoid_: PV data, box profiles

### Region profiles

**Region profile**:
The spectral profile of a closed region in one Stokes: for each channel, the statistics of the
region's valid pixels. Made in steps, and resumed from one step to the next for as long as the region,
its mask and the channels asked for stay the same; kept once made, until the region is edited or
removed. A point has none: there are no statistics of one pixel, and a point's profile is that pixel's
spectrum, whichever statistic is asked for.
_Avoid_: region spectral data, region spectral stats

### Reported statistics

**Totals**:
The count, sum, sum of squares, smallest and largest of the valid pixels of a plane, a cube or one
channel of a region: what every reported statistic is made from. A pixel that is not finite is not
valid and is in no total. carta-zarr calls the same numbers a SpectralTotals.
_Avoid_: accumulators, moments, raw stats

**Derived statistics**:
The mean, RMS, sigma and extrema that CARTA reports, made from Totals and from nothing else. Extrema
is whichever of the smallest and the largest has the larger magnitude, the largest when they tie. With
no valid pixel there is nothing to derive, and every one of them is undefined.
_Avoid_: computed stats, stats

**Lone-pixel sigma**:
The sigma reported when exactly one pixel is valid, where the sample standard deviation has no
definition. What to report is the consumer's policy, not a fact about the pixels: a spectral profile
reports zero, and a plane or a cube reports NaN.
_Avoid_: single-pixel sigma, n=1 case

### Moments

**Slab**:
What a moment holds at a time as it steps through the image: whole lines along the moment axis, over
as much of the other axes as it can afford. Its shape is chosen to match what the image decodes
together -- a Zarr store's own chunks, where the image has them -- so that each chunk is decoded as few
times as the memory allows: once for every slab that touches it, unless the moment's own cache keeps
it for the next. That cache is the moment's, not the session's, and lasts only as long as the moment.
Stepping through the image this way is not a **Walk**: the image is read through casacore, not by a
loader's own means.
_Avoid_: chunk (the image's unit, not the moment's), cursor
