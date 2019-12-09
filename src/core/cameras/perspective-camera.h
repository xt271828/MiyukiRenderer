// MIT License
//
// Copyright (c) 2019 椎名深雪
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef MIYUKIRENDERER_PERSPECTIVE_CAMERA_H
#define MIYUKIRENDERER_PERSPECTIVE_CAMERA_H

#include <miyuki.renderer/camera.h>
#include <miyuki.foundation/property.hpp>
#include <miyuki.foundation/serialize.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace miyuki::core {
    class PerspectiveCamera final : public Camera {
        Transform _transform{}, _invTransform{};
        Radians<float> fov = Radians<float>(Degrees<float>(80.0));
        TransformManipulator transform{};
    public:
        PerspectiveCamera() = default;

        PerspectiveCamera(const Vec3f &p1, const Vec3f &p2, Radians<float> fov) : fov(fov) {
            _transform = Transform(lookAt(p1, p2, vec3(0, 1, 0)));
            _invTransform = _transform.inverse();
        }

        [[nodiscard]] const Transform &getTransform() const override { return _transform; }

    public:
        MYK_DECL_CLASS(PerspectiveCamera, "PerspectiveCamera", interface = "Camera")

        MYK_AUTO_SER(transform, fov)

        MYK_PROP(transform, fov)

        void initialize(const json &params) override;

        void generateRay(const Point2f &u1, const Point2f &u2, const Point2i &raster, Point2i filmDimension,
                         CameraSample &sample) const override;

        void preprocess()override;
    };

} // namespace miyuki::core

#endif // MIYUKIRENDERER_PERSPECTIVE_CAMERA_H