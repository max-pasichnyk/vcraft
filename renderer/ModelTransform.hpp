#pragma once

#include "math/vec3.hpp"

#include <cmath>

struct ModelTransform {
	ModelTransform* next{nullptr};

	Vector3 origin{ 0.5f, 0.5f, 0.5f };
	float rotX = 0;
	float rotY = 0;
	float rotZ = 0;

	inline void apply(Vector3& p1, Vector3& p2, Vector3& p3, Vector3& p4) {
		if (rotX != 0) rotateX(rotX, origin, p1, p2, p3, p4);
		if (rotY != 0) rotateY(rotY, origin, p1, p2, p3, p4);
		if (rotZ != 0) rotateZ(rotZ, origin, p1, p2, p3, p4);
	}

	inline static void rotate(int ix, int iy, float angle, const Vector3& origin, Vector3& p1, Vector3& p2, Vector3& p3, Vector3& p4) {
        float ox = origin[ix];
        float oy = origin[iy];

        float s = std::sin(angle);
        float c = std::cos(angle);

		float x1 = p1[ix] - ox;
		float y1 = p1[iy] - oy;
		p1[ix] = ox + x1 * c + y1 * s;
		p1[iy] = oy - x1 * s + y1 * c;

		float x2 = p2[ix] - ox;
		float y2 = p2[iy] - oy;
		p2[ix] = ox + x2 * c + y2 * s;
		p2[iy] = oy - x2 * s + y2 * c;

		float x3 = p3[ix] - ox;
		float y3 = p3[iy] - oy;
		p3[ix] = ox + x3 * c + y3 * s;
		p3[iy] = oy - x3 * s + y3 * c;

		float x4 = p4[ix] - ox;
		float y4 = p4[iy] - oy;
		p4[ix] = ox + x4 * c + y4 * s;
		p4[iy] = oy - x4 * s + y4 * c;
    }

    inline static void rotateX(float angle, const Vector3& origin, Vector3& p1, Vector3& p2, Vector3& p3, Vector3& p4) {
		rotate(2, 1, angle, origin, p1, p2, p3, p4);
    }

    inline static void rotateY(float angle, const Vector3& origin, Vector3& p1, Vector3& p2, Vector3& p3, Vector3& p4) {
		rotate(0, 2, angle, origin, p1, p2, p3, p4);
    }

    inline static void rotateZ(float angle, const Vector3& origin, Vector3& p1, Vector3& p2, Vector3& p3, Vector3& p4) {
		rotate(1, 0, angle, origin, p1, p2, p3, p4);
    }
};