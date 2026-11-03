#ifndef AUVIEWRANGES_H
#define AUVIEWRANGES_H

#include "auviewclip.h"
#include <QtGui/qopengl.h>
#include <QVector>

// Consecutive triangle ranges retain blending and provoking-vertex order.
// Bounds are built once from the actual uploaded positions, after indexing.
class AuViewRanges
{
	struct Range { AuPoint low, high; int first, count; };
	QVector<Range> ranges;
public:
	void Clear() { ranges.clear(); }
	void Build(const float* positions, int vertices, const QVector<GLuint>& indices)
	{
		ranges.clear();
		const int count = indices.isEmpty() ? vertices : indices.size();
		if(!positions || vertices<=0 || count%3) return;
		const int chunk = 32*3;
		for(int first=0; first<count; first+=chunk)
		{
			Range range; range.first = first; range.count = qMin(chunk,count-first);
			for(int i=0; i<range.count; i++)
			{
				const GLuint index = indices.isEmpty() ? GLuint(first+i) : indices[first+i];
				if(index>=GLuint(vertices)) { ranges.clear(); return; }
				const float* p = positions+index*3;
				if(!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) { ranges.clear(); return; }
				if(!i) range.low = range.high = AuPoint(p[0],p[1],p[2]);
				else
				{
					range.low.x=qMin(range.low.x,double(p[0])); range.high.x=qMax(range.high.x,double(p[0]));
					range.low.y=qMin(range.low.y,double(p[1])); range.high.y=qMax(range.high.y,double(p[1]));
					range.low.z=qMin(range.low.z,double(p[2])); range.high.z=qMax(range.high.z,double(p[2]));
				}
			}
			ranges.append(range);
		}
	}
	// Return false when bounds are unavailable so callers use the original draw.
	template<class Draw>
	bool DrawVisible(const AuViewClip& clip, float minRadius, float maxRadius, Draw draw) const
	{
		if(ranges.isEmpty() || !std::isfinite(minRadius) || !std::isfinite(maxRadius) ||
				minRadius<0 || maxRadius<minRadius) return false;
		int first=0, count=0;
		for(const Range& range : ranges)
		{
			AuPoint low,high;
			const double lo[]={range.low.x,range.low.y,range.low.z}, hi[]={range.high.x,range.high.y,range.high.z};
			double* lower[]={&low.x,&low.y,&low.z}; double* upper[]={&high.x,&high.y,&high.z};
			for(int k=0; k<3; k++)
			{
				*lower[k]=qMin(lo[k]*minRadius,lo[k]*maxRadius);
				*upper[k]=qMax(hi[k]*minRadius,hi[k]*maxRadius);
				// Cloud shaders add separately rounded base-radius and lift terms.
				const double margin=8*std::numeric_limits<float>::epsilon()*(qMax(std::fabs(*lower[k]),std::fabs(*upper[k]))+1);
				*lower[k]-=margin; *upper[k]+=margin;
			}
			if(clip.Intersects(low,high))
			{
				if(!count) first=range.first;
				count+=range.count;
			}
			else if(count) { draw(first,count); count=0; }
		}
		if(count) draw(first,count);
		return true;
	}
};
#endif
