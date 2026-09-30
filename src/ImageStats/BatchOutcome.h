/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGESTATS_BATCHOUTCOME_H_
#define CARTA_SRC_IMAGESTATS_BATCHOUTCOME_H_

namespace carta {

// How a loader's own read ended: a walk, a region profile read on, or a cursor's spectrum.
//
// `declined` is the loader refusing the request before it began -- one it does not serve, such as an
// empty range or a stokes it cannot read -- so no callback has been called and nothing the caller
// holds has been touched. It is always answered before the loader asks anything of the pixels, which
// is what makes it safe to take another route after it. `failed` is the pixels having been asked and
// the answer not arriving -- a read or a request the library refused, which the loader logs -- and
// callbacks may already have been called by then. `cancelled` is the caller's own callback having
// said stop, which is not a reason to try another route.
//
// `declined` and `failed` are kept apart because a caller can do something sensible after the
// first and not after the second: its own route reads the same pixels, through the same library, and
// would meet the same failure after having already published part of an answer.
enum class BatchOutcome { finished, declined, cancelled, failed };

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_BATCHOUTCOME_H_
