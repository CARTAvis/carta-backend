# carta-backend

The server behind the CARTA image viewer: it opens images through loaders and answers the frontend's
requests about them. This file holds only the language settled so far, around the cube histogram; the
rest of the backend's vocabulary is not written down here yet.

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
How one half of a cube histogram is made: by a loader's walk, or plane by plane. The statistics and
the bins each take their own route, so a loader that walks the first may decline the second -- a cube
of one constant value, whose range is empty, is the case that does. A one-pass cube histogram has one
route and no halves.
_Avoid_: path, strategy, method
