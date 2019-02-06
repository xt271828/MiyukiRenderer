//
// Created by Shiina Miyuki on 2019/1/22.
//

#ifndef MIYUKI_BDPT_H
#define MIYUKI_BDPT_H

#include "../core/integrator.h"
#include "../core/geometry.h"
#include "../core/sampler.h"
#include "../core/material.h"

namespace Miyuki {
    enum class BxDFType;

    struct Vertex {
        enum VertexType {
            lightVertex,
            cameraVertex,
            surfaceVertex,
        };
        Vec3f hitPoint, normal, radiance;
        Float pdfFwd, pdfRev;
        BxDFType sampledType;
        VertexType type;
        int32_t geomID;
        int32_t primID;

        Vertex(VertexType _type) : geomID(-1), primID(-1), type(_type) {}

        bool connectable(const Vertex &rhs) const;

        Float convertDensity(Float pdf, const Vertex &) const;

        Float pdf(const Scene &, const Vertex *, const Vertex &next)const;
    };

    class Path : public std::vector<Vertex> {

    };

    class BDPT : public Integrator {
    public:


    protected:
        void generateLightPath(Sampler &, Scene &, Path &, uint32_t maxS);

        void generateEyePath(RenderContext &ctx, Scene &, Path &, uint32_t maxT);

        Spectrum connectBDPT(Scene &, Path &L, Path &E, int32_t s, int32_t t);

        void iteration(Scene &scene);

    public:
        void render(Scene &) override;
    };
}

#endif //MIYUKI_BDPT_H
