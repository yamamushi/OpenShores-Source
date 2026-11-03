#ifndef AUVIEWCLIP_H
#define AUVIEWCLIP_H

#include "aumatrix4x4.h"
#include <cmath>
#include <limits>

// Conservative side-plane tests using the same float matrices uploaded to GLSL.
// Do not test near/far planes: GL_DEPTH_CLAMP can keep that geometry visible.
class AuViewClip
{
	double plane[4][4], magnitude[4][4];
	double eyePlane[4][3], eyeMagnitude[4][3];
	bool valid = true;
public:
	AuViewClip(const AuMatrix4x4& projection, const AuMatrix4x4& model)
	{
		const double* p = projection.GLMatrix();
		const double* m = model.GLMatrix();
		for(int side=0; side<4; side++)
		{
			const int axis = side/2;
			const double sign = side%2 ? -1 : 1;
			for(int k=0; k<3; k++)
			{
				const double w = float(p[k*4+3]), a = float(p[k*4+axis]);
				eyePlane[side][k] = w+sign*a;
				eyeMagnitude[side][k] = std::fabs(w)+std::fabs(a);
			}
			for(int col=0; col<4; col++)
			{
				plane[side][col] = magnitude[side][col] = 0;
				for(int k=0; k<4; k++)
				{
					const double w = float(p[k*4+3]), a = float(p[k*4+axis]), b = float(m[col*4+k]);
					plane[side][col] += (w + sign*a)*b;
					magnitude[side][col] += (std::fabs(w)+std::fabs(a))*std::fabs(b);
				}
				valid = valid && std::isfinite(plane[side][col]) && std::isfinite(magnitude[side][col]);
			}
		}
	}
	bool Intersects(const AuPoint& low, const AuPoint& high) const
	{
		if(!valid) return true;
		const double lo[] = {low.x,low.y,low.z}, hi[] = {high.x,high.y,high.z};
		for(int k=0; k<3; k++)
			if(!std::isfinite(lo[k]) || !std::isfinite(hi[k]) || lo[k]>hi[k]) return true;
		for(int side=0; side<4; side++)
		{
			double maximum = plane[side][3], error = magnitude[side][3];
			for(int k=0; k<3; k++)
			{
				maximum += plane[side][k]*(plane[side][k]>=0 ? hi[k] : lo[k]);
				error += magnitude[side][k]*qMax(std::fabs(lo[k]),std::fabs(hi[k]));
			}
			// Bound float matrix multiplication, position transformation and clip
			// comparison error; retain uncertain patches rather than risk a hole.
			const double tolerance = 64*std::numeric_limits<float>::epsilon()*(error+1);
			if(maximum < -tolerance) return false;
		}
		return true;
	}
	bool IntersectsPlaced(const AuPoint& low, const AuPoint& high, const AuMatrix4x4& placement,
			const AuPoint& force, double maxMove) const
	{
		if(!valid || !std::isfinite(maxMove) || maxMove<0) return true;
		const double lo[] = {low.x,low.y,low.z,1}, hi[] = {high.x,high.y,high.z,1};
		const double wind[] = {float(force.x),float(force.y),float(force.z)};
		for(int k=0; k<3; k++)
			if(!std::isfinite(lo[k]) || !std::isfinite(hi[k]) || lo[k]>hi[k] || !std::isfinite(wind[k])) return true;
		const double* matrix = placement.GLMatrix();
		for(int side=0; side<4; side++)
		{
			double maximum = 0, error = 0, windShift = 0, windError = 0;
			for(int col=0; col<4; col++)
			{
				double coefficient = 0, bound = 0;
				for(int k=0; k<4; k++)
				{
					const double v = float(matrix[col*4+k]);
					if(!std::isfinite(v)) return true;
					coefficient += plane[side][k]*v;
					bound += magnitude[side][k]*std::fabs(v);
				}
				maximum += coefficient*(coefficient>=0 ? hi[col] : lo[col]);
				error += bound*qMax(std::fabs(lo[col]),std::fabs(hi[col]));
			}
			for(int k=0; k<3; k++)
			{
				windShift += eyePlane[side][k]*wind[k];
				windError += eyeMagnitude[side][k]*std::fabs(wind[k]);
			}
			maximum += std::fabs(windShift)*maxMove;
			error += windError*maxMove;
			const double tolerance = 128*std::numeric_limits<float>::epsilon()*(error+1);
			if(maximum < -tolerance) return false;
		}
		return true;
	}

};
#endif
