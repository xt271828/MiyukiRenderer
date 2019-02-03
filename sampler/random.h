//
// Created by Shiina Miyuki on 2019/1/17.
//

#ifndef MIYUKI_RANDOM_H
#define MIYUKI_RANDOM_H

#include "../core/sampler.h"
#include "../core/geometry.h"

namespace Miyuki {
    class RandomSampler : public Sampler {
    public:
        RandomSampler(Seed *s) : Sampler(s) {}

        Float nextFloat() override;

        int nextInt() override;

        Float nextFloat(Seed *seed) override;

        int nextInt(Seed *seed) override;

        Point2f nextFloat2D() override;
    };
}
#endif //MIYUKI_RANDOM_H
