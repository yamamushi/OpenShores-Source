#ifndef AUCOMPACTPALETTE_H
#define AUCOMPACTPALETTE_H

#include <QtGui/qopengl.h>
#include <QByteArray>
#include <QVector>
#include <cmath>

// Devices with 16 fragment samplers need room inside that budget for shadows.
// The existing layout remains in use on devices supporting 18 or more.
class AuShadowTextureLayout
{
public:
	static int FragmentUnits;
	static bool Compact() { return FragmentUnits >= 16 && FragmentUnits < 18; }
	static int SunUnit() { return Compact() ? 14 : 16; }
	static int LightUnit() { return Compact() ? 15 : 17; }
};

// Keep all material slots. The two overflow textures share sampler slots 0/1
// with the first bank, with consecutive triangle runs drawn in original order.
inline void AuCompactPaletteSource(QByteArray& source)
{
	for(const QByteArray& name : {QByteArray("texSurface"), QByteArray("texClothing")})
	{
		if(!source.contains(name + "[15]")) continue;
		source.replace(name + "[15]", "compactPalette[13]");
		source.replace(name + "[13]", "compactPalette[0]");
		source.replace(name + "[14]", "compactPalette[1]");
		source.replace(name + "[", "compactPalette[");
	}
	if(source.contains("texPavement[7]"))
	{
		const QByteArray name = source.contains("texGrass;") ? "compactPavement9" : "compactPavement8";
		source.replace("texPavement[7]", name + "[5]");
		source.replace("texPavement[5]", name + "[0]");
		source.replace("texPavement[6]", name + "[1]");
		source.replace("texPavement[", name + "[");
	}
}

class AuPaletteRanges
{
public:
	struct Range { int first, count; bool overflow; };
	QVector<Range> ranges;
	void Clear() { ranges.clear(); }
	void Build(const float* values, int vertices, int stride, const QVector<unsigned int>& indices,
			bool pavement=false)
	{
		Clear();
		const int count = indices.isEmpty() ? vertices : indices.size();
		if(!values || vertices<=0 || stride<=0 || count%3) return;
		for(int first=0; first<count; first+=3)
		{
			// GL's default last provoking vertex supplies the flat material index.
			const unsigned int vertex = indices.isEmpty() ? unsigned(first+2) : indices[first+2];
			if(vertex>=unsigned(vertices)) { Clear(); return; }
			float value = values[vertex*stride];
			if(!std::isfinite(value)) { Clear(); return; }
			int index = pavement ? int(std::round(value*7.f)) : int(value);
			bool overflow = index<0 || index >= (pavement ? 5 : 13);
			if(!ranges.isEmpty() && ranges.last().overflow==overflow) ranges.last().count+=3;
			else { Range run={first,3,overflow}; ranges.append(run); }
		}
	}

	// Caller enables vertex attributes and supplies the indexed/nonindexed draw.
	// Only the two reused 2D bindings change; array shadow bindings stay intact.
	template<class GL, class Draw> bool DrawGL(GL* gl, Draw draw) const
	{
		if(!AuShadowTextureLayout::Compact() || ranges.isEmpty()) return false;
		int program=0;gl->glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		int location=gl->glGetUniformLocation(program,"compactPalette[0]");
		int base=1, bank=13;
		if(location<0) { location=gl->glGetUniformLocation(program,"compactPavement9[0]");base=9;bank=5; }
		if(location<0) { location=gl->glGetUniformLocation(program,"compactPavement8[0]");base=8;bank=5; }
		if(location<0) return false;
		int units[13];for(int i=0;i<bank;i++) units[i]=base+i;
		gl->glUniform1iv(location,bank,units);
		int active=0, textures[4];gl->glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
		for(int i=0;i<4;i++)
		{
			gl->glActiveTexture(GL_TEXTURE0+base+(i<2 ? i : bank+i-2));
			gl->glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);
		}
		bool overflow=false;
		for(const Range& run : ranges)
		{
			if(run.overflow!=overflow)
			{
				for(int i=0;i<2;i++) { gl->glActiveTexture(GL_TEXTURE0+base+i);gl->glBindTexture(GL_TEXTURE_2D,textures[(run.overflow?2:0)+i]); }
				overflow=run.overflow;
			}
			draw(run.first,run.count);
		}
		if(overflow) for(int i=0;i<2;i++) { gl->glActiveTexture(GL_TEXTURE0+base+i);gl->glBindTexture(GL_TEXTURE_2D,textures[i]); }
		gl->glActiveTexture(active);
		return true;
	}
};
#endif
