//
// Created by Shiina Miyuki on 2019/3/11.
//

#ifndef MIYUKI_METROPOLIS_H
#define MIYUKI_METROPOLIS_H

#include "integrator.h"

namespace Miyuki {
    struct BootstrapSample {
        Float b;
        std::vector<Seed> seeds;
    };

    class MetropolisBootstrapper {
    public:
        virtual Float f(Seed *, MemoryArena *) = 0;

        void generateBootstrapSamples(BootstrapSample *samples, uint32_t nBootstrap, uint32_t nChains);
    };

    inline Float AverageMutationPerPixel(int nPixels, int nChains, int nIterations) {
        return nChains * nIterations / (Float) nPixels;
    }

    inline int IterationsMPP(int nPixels, int nChains, int mpp) {
        return std::round(mpp * nPixels / nChains);
    }
}
#endif //MIYUKI_METROPOLIS_H
