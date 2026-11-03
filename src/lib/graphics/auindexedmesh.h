#ifndef AUINDEXEDMESH_H
#define AUINDEXEDMESH_H

#include <QtGui/qopengl.h>
#include <QHash>
#include <QVector>
#include <cstring>
#include <initializer_list>
#include <unordered_map>

struct AuVertexStream
{
	float* data;
	int components;
};

// Compact only bit-identical complete vertices. In particular, UV seams,
// pavement boundaries, normals and opacity differences must remain distinct.
// Keep the original triangle and provoking-vertex order in the index list.
template<int Components>
int AuIndexVertexStreams(int vertices, std::initializer_list<AuVertexStream> streams, QVector<GLuint>& indices)
{
	indices.clear();
	int total = 0;
	for(const AuVertexStream& stream : streams) total += stream.components;
	Q_ASSERT(total == Components);
	if(vertices <= 0 || total != Components) return vertices;
	struct Key
	{
		float values[Components];
		bool operator==(const Key& other) const { return std::memcmp(values, other.values, sizeof(values)) == 0; }
	};
	struct Hash { size_t operator()(const Key& key) const { return qHashBits(key.values, sizeof(key.values)); } };
	std::unordered_map<Key, GLuint, Hash> seen;
	seen.reserve(vertices/2);
	indices.reserve(vertices);
	int unique = 0;
	for(int vertex=0; vertex<vertices; vertex++)
	{
		Key key;
		int offset = 0;
		for(const AuVertexStream& stream : streams)
		{
			std::memcpy(key.values+offset, stream.data+vertex*stream.components, stream.components*sizeof(float));
			offset += stream.components;
		}
		auto found = seen.find(key);
		if(found == seen.end())
		{
			found = seen.emplace(key, GLuint(unique)).first;
			offset = 0;
			for(const AuVertexStream& stream : streams)
			{
				// unique <= vertex, so writes cannot overwrite unread input.
				std::memcpy(stream.data+unique*stream.components, key.values+offset, stream.components*sizeof(float));
				offset += stream.components;
			}
			unique++;
		}
		indices.append(found->second);
	}
	if(unique == vertices) indices.clear(); // No reuse: retain the existing non-indexed draw.
	return unique;
}
#endif
