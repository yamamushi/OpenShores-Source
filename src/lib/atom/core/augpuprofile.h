#ifndef AUGPUPROFILE_H
#define AUGPUPROFILE_H

#include "auglobal.h"
#include "auframetiming.h"
#include "auglsl.h"
#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QVariant>

// Optional local diagnostics: OPENSHORES_GPU_PROFILE=1. Some Mac drivers return
// zero for both timestamp and elapsed GPU queries. For those investigations,
// isolate one render category every two seconds with explicit completion waits.
// These are synchronized CPU + driver + GPU times, not hardware GPU counters.
// No context lookups, waits or logging run during a normal launch.
namespace AuGpuTiming
{
struct Context : QObject
{
	QElapsedTimer clock, frameClock;
	QOpenGLFunctions* gl;
	qint64 nextSample = 0, sampledNs = 0;
	quint64 frameDraws = 0, frameVertices = 0, draws = 0, vertices = 0;
	int nextTarget = 0, calls = 0;
	bool sampling = false, active = false;
	const char* target = 0;
	Context(QOpenGLContext* parent) : QObject(parent), gl(parent->functions()) { clock.start(); }
};
inline Context* Current()
{
	static const bool enabled = qEnvironmentVariableIntValue("OPENSHORES_GPU_PROFILE") != 0;
	if(!enabled) return 0;
	QOpenGLContext* gl = QOpenGLContext::currentContext();
	if(!gl) return 0;
	const char* key = "openshoresGpuTiming";
	Context* context = static_cast<Context*>(gl->property(key).value<QObject*>());
	if(!context)
	{
		context = new Context(gl);
		gl->setProperty(key, QVariant::fromValue<QObject*>(context));
	}
	return context;
}
}
class AuGpuProfile
{
	AuFrameTiming::Scope cpuScope;
	AuGpuTiming::Context* context = 0;
	QElapsedTimer timer;
	quint64 draws = 0, vertices = 0;
public:
	explicit AuGpuProfile(const char* name) : cpuScope(name)
	{
		AuGpuTiming::Context* c = AuGpuTiming::Current();
		if(!c || !c->sampling || c->active || qstrcmp(c->target, name)) return;
		context = c;
		context->active = true;
		// Drain earlier passes before timing this one, so their cost is excluded.
		context->gl->glFinish();
		draws = AuRenderCounters::s_DrawCalls;
		vertices = AuRenderCounters::s_Vertices;
		timer.start();
	}
	~AuGpuProfile()
	{
		if(!context) return;
		context->gl->glFinish();
		context->sampledNs += timer.nsecsElapsed();
		context->draws += AuRenderCounters::s_DrawCalls - draws;
		context->vertices += AuRenderCounters::s_Vertices - vertices;
		context->calls++;
		context->active = false;
	}
};
class AuGpuFrameProfile
{
	AuGpuTiming::Context* context = 0;
public:
	AuGpuFrameProfile()
	{
		AuGpuTiming::Context* c = AuGpuTiming::Current();
		if(!c || c->sampling || c->clock.elapsed() < c->nextSample) return;
		static const char* targets[] = {"terrain.at", "terrain.near", "flora", "sea", "atmosphere", "rocks", "geometry", "world.effects", "controls"};
		context = c;
		context->target = targets[context->nextTarget++ % 9];
		context->nextSample = context->clock.elapsed() + 2000;
		context->sampledNs = 0; context->calls = 0;
		context->draws = context->vertices = 0;
		context->frameDraws = AuRenderCounters::s_DrawCalls;
		context->frameVertices = AuRenderCounters::s_Vertices;
		context->sampling = true;
		context->gl->glFinish();
		context->frameClock.start();
	}
	~AuGpuFrameProfile()
	{
		if(!context) return;
		context->gl->glFinish();
		double frameMs = context->frameClock.nsecsElapsed()/1000000.;
		context->sampling = false;
		AuGlobal::Log(g_Au.CacheApp() + "/soh.txt", QString(
			"gpu sync profile: target=%1; target_sync=%2ms/%3draws/%4tris/%5calls; "
			"frame_sync=%6ms/%7draws/%8tris; explicit waits enabled on this diagnostic frame")
			.arg(QString::fromLatin1(context->target)).arg(context->sampledNs/1000000., 0, 'f', 3)
			.arg(context->draws).arg(context->vertices/3).arg(context->calls)
			.arg(frameMs, 0, 'f', 3).arg(AuRenderCounters::s_DrawCalls-context->frameDraws)
			.arg((AuRenderCounters::s_Vertices-context->frameVertices)/3));
	}
};
#endif
