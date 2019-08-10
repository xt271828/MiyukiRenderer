#ifndef MIYUKI_SAMPLER_INTEGRATOR_H
#define MIYUKI_SAMPLER_INTEGRATOR_H

#include <core/integrators/integrator.h>
#include <core/intersection.hpp>

namespace Miyuki {
	namespace Core {
		class SamplerIntegrator : public ProgressiveRenderer {
		public:
			MYK_ABSTRACT(SamplerIntegrator); 
			SamplerIntegrator():_aborted(false){}
			size_t spp = 16;
			void abort()override {
				_aborted = true;
			}
			virtual void renderStart(const IntegratorContext& context) {}
			virtual void renderEnd(const IntegratorContext& context) {}
			virtual void Li(Intersection * isct, const IntegratorContext& context, SamplingContext&) = 0;
			void renderProgressive(
				const IntegratorContext& context,
				ProgressiveRenderCallback progressiveCallback)override;
		protected:
			std::atomic<bool> _aborted;
		};
		MYK_REFL(SamplerIntegrator, (ProgressiveRenderer), (spp));

		void HilbertMapping(const Point2i& nTiles, std::vector<Point2f>& hilbertMapping);
	}
}

#endif