#ifndef AUFRAMETIMING_H
#define AUFRAMETIMING_H

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QTimer>
#include <QVector>

// Opt-in CPU wall-clock diagnostics. Never waits for the GPU or reads GL state.
// Inline thread-local storage keeps worker/context activity out of view frames.
namespace AuFrameTiming
{
inline bool Enabled()
{
	static const bool enabled = qEnvironmentVariableIntValue("OPENSHORES_FRAME_TIMING") != 0;
	return enabled;
}
enum Category
{
	TerrainAt, TerrainNear, Flora, Sea, Atmosphere, Rocks, Geometry, Effects, Controls,
	Sync, Animate, Prepare, Finish, Mesh, FloraMesh, TextureUpload, BufferUpdate, ShaderCheck, Count
};
inline const char* Name(int category)
{
	static const char* names[] = {"terrain.at", "terrain.near", "flora", "sea", "atmosphere", "rocks",
		"geometry", "world.effects", "controls", "scene.sync", "scene.animate", "scene.prepare", "scene.finish",
		"mesh.create", "flora.mesh", "texture.upload", "buffer.update", "shader.check"};
	return names[category];
}
struct Totals
{
	qint64 ns[Count] = {};
	quint64 calls[Count] = {};
};
inline Totals*& Active()
{
	static thread_local Totals* active = 0;
	return active;
}
class Scope
{
	Totals* totals;
	Category category;
	QElapsedTimer timer;
public:
	explicit Scope(Category value) : totals(Enabled() ? Active() : 0), category(value)
	{
		if(totals) timer.start();
	}
	explicit Scope(const char* name) : totals(Enabled() ? Active() : 0), category(Count)
	{
		if(!totals) return;
		for(int i=0; i<Count; ++i)
			if(qstrcmp(name,Name(i))==0) { category=Category(i); timer.start(); return; }
		totals=0;
	}
	~Scope()
	{
		if(totals) { totals->ns[category]+=timer.nsecsElapsed(); ++totals->calls[category]; }
	}
	Scope(const Scope&)=delete;
	Scope& operator=(const Scope&)=delete;
};
struct Record
{
	bool swap = false;
	quint64 frame = 0, draws = 0, vertices = 0;
	qint64 at = 0, interval = -1, paint = -1, paintToSwap = -1, logger = 0;
	Totals totals;
};
class Recorder : public QObject
{
	QElapsedTimer clock;
	QFile file;
	QVector<Record> records;
	quint64 frame = 0, dropped = 0;
	qint64 previousPaint = -1, previousSwap = -1, lastPaint = -1, loggerNs = 0;
	bool paintPending = false;
public:
	explicit Recorder(QObject* owner, const QString& directory) : QObject(owner)
	{
		clock.start();
		records.reserve(2048);
		QDir().mkpath(directory);
		file.setFileName(QDir(directory).filePath(QString("frame-timing-%1-%2-%3.csv")
			.arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz"))
			.arg(QCoreApplication::applicationPid()).arg(quintptr(owner),0,16)));
		if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))
			qWarning("Cannot open frame timing file: %s",qPrintable(file.errorString()));
		else
		{
			QByteArray header="# CPU wall times; nested categories overlap; no GPU synchronization. Frame intervals include event-loop/composition/waits. Swap timestamps indicate Qt frameSwapped, not physical display scanout.\n";
			header+="record,frame,elapsed_ms,interval_ms,paint_ms,paint_to_swap_ms,draws,triangles,logger_io_ms,dropped_records";
			for(int i=0;i<Count;++i) header+=QByteArray(",")+Name(i)+"_ms,"+Name(i)+"_calls";
			header+='\n'; file.write(header);
			qInfo("Frame timing enabled: %s",qPrintable(file.fileName()));
		}
		QTimer* timer=new QTimer(this);
		connect(timer,&QTimer::timeout,this,[this]() { Flush(); });
		timer->start(2000);
	}
	~Recorder() { Flush(); }
	Record Begin()
	{
		Record r;
		r.frame=++frame; r.at=clock.nsecsElapsed();
		if(previousPaint>=0) r.interval=r.at-previousPaint;
		previousPaint=lastPaint=r.at;
		paintPending=true;
		r.logger=loggerNs; loggerNs=0;
		return r;
	}
	void Append(const Record& r)
	{
		// Bound memory if the event loop cannot service the logging timer.
		if(records.size()<2048) records.append(r); else ++dropped;
	}
	void Swapped()
	{
		// Exclude composition-only swaps with no new paintGL invocation.
		if(!paintPending) return;
		paintPending=false;
		Record r; r.swap=true; r.frame=frame; r.at=clock.nsecsElapsed();
		if(previousSwap>=0) r.interval=r.at-previousSwap;
		previousSwap=r.at; r.paintToSwap=r.at-lastPaint;
		Append(r);
	}
	void Flush()
	{
		if(records.isEmpty()) return;
		QElapsedTimer timer; timer.start();
		QByteArray output;
		for(const Record& r : records)
		{
			auto ms=[](qint64 ns) { return QByteArray::number(ns<0 ? -1. : ns/1000000.,'f',3); };
			output+=(r.swap ? "swap," : "frame,")+QByteArray::number(r.frame)+','+ms(r.at)+','+ms(r.interval)+','+
				ms(r.paint)+','+ms(r.paintToSwap)+','+QByteArray::number(r.draws)+','+QByteArray::number(r.vertices/3)+','+
				ms(r.logger)+','+QByteArray::number(dropped);
			for(int i=0;i<Count;++i) output+=','+ms(r.totals.ns[i])+','+QByteArray::number(r.totals.calls[i]);
			output+='\n';
		}
		if(file.isOpen() && (file.write(output)!=output.size() || !file.flush()))
			qWarning("Frame timing write failed: %s",qPrintable(file.errorString()));
		records.clear();
		loggerNs+=timer.nsecsElapsed();
	}
};
class Frame
{
	Recorder* recorder;
	Record record;
	Totals* previous = 0;
	quint64 draws, vertices;
	QElapsedTimer timer;
public:
	Frame(Recorder* output, quint64 initialDraws, quint64 initialVertices) : recorder(output), draws(initialDraws), vertices(initialVertices)
	{
		if(!recorder) return;
		record=recorder->Begin(); previous=Active(); Active()=&record.totals; timer.start();
	}
	void End(quint64 finalDraws, quint64 finalVertices)
	{
		if(!recorder) return;
		record.paint=timer.nsecsElapsed(); record.draws=finalDraws-draws; record.vertices=finalVertices-vertices;
		Active()=previous; recorder->Append(record); recorder=0;
	}
	~Frame() { if(recorder) Active()=previous; }
	Frame(const Frame&)=delete;
	Frame& operator=(const Frame&)=delete;
};
}
#endif
