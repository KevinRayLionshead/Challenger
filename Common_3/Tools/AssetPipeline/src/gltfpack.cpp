// gltfpack is part of meshoptimizer library; see meshoptimizer.h for version/license details
//
// gltfpack is a command-line tool that takes a glTF file as an input and can produce two types of files:
// - regular glb/gltf files that use data that has been optimized for GPU consumption using various cache optimizers
// and quantization
// - packed glb/gltf files that additionally use meshoptimizer codecs to reduce the size of vertex/index data; these
// files can be further compressed with deflate/etc.
//
// To load regular glb files, it should be sufficient to use a standard glTF loader (although note that these files
// use quantized position/texture coordinates that are technically invalid per spec; THREE.js and BabylonJS support
// these files out of the box).
// To load packed glb files, meshoptimizer vertex decoder needs to be integrated into the loader; demo/GLTFLoader.js
// contains a work-in-progress loader - please note that the extension specification isn't ready yet so the format
// will change!
//
// gltfpack supports materials, meshes, nodes, skinning and animations
// gltfpack doesn't support morph targets, lights and cameras

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifndef _CRT_NONeastlC_NO_WARNINGS
#define _CRT_NONeastlC_NO_WARNINGS
#endif

#include "../../../ThirdParty/OpenSource/meshoptimizer/src/meshoptimizer.h"

// Used for Obj loading
//#include <algorithm>

#include "../../../../Common_3/ThirdParty/OpenSource/EASTL/vector.h"
#include "../../../../Common_3/ThirdParty/OpenSource/EASTL/string.h"

#include "gltfpack.h"

#include "../../../OS/Interfaces/IMemory.h"    //NOTE: this should be the last include in a .cpp

struct NodeInfo
{
	bool keep;
	bool named;
	bool animated;

	unsigned int animated_paths;

	int remap;
};

struct BufferView
{
	enum Kind
	{
		Kind_Vertex,
		Kind_Index,
		Kind_Skin,
		Kind_Time,
		Kind_Keyframe,
		Kind_Image,
		Kind_Count
	};

	Kind kind;
	int variant;
	size_t stride;
	bool compressed;

	eastl::string data;
};

const char* getError(cgltf_result result)
{
	switch (result)
	{
	case cgltf_result_file_not_found:
		return "file not found";

	case cgltf_result_io_error:
		return "I/O error";

	case cgltf_result_invalid_json:
		return "invalid JSON";

	case cgltf_result_invalid_gltf:
		return "invalid GLTF";

	case cgltf_result_out_of_memory:
		return "out of memory";

	default:
		return "unknown error";
	}
}

cgltf_accessor* getAccessor(const cgltf_attribute* attributes, size_t attribute_count, cgltf_attribute_type type, int index = 0)
{
	for (size_t i = 0; i < attribute_count; ++i)
		if (attributes[i].type == type && attributes[i].index == index)
			return attributes[i].data;

	return 0;
}

void transformPosition(float* ptr, const float* transform)
{
	float x = ptr[0] * transform[0] + ptr[1] * transform[4] + ptr[2] * transform[8] + transform[12];
	float y = ptr[0] * transform[1] + ptr[1] * transform[5] + ptr[2] * transform[9] + transform[13];
	float z = ptr[0] * transform[2] + ptr[1] * transform[6] + ptr[2] * transform[10] + transform[14];

	ptr[0] = x;
	ptr[1] = y;
	ptr[2] = z;
}

void transformNormal(float* ptr, const float* transform)
{
	float x = ptr[0] * transform[0] + ptr[1] * transform[4] + ptr[2] * transform[8];
	float y = ptr[0] * transform[1] + ptr[1] * transform[5] + ptr[2] * transform[9];
	float z = ptr[0] * transform[2] + ptr[1] * transform[6] + ptr[2] * transform[10];

	float l = sqrtf(x * x + y * y + z * z);
	float s = (l == 0.f) ? 0.f : 1 / l;

	ptr[0] = x * s;
	ptr[1] = y * s;
	ptr[2] = z * s;
}

void transformMesh(Mesh& mesh, const cgltf_node* node)
{
	float transform[16];
	cgltf_node_transform_world(node, transform);

	for (size_t si = 0; si < mesh.streams.size(); ++si)
	{
		Stream& stream = mesh.streams[si];

		if (stream.type == cgltf_attribute_type_position)
		{
			for (size_t i = 0; i < stream.data.size(); ++i)
				transformPosition(stream.data[i].f, transform);
		}
		else if (stream.type == cgltf_attribute_type_normal || stream.type == cgltf_attribute_type_tangent)
		{
			for (size_t i = 0; i < stream.data.size(); ++i)
				transformNormal(stream.data[i].f, transform);
		}
	}
}

void parseMeshesGltf(cgltf_data* data, eastl::vector<Mesh>& meshes)
{
	for (size_t ni = 0; ni < data->nodes_count; ++ni)
	{
		cgltf_node& node = data->nodes[ni];

		if (!node.mesh)
			continue;

		const cgltf_mesh& mesh = *node.mesh;

		for (size_t pi = 0; pi < mesh.primitives_count; ++pi)
		{
			const cgltf_primitive& primitive = mesh.primitives[pi];

			if (!primitive.indices)
				continue;

			if (primitive.type != cgltf_primitive_type_triangles)
				continue;

			Mesh result;

			result.node = &node;

			result.material = primitive.material;
			result.skin = node.skin;

			result.indices.resize(primitive.indices->count);
			for (size_t i = 0; i < primitive.indices->count; ++i)
				result.indices[i] = unsigned(cgltf_accessor_read_index(primitive.indices, i));

			for (size_t ai = 0; ai < primitive.attributes_count; ++ai)
			{
				const cgltf_attribute& attr = primitive.attributes[ai];

				if (attr.type == cgltf_attribute_type_invalid)
					continue;

				Stream s = { attr.type, attr.index };
				s.data.resize(attr.data->count);

				for (size_t i = 0; i < attr.data->count; ++i)
					cgltf_accessor_read_float(attr.data, i, s.data[i].f, 4);

				result.streams.push_back(s);
			}

			meshes.push_back(result);
		}
	}
}

void defaultFree(void*, void* p)
{
	conf_free(p);
}

size_t textureIndex(const eastl::vector<eastl::string>& textures, const eastl::string& name)
{
	eastl::vector<eastl::string>::const_iterator it = eastl::lower_bound(textures.begin(), textures.end(), name);
	ASSERT(it != textures.end());
	ASSERT(*it == name);

	return size_t(it - textures.begin());
}

void parseMeshesObj(fastObjMesh* obj, cgltf_data* data, eastl::vector<Mesh>& meshes)
{
	unsigned int material_count = eastl::max(obj->material_count, 1u);

	eastl::vector<size_t> vertex_count(material_count);
	eastl::vector<size_t> index_count(material_count);

	for (unsigned int fi = 0; fi < obj->face_count; ++fi)
	{
		unsigned int mi = obj->face_materials[fi];

		vertex_count[mi] += obj->face_vertices[fi];
		index_count[mi] += (obj->face_vertices[fi] - 2) * 3;
	}

	eastl::vector<size_t> mesh_index(material_count);

	for (unsigned int mi = 0; mi < material_count; ++mi)
	{
		if (index_count[mi] == 0)
			continue;

		mesh_index[mi] = meshes.size();

		meshes.push_back(Mesh());

		Mesh& mesh = meshes.back();

		if (data->materials_count)
		{
			ASSERT(mi < data->materials_count);
			mesh.material = &data->materials[mi];
		}

		mesh.streams.resize(3);
		mesh.streams[0].type = cgltf_attribute_type_position;
		mesh.streams[0].data.resize(vertex_count[mi]);
		mesh.streams[1].type = cgltf_attribute_type_normal;
		mesh.streams[1].data.resize(vertex_count[mi]);
		mesh.streams[2].type = cgltf_attribute_type_texcoord;
		mesh.streams[2].data.resize(vertex_count[mi]);
		mesh.indices.resize(index_count[mi]);
	}

	eastl::vector<size_t> vertex_offset(material_count);
	eastl::vector<size_t> index_offset(material_count);

	size_t group_offset = 0;

	for (unsigned int fi = 0; fi < obj->face_count; ++fi)
	{
		unsigned int mi = obj->face_materials[fi];
		Mesh& mesh = meshes[mesh_index[mi]];

		size_t vo = vertex_offset[mi];
		size_t io = index_offset[mi];

		for (unsigned int vi = 0; vi < obj->face_vertices[fi]; ++vi)
		{
			fastObjIndex ii = obj->indices[group_offset + vi];

			Attr p = { {obj->positions[ii.p * 3 + 0], obj->positions[ii.p * 3 + 1], obj->positions[ii.p * 3 + 2]} };
			Attr n = { {obj->normals[ii.n * 3 + 0], obj->normals[ii.n * 3 + 1], obj->normals[ii.n * 3 + 2]} };
			Attr t = { {obj->texcoords[ii.t * 2 + 0], 1.f - obj->texcoords[ii.t * 2 + 1]} };

			mesh.streams[0].data[vo + vi] = p;
			mesh.streams[1].data[vo + vi] = n;
			mesh.streams[2].data[vo + vi] = t;
		}

		for (unsigned int vi = 2; vi < obj->face_vertices[fi]; ++vi)
		{
			size_t to = io + (vi - 2) * 3;

			mesh.indices[to + 0] = unsigned(vo);
			mesh.indices[to + 1] = unsigned(vo + vi - 1);
			mesh.indices[to + 2] = unsigned(vo + vi);
		}

		vertex_offset[mi] += obj->face_vertices[fi];
		index_offset[mi] += (obj->face_vertices[fi] - 2) * 3;
		group_offset += obj->face_vertices[fi];
	}
}

bool canMerge(const Mesh& lhs, const Mesh& rhs)
{
	if (lhs.node != rhs.node)
	{
		if (!lhs.node || !rhs.node)
			return false;

		if (lhs.node->parent != rhs.node->parent)
			return false;

		bool lhs_transform = lhs.node->has_translation | lhs.node->has_rotation | lhs.node->has_scale | lhs.node->has_matrix;
		bool rhs_transform = rhs.node->has_translation | rhs.node->has_rotation | rhs.node->has_scale | rhs.node->has_matrix;

		if (lhs_transform || rhs_transform)
			return false;

		// we can merge nodes that don't have transforms of their own and have the same parent
		// this is helpful when instead of splitting mesh into primitives, DCCs split mesh into mesh nodes
	}

	if (lhs.material != rhs.material)
		return false;

	if (lhs.skin != rhs.skin)
		return false;

	if (lhs.streams.size() != rhs.streams.size())
		return false;

	for (size_t i = 0; i < lhs.streams.size(); ++i)
		if (lhs.streams[i].type != rhs.streams[i].type || lhs.streams[i].index != rhs.streams[i].index)
			return false;

	return true;
}

void mergeMeshes(Mesh& target, const Mesh& mesh)
{
	ASSERT(target.streams.size() == mesh.streams.size());

	size_t vertex_offset = target.streams[0].data.size();
	size_t index_offset = target.indices.size();

	for (size_t i = 0; i < target.streams.size(); ++i)
		target.streams[i].data.insert(target.streams[i].data.end(), mesh.streams[i].data.begin(), mesh.streams[i].data.end());

	target.indices.resize(target.indices.size() + mesh.indices.size());

	size_t index_count = mesh.indices.size();

	for (size_t i = 0; i < index_count; ++i)
		target.indices[index_offset + i] = unsigned(vertex_offset + mesh.indices[i]);
}

void mergeMeshes(eastl::vector<Mesh>& meshes)
{
	for (size_t i = 0; i < meshes.size(); ++i)
	{
		Mesh& mesh = meshes[i];

		for (size_t j = 0; j < i; ++j)
		{
			Mesh& target = meshes[j];

			if (target.indices.size() && canMerge(mesh, target))
			{
				mergeMeshes(target, mesh);

				mesh.streams.clear();
				mesh.indices.clear();

				break;
			}
		}
	}
}

void reindexMesh(Mesh& mesh)
{
	size_t total_vertices = mesh.streams[0].data.size();
	size_t total_indices = mesh.indices.size();

	eastl::vector<meshopt_Stream> streams;
	for (size_t i = 0; i < mesh.streams.size(); ++i)
	{
		meshopt_Stream stream = { &mesh.streams[i].data[0], sizeof(Attr), sizeof(Attr) };
		streams.push_back(stream);
	}

	eastl::vector<unsigned int> remap(total_indices);
	size_t unique_vertices = meshopt_generateVertexRemapMulti(&remap[0], &mesh.indices[0], total_indices, total_vertices, &streams[0], streams.size());

	meshopt_remapIndexBuffer(&mesh.indices[0], &mesh.indices[0], total_indices, &remap[0]);

	for (size_t i = 0; i < mesh.streams.size(); ++i)
	{
		meshopt_remapVertexBuffer(&mesh.streams[i].data[0], &mesh.streams[i].data[0], total_vertices, sizeof(Attr), &remap[0]);
		mesh.streams[i].data.resize(unique_vertices);
	}
}

void stripifyMesh(Mesh& mesh)
{
	size_t vertIndex = 0;
	
	for (size_t i = 0; i < mesh.streams.size(); ++i)
	{
		if (mesh.streams[i].type == cgltf_attribute_type::cgltf_attribute_type_position)
		{
			vertIndex = i;
			break;
		}
	}

	size_t vertCount = mesh.streams[vertIndex].data.size();

	unsigned int restart_index = 0;

	eastl::vector<unsigned int> strip(meshopt_stripifyBound(mesh.indices.size()));
	strip.resize(meshopt_stripify(&strip[0], &mesh.indices[0], mesh.indices.size(), mesh.streams[vertIndex].data.size(), restart_index));

	eastl::vector<Attr> vertexData;
	vertexData.resize(strip.size());

	for (size_t i = 0; i < mesh.streams.size(); ++i)
	{
		for (size_t j = 0; j < strip.size(); ++j)
		{
			vertexData[j] = mesh.streams[i].data[strip[j]];
		}

		mesh.streams[i].data.resize(strip.size());
		mesh.streams[i].data = vertexData;
	}

	mesh.indices = strip;
}

void simplifyMesh(Mesh& mesh, float threshold)
{
	size_t vertIndex = 0;
	for (size_t i = 0; i < mesh.streams.size(); ++i)
	{
		if (mesh.streams[i].type == cgltf_attribute_type::cgltf_attribute_type_position)
		{
			vertIndex = i;
			break;
		}
	}

	size_t target_index_count = size_t(mesh.indices.size() * threshold);
	float target_error = 1e-2f;

	mesh.indices.resize(mesh.indices.size()); // note: simplify needs space for index_count elements in the destination array, not target_index_count
	mesh.indices.resize(meshopt_simplify(&mesh.indices[0], &mesh.indices[0], mesh.indices.size(), &mesh.streams[vertIndex].data[0].f[0], mesh.streams[0].data.size(), sizeof(Attr), target_index_count, target_error));

	size_t vertex_count = mesh.streams[vertIndex].data.size();
	eastl::vector<unsigned int> remap(vertex_count);
	size_t unique_vertices = meshopt_optimizeVertexFetchRemap(&remap[0], &mesh.indices[0], mesh.indices.size(), vertex_count);

	meshopt_remapIndexBuffer(&mesh.indices[0], &mesh.indices[0], mesh.indices.size(), &remap[0]);

	for (size_t i = 0; i < mesh.streams.size(); ++i)
	{
		meshopt_remapVertexBuffer(&mesh.streams[i].data[0], &mesh.streams[i].data[0], vertex_count, sizeof(Attr), &remap[0]);
		mesh.streams[i].data.resize(unique_vertices);
	}
}

void optimizeMesh(Mesh& mesh)
{
	size_t vertex_count = mesh.streams[0].data.size();

	meshopt_optimizeVertexCache(&mesh.indices[0], &mesh.indices[0], mesh.indices.size(), vertex_count);

	eastl::vector<unsigned int> remap(vertex_count);
	size_t unique_vertices = meshopt_optimizeVertexFetchRemap(&remap[0], &mesh.indices[0], mesh.indices.size(), vertex_count);

	ASSERT(unique_vertices == vertex_count);
	(void)unique_vertices;

	meshopt_remapIndexBuffer(&mesh.indices[0], &mesh.indices[0], mesh.indices.size(), &remap[0]);

	for (size_t i = 0; i < mesh.streams.size(); ++i)
		meshopt_remapVertexBuffer(&mesh.streams[i].data[0], &mesh.streams[i].data[0], vertex_count, sizeof(Attr), &remap[0]);
}

bool getAttributeBounds(const eastl::vector<Mesh>& meshes, cgltf_attribute_type type, Attr& min, Attr& max)
{
	min.f[0] = min.f[1] = min.f[2] = min.f[3] = +FLT_MAX;
	max.f[0] = max.f[1] = max.f[2] = max.f[3] = -FLT_MAX;

	bool valid = false;

	for (size_t i = 0; i < meshes.size(); ++i)
	{
		const Mesh& mesh = meshes[i];

		for (size_t j = 0; j < mesh.streams.size(); ++j)
		{
			const Stream& s = mesh.streams[j];

			if (s.type == type)
			{
				for (size_t k = 0; k < s.data.size(); ++k)
				{
					const Attr& a = s.data[k];

					min.f[0] = eastl::min(min.f[0], a.f[0]);
					min.f[1] = eastl::min(min.f[1], a.f[1]);
					min.f[2] = eastl::min(min.f[2], a.f[2]);
					min.f[3] = eastl::min(min.f[3], a.f[3]);

					max.f[0] = eastl::max(max.f[0], a.f[0]);
					max.f[1] = eastl::max(max.f[1], a.f[1]);
					max.f[2] = eastl::max(max.f[2], a.f[2]);
					max.f[3] = eastl::max(max.f[3], a.f[3]);

					valid = true;
				}
			}
		}
	}

	return valid;
}

QuantizationParams prepareQuantization(const eastl::vector<Mesh>& meshes, const Settings& settings)
{
	QuantizationParams result = {};

	result.pos_bits = settings.pos_bits;

	Attr pos_min, pos_max;
	if (getAttributeBounds(meshes, cgltf_attribute_type_position, pos_min, pos_max))
	{
		result.pos_offset[0] = pos_min.f[0];
		result.pos_offset[1] = pos_min.f[1];
		result.pos_offset[2] = pos_min.f[2];
		result.pos_scale = eastl::max(pos_max.f[0] - pos_min.f[0], eastl::max(pos_max.f[1] - pos_min.f[1], pos_max.f[2] - pos_min.f[2]));
	}

	result.uv_bits = settings.tex_bits;

	Attr uv_min, uv_max;
	if (getAttributeBounds(meshes, cgltf_attribute_type_texcoord, uv_min, uv_max))
	{
		result.uv_offset[0] = uv_min.f[0];
		result.uv_offset[1] = uv_min.f[1];
		result.uv_scale[0] = uv_max.f[0] - uv_min.f[0];
		result.uv_scale[1] = uv_max.f[1] - uv_min.f[1];
	}

	return result;
}

void rescaleNormal(float& nx, float& ny, float& nz)
{
	// scale the normal to make sure the largest component is +-1.0
	// this reduces the entropy of the normal by ~1.5 bits without losing precision
	// it's better to use octahedral encoding but that requires special shader support
	float nm = eastl::max(fabsf(nx), eastl::max(fabsf(ny), fabsf(nz)));
	float ns = nm == 0.f ? 0.f : 1 / nm;

	nx *= ns;
	ny *= ns;
	nz *= ns;
}

void renormalizeWeights(uint8_t(&w)[4])
{
	int sum = w[0] + w[1] + w[2] + w[3];

	if (sum == 255)
		return;

	// we assume that the total error is limited to 0.5/component = 2
	// this means that it's acceptable to adjust the max. component to compensate for the error
	int max = 0;

	for (int k = 1; k < 4; ++k)
		if (w[k] > w[max])
			max = k;

	w[max] += uint8_t(255 - sum);
}

StreamFormat writeVertexStream(eastl::string& bin, const Stream& stream, const QuantizationParams& params, const Settings& settings)
{
	if (stream.type == cgltf_attribute_type_position)
	{
		float pos_rscale = params.pos_scale == 0.f ? 0.f : 1.f / params.pos_scale;

		for (size_t i = 0; i < stream.data.size(); ++i)
		{
			const Attr& a = stream.data[i];

			uint16_t v[4] = {
				uint16_t(meshopt_quantizeUnorm((a.f[0] - params.pos_offset[0]) * pos_rscale, params.pos_bits)),
				uint16_t(meshopt_quantizeUnorm((a.f[1] - params.pos_offset[1]) * pos_rscale, params.pos_bits)),
				uint16_t(meshopt_quantizeUnorm((a.f[2] - params.pos_offset[2]) * pos_rscale, params.pos_bits)),
				1 };
			bin.append(reinterpret_cast<const char*>(v), sizeof(v));
		}

		// note: vec4 is used instead of vec3 to avoid three.js bug with interleaved buffers (#16802)
		StreamFormat format = { cgltf_type_vec4, cgltf_component_type_r_16u, false, 8 };
		return format;
	}
	else if (stream.type == cgltf_attribute_type_texcoord)
	{
		float uv_rscale[2] = {
			params.uv_scale[0] == 0.f ? 0.f : 1.f / params.uv_scale[0],
			params.uv_scale[1] == 0.f ? 0.f : 1.f / params.uv_scale[1],
		};

		for (size_t i = 0; i < stream.data.size(); ++i)
		{
			const Attr& a = stream.data[i];

			uint16_t v[2] = {
				uint16_t(meshopt_quantizeUnorm((a.f[0] - params.uv_offset[0]) * uv_rscale[0], params.uv_bits)),
				uint16_t(meshopt_quantizeUnorm((a.f[1] - params.uv_offset[1]) * uv_rscale[1], params.uv_bits)),
			};
			bin.append(reinterpret_cast<const char*>(v), sizeof(v));
		}

		StreamFormat format = { cgltf_type_vec2, cgltf_component_type_r_16u, false, 4 };
		return format;
	}
	else if (stream.type == cgltf_attribute_type_normal)
	{
		int bits = settings.nrm_unit ? 8 : settings.nrm_bits;

		for (size_t i = 0; i < stream.data.size(); ++i)
		{
			const Attr& a = stream.data[i];

			float nx = a.f[0], ny = a.f[1], nz = a.f[2];

			if (!settings.nrm_unit)
				rescaleNormal(nx, ny, nz);

			int8_t v[4] = {
				int8_t(meshopt_quantizeSnorm(nx, bits)),
				int8_t(meshopt_quantizeSnorm(ny, bits)),
				int8_t(meshopt_quantizeSnorm(nz, bits)),
				0 };
			bin.append(reinterpret_cast<const char*>(v), sizeof(v));
		}

		// note: vec4 is used instead of vec3 to avoid three.js bug with interleaved buffers (#16802)
		StreamFormat format = { cgltf_type_vec4, cgltf_component_type_r_8, true, 4 };
		return format;
	}
	else if (stream.type == cgltf_attribute_type_tangent)
	{
		int bits = settings.nrm_unit ? 8 : settings.nrm_bits;

		for (size_t i = 0; i < stream.data.size(); ++i)
		{
			const Attr& a = stream.data[i];

			float nx = a.f[0], ny = a.f[1], nz = a.f[2], nw = a.f[3];

			if (!settings.nrm_unit)
				rescaleNormal(nx, ny, nz);

			int8_t v[4] = {
				int8_t(meshopt_quantizeSnorm(nx, bits)),
				int8_t(meshopt_quantizeSnorm(ny, bits)),
				int8_t(meshopt_quantizeSnorm(nz, bits)),
				int8_t(meshopt_quantizeSnorm(nw, 8)) };
			bin.append(reinterpret_cast<const char*>(v), sizeof(v));
		}

		StreamFormat format = { cgltf_type_vec4, cgltf_component_type_r_8, true, 4 };
		return format;
	}
	else if (stream.type == cgltf_attribute_type_color)
	{
		for (size_t i = 0; i < stream.data.size(); ++i)
		{
			const Attr& a = stream.data[i];

			uint8_t v[4] = {
				uint8_t(meshopt_quantizeUnorm(a.f[0], 8)),
				uint8_t(meshopt_quantizeUnorm(a.f[1], 8)),
				uint8_t(meshopt_quantizeUnorm(a.f[2], 8)),
				uint8_t(meshopt_quantizeUnorm(a.f[3], 8)) };
			bin.append(reinterpret_cast<const char*>(v), sizeof(v));
		}

		StreamFormat format = { cgltf_type_vec4, cgltf_component_type_r_8u, true, 4 };
		return format;
	}
	else if (stream.type == cgltf_attribute_type_weights)
	{
		for (size_t i = 0; i < stream.data.size(); ++i)
		{
			const Attr& a = stream.data[i];

			uint8_t v[4] = {
				uint8_t(meshopt_quantizeUnorm(a.f[0], 8)),
				uint8_t(meshopt_quantizeUnorm(a.f[1], 8)),
				uint8_t(meshopt_quantizeUnorm(a.f[2], 8)),
				uint8_t(meshopt_quantizeUnorm(a.f[3], 8)) };

			renormalizeWeights(v);

			bin.append(reinterpret_cast<const char*>(v), sizeof(v));
		}

		StreamFormat format = { cgltf_type_vec4, cgltf_component_type_r_8u, true, 4 };
		return format;
	}
	else if (stream.type == cgltf_attribute_type_joints)
	{
		unsigned int maxj = 0;

		for (size_t i = 0; i < stream.data.size(); ++i)
			maxj = eastl::max(maxj, unsigned(stream.data[i].f[0]));

		ASSERT(maxj <= 65535);

		if (maxj <= 255)
		{
			for (size_t i = 0; i < stream.data.size(); ++i)
			{
				const Attr& a = stream.data[i];

				uint8_t v[4] = {
					uint8_t(a.f[0]),
					uint8_t(a.f[1]),
					uint8_t(a.f[2]),
					uint8_t(a.f[3]) };
				bin.append(reinterpret_cast<const char*>(v), sizeof(v));
			}

			StreamFormat format = { cgltf_type_vec4, cgltf_component_type_r_8u, false, 4 };
			return format;
		}
		else
		{
			for (size_t i = 0; i < stream.data.size(); ++i)
			{
				const Attr& a = stream.data[i];

				uint16_t v[4] = {
					uint16_t(a.f[0]),
					uint16_t(a.f[1]),
					uint16_t(a.f[2]),
					uint16_t(a.f[3]) };
				bin.append(reinterpret_cast<const char*>(v), sizeof(v));
			}

			StreamFormat format = { cgltf_type_vec4, cgltf_component_type_r_16u, false, 8 };
			return format;
		}
	}
	else
	{
		for (size_t i = 0; i < stream.data.size(); ++i)
		{
			const Attr& a = stream.data[i];

			float v[4] = {
				a.f[0],
				a.f[1],
				a.f[2],
				a.f[3] };
			bin.append(reinterpret_cast<const char*>(v), sizeof(v));
		}

		StreamFormat format = { cgltf_type_vec4, cgltf_component_type_r_32f, false, 16 };
		return format;
	}
}

void getPositionBounds(uint16_t min[3], uint16_t max[3], const Stream& stream, const QuantizationParams& params)
{
	ASSERT(stream.type == cgltf_attribute_type_position);
	ASSERT(stream.data.size() > 0);

	min[0] = min[1] = min[2] = 0xffff;
	max[0] = max[1] = max[2] = 0;

	float pos_rscale = params.pos_scale == 0.f ? 0.f : 1.f / params.pos_scale;

	for (size_t i = 0; i < stream.data.size(); ++i)
	{
		const Attr& a = stream.data[i];

		for (int k = 0; k < 3; ++k)
		{
			uint16_t v = uint16_t(meshopt_quantizeUnorm((a.f[k] - params.pos_offset[k]) * pos_rscale, params.pos_bits));

			min[k] = eastl::min(min[k], v);
			max[k] = eastl::max(max[k], v);
		}
	}
}

StreamFormat writeIndexStream(eastl::string& bin, const eastl::vector<unsigned int>& stream)
{
	unsigned int maxi = 0;
	for (size_t i = 0; i < stream.size(); ++i)
		maxi = eastl::max(maxi, stream[i]);

	// save 16-bit indices if we can; note that we can't use restart index (65535)
	if (maxi < 65535)
	{
		for (size_t i = 0; i < stream.size(); ++i)
		{
			uint16_t v[1] = { uint16_t(stream[i]) };
			bin.append(reinterpret_cast<const char*>(v), sizeof(v));
		}

		StreamFormat format = { cgltf_type_scalar, cgltf_component_type_r_16u, false, 2 };
		return format;
	}
	else
	{
		for (size_t i = 0; i < stream.size(); ++i)
		{
			uint32_t v[1] = { stream[i] };
			bin.append(reinterpret_cast<const char*>(v), sizeof(v));
		}

		StreamFormat format = { cgltf_type_scalar, cgltf_component_type_r_32u, false, 4 };
		return format;
	}
}

StreamFormat writeTimeStream(eastl::string& bin, const eastl::vector<float>& data)
{
	for (size_t i = 0; i < data.size(); ++i)
	{
		float v[1] = { data[i] };
		bin.append(reinterpret_cast<const char*>(v), sizeof(v));
	}

	StreamFormat format = { cgltf_type_scalar, cgltf_component_type_r_32f, false, 4 };
	return format;
}

StreamFormat writeKeyframeStream(eastl::string& bin, cgltf_animation_path_type type, const eastl::vector<Attr>& data)
{
	if (type == cgltf_animation_path_type_rotation)
	{
		for (size_t i = 0; i < data.size(); ++i)
		{
			const Attr& a = data[i];

			int16_t v[4] = {
				int16_t(meshopt_quantizeSnorm(a.f[0], 16)),
				int16_t(meshopt_quantizeSnorm(a.f[1], 16)),
				int16_t(meshopt_quantizeSnorm(a.f[2], 16)),
				int16_t(meshopt_quantizeSnorm(a.f[3], 16)),
			};
			bin.append(reinterpret_cast<const char*>(v), sizeof(v));
		}

		StreamFormat format = { cgltf_type_vec4, cgltf_component_type_r_16, true, 8 };
		return format;
	}
	else
	{
		for (size_t i = 0; i < data.size(); ++i)
		{
			const Attr& a = data[i];

			float v[3] = { a.f[0], a.f[1], a.f[2] };
			bin.append(reinterpret_cast<const char*>(v), sizeof(v));
		}

		StreamFormat format = { cgltf_type_vec3, cgltf_component_type_r_32f, false, 12 };
		return format;
	}
}

void compressVertexStream(eastl::string& bin, size_t offset, size_t count, size_t stride)
{
	eastl::vector<unsigned char> compressed(meshopt_encodeVertexBufferBound(count, stride));
	size_t size = meshopt_encodeVertexBuffer(&compressed[0], compressed.size(), bin.c_str() + offset, count, stride);

	bin.erase(offset);
	bin.append(reinterpret_cast<const char*>(&compressed[0]), size);
}

void compressIndexStream(eastl::string& bin, size_t offset, size_t count, size_t stride)
{
	ASSERT(stride == 2 || stride == 4);

	eastl::vector<unsigned char> compressed(meshopt_encodeIndexBufferBound(count, count * 3));
	size_t size = 0;

	if (stride == 2)
		size = meshopt_encodeIndexBuffer(&compressed[0], compressed.size(), reinterpret_cast<const uint16_t*>(bin.c_str() + offset), count);
	else
		size = meshopt_encodeIndexBuffer(&compressed[0], compressed.size(), reinterpret_cast<const uint32_t*>(bin.c_str() + offset), count);

	bin.erase(offset);
	bin.append(reinterpret_cast<const char*>(&compressed[0]), size);
}

void comma(eastl::string& s)
{
	char ch = s.empty() ? 0 : s[s.size() - 1];

	if (ch != 0 && ch != '[' && ch != '{')
		s += ',';
}

eastl::string to_string(size_t v)
{
	char buf[32];
	sprintf(buf, "%zu", v);
	return buf;
}

eastl::string to_string(float v)
{
	char buf[512];
	sprintf(buf, "%.9g", v);
	return buf;
}

const char* componentType(cgltf_component_type type)
{
	switch (type)
	{
	case cgltf_component_type_r_8:
		return "5120";
	case cgltf_component_type_r_8u:
		return "5121";
	case cgltf_component_type_r_16:
		return "5122";
	case cgltf_component_type_r_16u:
		return "5123";
	case cgltf_component_type_r_32u:
		return "5125";
	case cgltf_component_type_r_32f:
		return "5126";
	default:
		return "0";
	}
}

const char* shapeType(cgltf_type type)
{
	switch (type)
	{
	case cgltf_type_scalar:
		return "\"SCALAR\"";
	case cgltf_type_vec2:
		return "\"VEC2\"";
	case cgltf_type_vec3:
		return "\"VEC3\"";
	case cgltf_type_vec4:
		return "\"VEC4\"";
	case cgltf_type_mat2:
		return "\"MAT2\"";
	case cgltf_type_mat3:
		return "\"MAT3\"";
	case cgltf_type_mat4:
		return "\"MAT4\"";
	default:
		return "\"\"";
	}
}

const char* attributeType(cgltf_attribute_type type)
{
	switch (type)
	{
	case cgltf_attribute_type_position:
		return "POSITION";
	case cgltf_attribute_type_normal:
		return "NORMAL";
	case cgltf_attribute_type_tangent:
		return "TANGENT";
	case cgltf_attribute_type_texcoord:
		return "TEXCOORD";
	case cgltf_attribute_type_color:
		return "COLOR";
	case cgltf_attribute_type_joints:
		return "JOINTS";
	case cgltf_attribute_type_weights:
		return "WEIGHTS";
	default:
		return "ATTRIBUTE";
	}
}

const char* animationPath(cgltf_animation_path_type type)
{
	switch (type)
	{
	case cgltf_animation_path_type_translation:
		return "\"translation\"";
	case cgltf_animation_path_type_rotation:
		return "\"rotation\"";
	case cgltf_animation_path_type_scale:
		return "\"scale\"";
	default:
		return "\"\"";
	}
}

void writeTextureInfo(eastl::string& json, const cgltf_data* data, const cgltf_texture_view& view, const QuantizationParams& qp)
{
	ASSERT(view.texture);

	json += "{\"index\":";
	json += to_string(size_t(view.texture - data->textures));
	json += ",\"texCoord\":";
	json += to_string(size_t(view.texcoord));
	json += ",\"extensions\":{\"KHR_texture_transform\":{";
	json += "\"offset\":[";
	json += to_string(qp.uv_offset[0]);
	json += ",";
	json += to_string(qp.uv_offset[1]);
	json += "],\"scale\":[";
	json += to_string(qp.uv_scale[0] / float((1 << qp.uv_bits) - 1));
	json += ",";
	json += to_string(qp.uv_scale[1] / float((1 << qp.uv_bits) - 1));
	json += "]}}}";
}

void writeMaterialInfo(eastl::string& json, const cgltf_data* data, const cgltf_material& material, const QuantizationParams& qp)
{
	static const float white[4] = { 1, 1, 1, 1 };
	static const float black[4] = { 0, 0, 0, 0 };

	if (material.has_pbr_metallic_roughness)
	{
		const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;

		comma(json);
		json += "\"pbrMetallicRoughness\":{";
		if (memcmp(pbr.base_color_factor, white, 16) != 0)
		{
			comma(json);
			json += "\"baseColorFactor\":[";
			json += to_string(pbr.base_color_factor[0]);
			json += ",";
			json += to_string(pbr.base_color_factor[1]);
			json += ",";
			json += to_string(pbr.base_color_factor[2]);
			json += ",";
			json += to_string(pbr.base_color_factor[3]);
			json += "]";
		}
		if (pbr.base_color_texture.texture)
		{
			comma(json);
			json += "\"baseColorTexture\":";
			writeTextureInfo(json, data, pbr.base_color_texture, qp);
		}
		if (pbr.metallic_factor != 1)
		{
			comma(json);
			json += "\"metallicFactor\":";
			json += to_string(pbr.metallic_factor);
		}
		if (pbr.roughness_factor != 1)
		{
			comma(json);
			json += "\"roughnessFactor\":";
			json += to_string(pbr.roughness_factor);
		}
		if (pbr.metallic_roughness_texture.texture)
		{
			comma(json);
			json += "\"metallicRoughnessTexture\":";
			writeTextureInfo(json, data, pbr.metallic_roughness_texture, qp);
		}
		json += "}";
	}

	if (material.normal_texture.texture)
	{
		comma(json);
		json += "\"normalTexture\":";
		writeTextureInfo(json, data, material.normal_texture, qp);
	}

	if (material.occlusion_texture.texture)
	{
		comma(json);
		json += "\"occlusionTexture\":";
		writeTextureInfo(json, data, material.occlusion_texture, qp);
	}

	if (material.emissive_texture.texture)
	{
		comma(json);
		json += "\"emissiveTexture\":";
		writeTextureInfo(json, data, material.emissive_texture, qp);
	}

	if (memcmp(material.emissive_factor, black, 12) != 0)
	{
		comma(json);
		json += "\"emissiveFactor\":[";
		json += to_string(material.emissive_factor[0]);
		json += ",";
		json += to_string(material.emissive_factor[1]);
		json += ",";
		json += to_string(material.emissive_factor[2]);
		json += "]";
	}

	if (material.alpha_mode != cgltf_alpha_mode_opaque)
	{
		comma(json);
		json += "\"alphaMode\":";
		json += (material.alpha_mode == cgltf_alpha_mode_blend) ? "\"BLEND\"" : "\"MASK\"";
	}

	if (material.alpha_cutoff != 0.5f)
	{
		comma(json);
		json += "\"alphaCutoff\":";
		json += to_string(material.alpha_cutoff);
	}

	if (material.double_sided)
	{
		comma(json);
		json += "\"doubleSided\":true";
	}

	if (material.has_pbr_specular_glossiness || material.unlit)
	{
		comma(json);
		json += "\"extensions\":{";

		if (material.has_pbr_specular_glossiness)
		{
			const cgltf_pbr_specular_glossiness& pbr = material.pbr_specular_glossiness;

			comma(json);
			json += "\"KHR_materials_pbrSpecularGlossiness\":{";
			if (pbr.diffuse_texture.texture)
			{
				comma(json);
				json += "\"diffuseTexture\":";
				writeTextureInfo(json, data, pbr.diffuse_texture, qp);
			}
			if (pbr.specular_glossiness_texture.texture)
			{
				comma(json);
				json += "\"specularGlossinessTexture\":";
				writeTextureInfo(json, data, pbr.specular_glossiness_texture, qp);
			}
			if (memcmp(pbr.diffuse_factor, white, 16) != 0)
			{
				comma(json);
				json += "\"diffuseFactor\":[";
				json += to_string(pbr.diffuse_factor[0]);
				json += ",";
				json += to_string(pbr.diffuse_factor[1]);
				json += ",";
				json += to_string(pbr.diffuse_factor[2]);
				json += ",";
				json += to_string(pbr.diffuse_factor[3]);
				json += "]";
			}
			if (memcmp(pbr.specular_factor, white, 12) != 0)
			{
				comma(json);
				json += "\"specularFactor\":[";
				json += to_string(pbr.specular_factor[0]);
				json += ",";
				json += to_string(pbr.specular_factor[1]);
				json += ",";
				json += to_string(pbr.specular_factor[2]);
				json += "]";
			}
			if (pbr.glossiness_factor != 1)
			{
				comma(json);
				json += "\"glossinessFactor\":";
				json += to_string(pbr.glossiness_factor);
			}
			json += "}";
		}
		if (material.unlit)
		{
			comma(json);
			json += "\"KHR_materials_unlit\":{}";
		}

		json += "}";
	}
}

bool usesTextureSet(const cgltf_material& material, int set)
{
	if (material.has_pbr_metallic_roughness)
	{
		const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;

		if (pbr.base_color_texture.texture && pbr.base_color_texture.texcoord == set)
			return true;

		if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texcoord == set)
			return true;
	}

	if (material.has_pbr_specular_glossiness)
	{
		const cgltf_pbr_specular_glossiness& pbr = material.pbr_specular_glossiness;

		if (pbr.diffuse_texture.texture && pbr.diffuse_texture.texcoord == set)
			return true;

		if (pbr.specular_glossiness_texture.texture && pbr.specular_glossiness_texture.texcoord == set)
			return true;
	}

	if (material.normal_texture.texture && material.normal_texture.texcoord == set)
		return true;

	if (material.occlusion_texture.texture && material.occlusion_texture.texcoord == set)
		return true;

	if (material.emissive_texture.texture && material.emissive_texture.texcoord == set)
		return true;

	return false;
}

size_t getBufferView(eastl::vector<BufferView>& views, BufferView::Kind kind, int variant, size_t stride, bool compressed)
{
	if (variant >= 0)
	{
		for (size_t i = 0; i < views.size(); ++i)
			if (views[i].kind == kind && views[i].variant == variant && views[i].stride == stride && views[i].compressed == compressed)
				return i;
	}

	BufferView view = { kind, variant, stride, compressed };
	views.push_back(view);

	return views.size() - 1;
}

void writeBufferView(eastl::string& json, BufferView::Kind kind, size_t count, size_t stride, size_t bin_offset, size_t bin_size, bool compressed)
{
	json += "{\"buffer\":0";
	json += ",\"byteLength\":";
	json += to_string(bin_size);
	json += ",\"byteOffset\":";
	json += to_string(bin_offset);
	if (kind == BufferView::Kind_Vertex)
	{
		json += ",\"byteStride\":";
		json += to_string(stride);
	}
	if (kind == BufferView::Kind_Vertex || kind == BufferView::Kind_Index)
	{
		json += ",\"target\":";
		json += (kind == BufferView::Kind_Vertex) ? "34962" : "34963";
	}
	if (compressed)
	{
		json += ",\"extensions\":{";
		json += "\"KHR_meshopt_compression\":{";
		json += "\"count\":";
		json += to_string(count);
		json += ",\"byteStride\":";
		json += to_string(stride);
		json += "}}";
	}
	json += "}";
}

void writeAccessor(eastl::string& json, size_t view, size_t offset, cgltf_type type, cgltf_component_type component_type, bool normalized, size_t count, const float* min = 0, const float* max = 0, size_t numminmax = 0)
{
	json += "{\"bufferView\":";
	json += to_string(view);
	json += ",\"byteOffset\":";
	json += to_string(offset);
	json += ",\"componentType\":";
	json += componentType(component_type);
	json += ",\"count\":";
	json += to_string(count);
	json += ",\"type\":";
	json += shapeType(type);

	if (normalized)
	{
		json += ",\"normalized\":true";
	}

	if (min && max)
	{
		ASSERT(numminmax);

		json += ",\"min\":[";
		for (size_t k = 0; k < numminmax; ++k)
		{
			comma(json);
			json += to_string(min[k]);
		}
		json += "],\"max\":[";
		for (size_t k = 0; k < numminmax; ++k)
		{
			comma(json);
			json += to_string(max[k]);
		}
		json += "]";
	}

	json += "}";
}

float getDelta(const Attr& l, const Attr& r, cgltf_animation_path_type type)
{
	if (type == cgltf_animation_path_type_rotation)
	{
		float error = 1.f - fabsf(l.f[0] * r.f[0] + l.f[1] * r.f[1] + l.f[2] * r.f[2] + l.f[3] * r.f[3]);

		return error;
	}
	else
	{
		float error = 0;
		for (int k = 0; k < 4; ++k)
			error += fabsf(r.f[k] - l.f[k]);

		return error;
	}
}

bool isTrackConstant(const cgltf_animation_sampler& sampler, cgltf_animation_path_type type)
{
	const float tolerance = 1e-3f;

	Attr first = {};
	cgltf_accessor_read_float(sampler.output, 0, first.f, 4);

	for (size_t i = 1; i < sampler.output->count; ++i)
	{
		Attr attr = {};
		cgltf_accessor_read_float(sampler.output, i, attr.f, 4);

		if (getDelta(first, attr, type) > tolerance)
			return false;
	}

	return true;
}

Attr interpolateLinear(const Attr& l, const Attr& r, float t, cgltf_animation_path_type type)
{
	if (type == cgltf_animation_path_type_rotation)
	{
		// Approximating slerp, https://zeux.io/2015/07/23/approximating-slerp/
		// We also handle quaternion double-cover
		float ca = l.f[0] * r.f[0] + l.f[1] * r.f[1] + l.f[2] * r.f[2] + l.f[3] * r.f[3];

		float d = fabsf(ca);
		float A = 1.0904f + d * (-3.2452f + d * (3.55645f - d * 1.43519f));
		float B = 0.848013f + d * (-1.06021f + d * 0.215638f);
		float k = A * (t - 0.5f) * (t - 0.5f) + B;
		float ot = t + t * (t - 0.5f) * (t - 1) * k;

		float t0 = 1 - ot;
		float t1 = ca > 0 ? ot : -ot;

		Attr lerp = { {
			l.f[0] * t0 + r.f[0] * t1,
			l.f[1] * t0 + r.f[1] * t1,
			l.f[2] * t0 + r.f[2] * t1,
			l.f[3] * t0 + r.f[3] * t1,
		} };

		float len = sqrtf(lerp.f[0] * lerp.f[0] + lerp.f[1] * lerp.f[1] + lerp.f[2] * lerp.f[2] + lerp.f[3] * lerp.f[3]);

		if (len > 0.f)
		{
			lerp.f[0] /= len;
			lerp.f[1] /= len;
			lerp.f[2] /= len;
			lerp.f[3] /= len;
		}

		return lerp;
	}
	else
	{
		Attr lerp = { {
			l.f[0] * (1 - t) + r.f[0] * t,
			l.f[1] * (1 - t) + r.f[1] * t,
			l.f[2] * (1 - t) + r.f[2] * t,
			l.f[3] * (1 - t) + r.f[3] * t,
		} };

		return lerp;
	}
}

Attr interpolateHermite(const Attr& v0, const Attr& t0, const Attr& v1, const Attr& t1, float t, float dt, cgltf_animation_path_type type)
{
	float s0 = 1 + t * t * (2 * t - 3);
	float s1 = t + t * t * (t - 2);
	float s2 = 1 - s0;
	float s3 = t * t * (t - 1);

	float ts1 = dt * s1;
	float ts3 = dt * s3;

	Attr lerp = { {
		s0 * v0.f[0] + ts1 * t0.f[0] + s2 * v1.f[0] + ts3 * t1.f[0],
		s0 * v0.f[1] + ts1 * t0.f[1] + s2 * v1.f[1] + ts3 * t1.f[1],
		s0 * v0.f[2] + ts1 * t0.f[2] + s2 * v1.f[2] + ts3 * t1.f[2],
		s0 * v0.f[3] + ts1 * t0.f[3] + s2 * v1.f[3] + ts3 * t1.f[3],
	} };

	if (type == cgltf_animation_path_type_rotation)
	{
		float len = sqrtf(lerp.f[0] * lerp.f[0] + lerp.f[1] * lerp.f[1] + lerp.f[2] * lerp.f[2] + lerp.f[3] * lerp.f[3]);

		if (len > 0.f)
		{
			lerp.f[0] /= len;
			lerp.f[1] /= len;
			lerp.f[2] /= len;
			lerp.f[3] /= len;
		}
	}

	return lerp;
}

void resampleKeyframes(eastl::vector<Attr>& data, const cgltf_animation_sampler& sampler, cgltf_animation_path_type type, int frames, float mint, int freq)
{
	size_t cursor = 0;

	for (int i = 0; i < frames; ++i)
	{
		float time = mint + float(i) / freq;

		while (cursor + 1 < sampler.input->count)
		{
			float next_time = 0;
			cgltf_accessor_read_float(sampler.input, cursor + 1, &next_time, 1);

			if (next_time > time)
				break;

			cursor++;
		}

		if (cursor + 1 < sampler.input->count)
		{
			float cursor_time = 0;
			float next_time = 0;
			cgltf_accessor_read_float(sampler.input, cursor + 0, &cursor_time, 1);
			cgltf_accessor_read_float(sampler.input, cursor + 1, &next_time, 1);

			float range = next_time - cursor_time;
			float inv_range = (range == 0.f) ? 0.f : 1.f / (next_time - cursor_time);
			float t = eastl::max(0.f, eastl::min(1.f, (time - cursor_time) * inv_range));

			switch (sampler.interpolation)
			{
			case cgltf_interpolation_type_linear:
			{
				Attr v0 = {};
				Attr v1 = {};
				cgltf_accessor_read_float(sampler.output, cursor + 0, v0.f, 4);
				cgltf_accessor_read_float(sampler.output, cursor + 1, v1.f, 4);
				data.push_back(interpolateLinear(v0, v1, t, type));
			}
			break;

			case cgltf_interpolation_type_step:
			{
				Attr v = {};
				cgltf_accessor_read_float(sampler.output, cursor, v.f, 4);
				data.push_back(v);
			}
			break;

			case cgltf_interpolation_type_cubic_spline:
			{
				Attr v0 = {};
				Attr b0 = {};
				Attr a1 = {};
				Attr v1 = {};
				cgltf_accessor_read_float(sampler.output, cursor * 3 + 1, v0.f, 4);
				cgltf_accessor_read_float(sampler.output, cursor * 3 + 2, b0.f, 4);
				cgltf_accessor_read_float(sampler.output, cursor * 3 + 3, a1.f, 4);
				cgltf_accessor_read_float(sampler.output, cursor * 3 + 4, v1.f, 4);
				data.push_back(interpolateHermite(v0, b0, v1, a1, t, range, type));
			}
			break;

			default:
				ASSERT(!"Unknown interpolation type");
			}
		}
		else
		{
			size_t offset = sampler.interpolation == cgltf_interpolation_type_cubic_spline ? cursor * 3 + 1 : cursor;

			Attr v = {};
			cgltf_accessor_read_float(sampler.output, offset, v.f, 4);
			data.push_back(v);
		}
	}
}

void markAnimated(cgltf_data* data, eastl::vector<NodeInfo>& nodes)
{
	for (size_t i = 0; i < data->animations_count; ++i)
	{
		const cgltf_animation& animation = data->animations[i];

		for (size_t j = 0; j < animation.channels_count; ++j)
		{
			const cgltf_animation_channel& channel = animation.channels[j];
			const cgltf_animation_sampler& sampler = *channel.sampler;

			if (!channel.target_node)
				continue;

			NodeInfo& ni = nodes[channel.target_node - data->nodes];

			if (channel.target_path != cgltf_animation_path_type_translation && channel.target_path != cgltf_animation_path_type_rotation && channel.target_path != cgltf_animation_path_type_scale)
				continue;

			// mark nodes that have animation tracks that change their base transform as animated
			if (!isTrackConstant(sampler, channel.target_path))
			{
				ni.animated_paths |= (1 << channel.target_path);
			}
			else
			{
				Attr base = {};

				switch (channel.target_path)
				{
				case cgltf_animation_path_type_translation:
					memcpy(base.f, channel.target_node->translation, 3 * sizeof(float));
					break;
				case cgltf_animation_path_type_rotation:
					memcpy(base.f, channel.target_node->rotation, 4 * sizeof(float));
					break;
				case cgltf_animation_path_type_scale:
					memcpy(base.f, channel.target_node->scale, 3 * sizeof(float));
					break;
				default:;
				}

				Attr first = {};
				cgltf_accessor_read_float(sampler.output, 0, first.f, 4);

				const float tolerance = 1e-3f;

				if (getDelta(base, first, channel.target_path) > tolerance)
				{
					ni.animated_paths |= (1 << channel.target_path);
				}
			}
		}
	}

	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		NodeInfo& ni = nodes[i];

		for (cgltf_node* node = &data->nodes[i]; node; node = node->parent)
			ni.animated |= nodes[node - data->nodes].animated_paths != 0;
	}
}

void markNeeded(cgltf_data* data, eastl::vector<NodeInfo>& nodes)
{
	// mark all joints as kept & named (names might be important to manipulate externally)
	for (size_t i = 0; i < data->skins_count; ++i)
	{
		const cgltf_skin& skin = data->skins[i];

		// for now we keep all joints directly referenced by the skin and the entire ancestry tree; we keep names for joints as well
		for (size_t j = 0; j < skin.joints_count; ++j)
		{
			NodeInfo& ni = nodes[skin.joints[j] - data->nodes];

			ni.keep = true;
			ni.named = true;
		}
	}

	// mark all animated nodes as kept
	for (size_t i = 0; i < data->animations_count; ++i)
	{
		const cgltf_animation& animation = data->animations[i];

		for (size_t j = 0; j < animation.channels_count; ++j)
		{
			const cgltf_animation_channel& channel = animation.channels[j];

			if (channel.target_node)
			{
				NodeInfo& ni = nodes[channel.target_node - data->nodes];

				ni.keep = true;
			}
		}
	}

	// mark all animated mesh nodes as kept
	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		if (data->nodes[i].mesh && !data->nodes[i].skin && nodes[i].animated)
		{
			nodes[i].keep = true;
		}
	}
}

void remapNodes(cgltf_data* data, eastl::vector<NodeInfo>& nodes, size_t& node_offset)
{
	// to keep a node, we currently need to keep the entire ancestry chain
	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		if (!nodes[i].keep)
			continue;

		for (cgltf_node* node = &data->nodes[i]; node; node = node->parent)
			nodes[node - data->nodes].keep = true;
	}

	// generate sequential indices for all nodes; they aren't sorted topologically
	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		NodeInfo& ni = nodes[i];

		if (ni.keep)
		{
			ni.remap = int(node_offset);

			node_offset++;
		}
	}
}

bool parseDataUri(const char* uri, eastl::string& mime_type, eastl::string& result)
{
	if (strncmp(uri, "data:", 5) == 0)
	{
		const char* comma = strchr(uri, ',');

		if (comma && comma - uri >= 7 && strncmp(comma - 7, ";base64", 7) == 0)
		{
			const char* base64 = comma + 1;
			size_t base64_size = strlen(base64);
			size_t size = base64_size - base64_size / 4;

			if (base64_size >= 2)
			{
				size -= base64[base64_size - 2] == '=';
				size -= base64[base64_size - 1] == '=';
			}

			void* data = 0;

			cgltf_options options = {};
			cgltf_result res = cgltf_load_buffer_base64(&options, size, base64, &data);

			if (res != cgltf_result_success)
				return false;

			mime_type = eastl::string(uri + 5, comma - 7);
			result = eastl::string(static_cast<const char*>(data), size);

			conf_free(data);

			return true;
		}
	}

	return false;
}

void writeEmbeddedImage(eastl::string& json, eastl::vector<BufferView>& views, const char* data, size_t size, const char* mime_type)
{
	size_t view = getBufferView(views, BufferView::Kind_Image, -1, 1, false);

	ASSERT(views[view].data.empty());
	views[view].data.append(data, size);

	// each chunk must be aligned to 4 bytes
	views[view].data.resize((views[view].data.size() + 3) & ~3);

	json += "\"bufferView\":";
	json += to_string(view);
	json += ",\"mimeType\":\"";
	json += mime_type;
	json += "\"";
}

bool process(cgltf_data* data, eastl::vector<Mesh>& meshes, const Settings& settings, eastl::string& json, eastl::string& bin)
{
	if (settings.verbose)
	{
		printf("input: %d nodes, %d meshes, %d skins, %d animations\n",
			int(data->nodes_count), int(data->meshes_count), int(data->skins_count), int(data->animations_count));
	}

	eastl::vector<NodeInfo> nodes(data->nodes_count);

	markAnimated(data, nodes);
	markNeeded(data, nodes);

	for (size_t i = 0; i < meshes.size(); ++i)
	{
		Mesh& mesh = meshes[i];

		if (mesh.node)
		{
			NodeInfo& ni = nodes[mesh.node - data->nodes];

			// skinned meshes don't use the node transform
			if (mesh.skin)
			{
				mesh.node = 0;
			}
			// we transform all non-animated meshes to world space
			// this makes sure that quantization doesn't introduce gaps if the original scene was watertight
			else if (!ni.animated)
			{
				transformMesh(mesh, mesh.node);
				mesh.node = 0;
			}
		}
	}

	mergeMeshes(meshes);

	for (size_t i = 0; i < meshes.size(); ++i)
	{
		Mesh& mesh = meshes[i];

		if (mesh.indices.empty())
			continue;

		reindexMesh(mesh);
		optimizeMesh(mesh);
	}

	if (settings.verbose)
	{
		size_t triangles = 0;
		size_t vertices = 0;

		for (size_t i = 0; i < meshes.size(); ++i)
		{
			const Mesh& mesh = meshes[i];

			triangles += mesh.indices.size() / 3;
			vertices += mesh.streams.empty() ? 0 : mesh.streams[0].data.size();
		}

		printf("meshes: %d triangles, %d vertices\n", int(triangles), int(vertices));
	}

	QuantizationParams qp = prepareQuantization(meshes, settings);

	eastl::string json_images;
	eastl::string json_textures;
	eastl::string json_materials;
	eastl::string json_accessors;
	eastl::string json_meshes;
	eastl::string json_nodes;
	eastl::string json_skins;
	eastl::string json_roots;
	eastl::string json_animations;

	eastl::vector<BufferView> views;
	eastl::string scratch;

	bool has_pbr_specular_glossiness = false;

	size_t accr_offset = 0;
	size_t node_offset = 0;
	size_t mesh_offset = 0;

	for (size_t i = 0; i < data->images_count; ++i)
	{
		const cgltf_image& image = data->images[i];

		comma(json_images);
		json_images += "{";
		if (image.uri)
		{
			eastl::string mime_type;
			eastl::string img;

			if (parseDataUri(image.uri, mime_type, img))
			{
				writeEmbeddedImage(json_images, views, img.c_str(), img.size(), mime_type.c_str());
			}
			else
			{
				json_images += "\"uri\":\"";
				json_images += image.uri;
				json_images += "\"";
			}
		}
		else if (image.buffer_view && image.buffer_view->buffer->data && image.mime_type)
		{
			const char* img = static_cast<const char*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
			size_t size = image.buffer_view->size;

			writeEmbeddedImage(json_images, views, img, size, image.mime_type);
		}

		json_images += "}";
	}

	for (size_t i = 0; i < data->textures_count; ++i)
	{
		const cgltf_texture& texture = data->textures[i];

		comma(json_textures);
		json_textures += "{";
		if (texture.image)
		{
			json_textures += "\"source\":";
			json_textures += to_string(size_t(texture.image - data->images));
		}
		json_textures += "}";
	}

	for (size_t i = 0; i < data->materials_count; ++i)
	{
		const cgltf_material& material = data->materials[i];

		comma(json_materials);
		json_materials += "{";
		writeMaterialInfo(json_materials, data, material, qp);
		json_materials += "}";

		has_pbr_specular_glossiness = has_pbr_specular_glossiness || material.has_pbr_specular_glossiness;
	}

	eastl::vector<int> node_mesh(nodes.size(), -1);

	for (size_t i = 0; i < meshes.size(); ++i)
	{
		const Mesh& mesh = meshes[i];

		if (mesh.indices.empty())
			continue;

		eastl::string json_attributes;

		for (size_t j = 0; j < mesh.streams.size(); ++j)
		{
			const Stream& stream = mesh.streams[j];

			if (stream.type == cgltf_attribute_type_texcoord && (!mesh.material || !usesTextureSet(*mesh.material, stream.index)))
				continue;

			if ((stream.type == cgltf_attribute_type_joints || stream.type == cgltf_attribute_type_weights) && !mesh.skin)
				continue;

			scratch.clear();
			StreamFormat format = writeVertexStream(scratch, stream, qp, settings);

			size_t view = getBufferView(views, BufferView::Kind_Vertex, 0, format.stride, settings.compress);
			size_t offset = views[view].data.size();
			views[view].data += scratch;

			comma(json_accessors);
			if (stream.type == cgltf_attribute_type_position)
			{
				uint16_t min[3] = {};
				uint16_t max[3] = {};
				getPositionBounds(min, max, stream, qp);

				// note: vec4 is used instead of vec3 to avoid three.js bug with interleaved buffers (#16802)
				float minf[4] = { float(min[0]), float(min[1]), float(min[2]), 1 };
				float maxf[4] = { float(max[0]), float(max[1]), float(max[2]), 1 };

				writeAccessor(json_accessors, view, offset, format.type, format.component_type, format.normalized, stream.data.size(), minf, maxf, 4);
			}
			else
			{
				writeAccessor(json_accessors, view, offset, format.type, format.component_type, format.normalized, stream.data.size());
			}

			size_t vertex_accr = accr_offset++;

			comma(json_attributes);
			json_attributes += "\"";
			json_attributes += attributeType(stream.type);
			if (stream.type != cgltf_attribute_type_position && stream.type != cgltf_attribute_type_normal && stream.type != cgltf_attribute_type_tangent)
			{
				json_attributes += "_";
				json_attributes += to_string(size_t(stream.index));
			}
			json_attributes += "\":";
			json_attributes += to_string(vertex_accr);
		}

		size_t index_accr = 0;

		{
			scratch.clear();
			StreamFormat format = writeIndexStream(scratch, mesh.indices);

			// TODO: ideally variant would be 0 but this hurts index compression
			size_t view = getBufferView(views, BufferView::Kind_Index, -1, format.stride, settings.compress);
			size_t offset = views[view].data.size();
			views[view].data += scratch;

			comma(json_accessors);
			writeAccessor(json_accessors, view, offset, format.type, format.component_type, format.normalized, mesh.indices.size());

			index_accr = accr_offset++;
		}

		comma(json_meshes);
		json_meshes += "{\"primitives\":[{\"attributes\":{";
		json_meshes += json_attributes;
		json_meshes += "},\"indices\":";
		json_meshes += to_string(index_accr);
		if (mesh.material)
		{
			json_meshes += ",\"material\":";
			json_meshes += to_string(size_t(mesh.material - data->materials));
		}
		json_meshes += "}]}";

		float node_scale = qp.pos_scale / float((1 << qp.pos_bits) - 1);

		comma(json_nodes);
		json_nodes += "{\"mesh\":";
		json_nodes += to_string(mesh_offset);
		if (mesh.skin)
		{
			comma(json_nodes);
			json_nodes += "\"skin\":";
			json_nodes += to_string(size_t(mesh.skin - data->skins));
		}
		json_nodes += ",\"translation\":[";
		json_nodes += to_string(qp.pos_offset[0]);
		json_nodes += ",";
		json_nodes += to_string(qp.pos_offset[1]);
		json_nodes += ",";
		json_nodes += to_string(qp.pos_offset[2]);
		json_nodes += "],\"scale\":[";
		json_nodes += to_string(node_scale);
		json_nodes += ",";
		json_nodes += to_string(node_scale);
		json_nodes += ",";
		json_nodes += to_string(node_scale);
		json_nodes += "]";
		json_nodes += "}";

		if (mesh.node)
		{
			ASSERT(nodes[mesh.node - data->nodes].keep);
			node_mesh[mesh.node - data->nodes] = int(node_offset);
		}
		else
		{
			comma(json_roots);
			json_roots += to_string(node_offset);
		}

		node_offset++;
		mesh_offset++;
	}

	remapNodes(data, nodes, node_offset);

	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		NodeInfo& ni = nodes[i];

		if (!ni.keep)
			continue;

		const cgltf_node& node = data->nodes[i];

		if (!node.parent)
		{
			comma(json_roots);
			json_roots += to_string(size_t(ni.remap));
		}

		comma(json_nodes);
		json_nodes += "{";
		if (node.name && ni.named)
		{
			comma(json_nodes);
			json_nodes += "\"name\":\"";
			json_nodes += node.name;
			json_nodes += "\"";
		}
		if (node.has_translation)
		{
			comma(json_nodes);
			json_nodes += "\"translation\":[";
			json_nodes += to_string(node.translation[0]);
			json_nodes += ",";
			json_nodes += to_string(node.translation[1]);
			json_nodes += ",";
			json_nodes += to_string(node.translation[2]);
			json_nodes += "]";
		}
		if (node.has_rotation)
		{
			comma(json_nodes);
			json_nodes += "\"rotation\":[";
			json_nodes += to_string(node.rotation[0]);
			json_nodes += ",";
			json_nodes += to_string(node.rotation[1]);
			json_nodes += ",";
			json_nodes += to_string(node.rotation[2]);
			json_nodes += ",";
			json_nodes += to_string(node.rotation[3]);
			json_nodes += "]";
		}
		if (node.has_scale)
		{
			comma(json_nodes);
			json_nodes += "\"scale\":[";
			json_nodes += to_string(node.scale[0]);
			json_nodes += ",";
			json_nodes += to_string(node.scale[1]);
			json_nodes += ",";
			json_nodes += to_string(node.scale[2]);
			json_nodes += "]";
		}
		if (node.has_matrix)
		{
			comma(json_nodes);
			json_nodes += "\"matrix\":[";
			for (int k = 0; k < 16; ++k)
			{
				comma(json_nodes);
				json_nodes += to_string(node.matrix[k]);
			}
			json_nodes += "]";
		}
		if (node.children_count || node_mesh[i] >= 0)
		{
			comma(json_nodes);
			json_nodes += "\"children\":[";
			for (size_t j = 0; j < node.children_count; ++j)
			{
				NodeInfo& ci = nodes[node.children[j] - data->nodes];

				if (ci.keep)
				{
					comma(json_nodes);
					json_nodes += to_string(size_t(ci.remap));
				}
			}
			if (node_mesh[i] >= 0)
			{
				comma(json_nodes);
				json_nodes += to_string(size_t(node_mesh[i]));
			}
			json_nodes += "]";
		}
		json_nodes += "}";
	}

	for (size_t i = 0; i < data->skins_count; ++i)
	{
		const cgltf_skin& skin = data->skins[i];

		scratch.clear();

		for (size_t j = 0; j < skin.joints_count; ++j)
		{
			float transform[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

			if (skin.inverse_bind_matrices)
			{
				cgltf_accessor_read_float(skin.inverse_bind_matrices, j, transform, 16);
			}

			float node_scale = qp.pos_scale / float((1 << qp.pos_bits) - 1);

			// pos_offset has to be applied first, thus it results in an offset rotated by the bind matrix
			transform[12] += qp.pos_offset[0] * transform[0] + qp.pos_offset[1] * transform[4] + qp.pos_offset[2] * transform[8];
			transform[13] += qp.pos_offset[0] * transform[1] + qp.pos_offset[1] * transform[5] + qp.pos_offset[2] * transform[9];
			transform[14] += qp.pos_offset[0] * transform[2] + qp.pos_offset[1] * transform[6] + qp.pos_offset[2] * transform[10];

			// node_scale will be applied before the rotation/scale from transform
			for (int k = 0; k < 12; ++k)
				transform[k] *= node_scale;

			scratch.append(reinterpret_cast<const char*>(transform), sizeof(transform));
		}

		size_t view = getBufferView(views, BufferView::Kind_Skin, 0, 64, settings.compress);
		size_t offset = views[view].data.size();
		views[view].data += scratch;

		comma(json_accessors);
		writeAccessor(json_accessors, view, offset, cgltf_type_mat4, cgltf_component_type_r_32f, false, skin.joints_count);

		size_t matrix_accr = accr_offset++;

		comma(json_skins);
		json_skins += "{";
		json_skins += "\"joints\":[";
		for (size_t j = 0; j < skin.joints_count; ++j)
		{
			comma(json_skins);
			json_skins += to_string(size_t(nodes[skin.joints[j] - data->nodes].remap));
		}
		json_skins += "]";
		json_skins += ",\"inverseBindMatrices\":";
		json_skins += to_string(matrix_accr);
		if (skin.skeleton)
		{
			comma(json_skins);
			json_skins += "\"skeleton\":";
			json_skins += to_string(size_t(nodes[skin.skeleton - data->nodes].remap));
		}
		json_skins += "}";
	}

	for (size_t i = 0; i < data->animations_count; ++i)
	{
		const cgltf_animation& animation = data->animations[i];

		eastl::string json_samplers;
		eastl::string json_channels;

		eastl::vector<const cgltf_animation_channel*> tracks;

		for (size_t j = 0; j < animation.channels_count; ++j)
		{
			const cgltf_animation_channel& channel = animation.channels[j];

			if (!channel.target_node)
				continue;

			NodeInfo& ni = nodes[channel.target_node - data->nodes];

			if (!ni.keep)
				continue;

			if (channel.target_path != cgltf_animation_path_type_translation && channel.target_path != cgltf_animation_path_type_rotation && channel.target_path != cgltf_animation_path_type_scale)
				continue;

			if (!settings.anim_const && (ni.animated_paths & (1 << channel.target_path)) == 0)
				continue;

			tracks.push_back(&channel);
		}

		if (tracks.empty())
			continue;

		float mint = 0, maxt = 0;
		bool needs_time = false;
		bool needs_pose = false;

		for (size_t j = 0; j < tracks.size(); ++j)
		{
			const cgltf_animation_channel& channel = *tracks[j];
			const cgltf_animation_sampler& sampler = *channel.sampler;

			mint = eastl::min(mint, sampler.input->min[0]);
			maxt = eastl::max(maxt, sampler.input->max[0]);

			bool tc = isTrackConstant(sampler, channel.target_path);

			needs_time = needs_time || !tc;
			needs_pose = needs_pose || tc;
		}

		// round the number of frames to nearest but favor the "up" direction
		// this means that at 10 Hz resampling, we will try to preserve the last frame <10ms
		// but if the last frame is <2ms we favor just removing this data
		int frames = 1 + int((maxt - mint) * settings.anim_freq + 0.8f);

		size_t time_accr = 0;

		if (needs_time)
		{
			eastl::vector<float> time(frames);

			for (int j = 0; j < frames; ++j)
				time[j] = mint + float(j) / settings.anim_freq;

			scratch.clear();
			StreamFormat format = writeTimeStream(scratch, time);

			size_t view = getBufferView(views, BufferView::Kind_Time, 0, format.stride, settings.compress);
			size_t offset = views[view].data.size();
			views[view].data += scratch;

			comma(json_accessors);
			writeAccessor(json_accessors, view, offset, cgltf_type_scalar, format.component_type, format.normalized, frames, &time.front(), &time.back(), 1);

			time_accr = accr_offset++;
		}

		size_t pose_accr = 0;

		if (needs_pose)
		{
			eastl::vector<float> pose(1, mint);

			scratch.clear();
			StreamFormat format = writeTimeStream(scratch, pose);

			size_t view = getBufferView(views, BufferView::Kind_Time, 0, format.stride, settings.compress);
			size_t offset = views[view].data.size();
			views[view].data += scratch;

			comma(json_accessors);
			writeAccessor(json_accessors, view, offset, cgltf_type_scalar, format.component_type, format.normalized, 1, &pose.front(), &pose.back(), 1);

			pose_accr = accr_offset++;
		}

		size_t track_offset = 0;

		for (size_t j = 0; j < tracks.size(); ++j)
		{
			const cgltf_animation_channel& channel = *tracks[j];
			const cgltf_animation_sampler& sampler = *channel.sampler;

			bool tc = isTrackConstant(sampler, channel.target_path);

			eastl::vector<Attr> track;
			if (tc)
			{
				Attr pose = {};
				cgltf_accessor_read_float(sampler.output, 0, pose.f, 4);
				track.push_back(pose);
			}
			else
			{
				resampleKeyframes(track, sampler, channel.target_path, frames, mint, settings.anim_freq);
			}

			scratch.clear();
			StreamFormat format = writeKeyframeStream(scratch, channel.target_path, track);

			size_t view = getBufferView(views, BufferView::Kind_Keyframe, channel.target_path, format.stride, settings.compress);
			size_t offset = views[view].data.size();
			views[view].data += scratch;

			comma(json_accessors);
			writeAccessor(json_accessors, view, offset, format.type, format.component_type, format.normalized, track.size());

			size_t data_accr = accr_offset++;

			comma(json_samplers);
			json_samplers += "{\"input\":";
			json_samplers += to_string(tc ? pose_accr : time_accr);
			json_samplers += ",\"output\":";
			json_samplers += to_string(data_accr);
			json_samplers += "}";

			comma(json_channels);
			json_channels += "{\"sampler\":";
			json_channels += to_string(track_offset);
			json_channels += ",\"target\":{\"node\":";
			json_channels += to_string(size_t(nodes[channel.target_node - data->nodes].remap));
			json_channels += ",\"path\":";
			json_channels += animationPath(channel.target_path);
			json_channels += "}}";

			track_offset++;
		}

		comma(json_animations);
		json_animations += "{";
		if (animation.name)
		{
			json_animations += "\"name\":\"";
			json_animations += animation.name;
			json_animations += "\",";
		}
		json_animations += "\"samplers\":[";
		json_animations += json_samplers;
		json_animations += "],\"channels\":[";
		json_animations += json_channels;
		json_animations += "]}";
	}

	json += "\"asset\":{\"version\":\"2.0\", \"generator\":\"gltfpack\"}";
	json += ",\"extensionsUsed\":[";
	json += "\"KHR_quantized_geometry\"";
	if (settings.compress)
	{
		comma(json);
		json += "\"KHR_meshopt_compression\"";
	}
	if (!json_textures.empty())
	{
		comma(json);
		json += "\"KHR_texture_transform\"";
	}
	if (has_pbr_specular_glossiness)
	{
		comma(json);
		json += "\"KHR_materials_pbrSpecularGlossiness\"";
	}
	json += "]";

	size_t bytes[BufferView::Kind_Count] = {};
	size_t bytes_raw[BufferView::Kind_Count] = {};

	if (!views.empty())
	{
		json += ",\"bufferViews\":[";
		for (size_t i = 0; i < views.size(); ++i)
		{
			BufferView& view = views[i];

			size_t offset = bin.size();
			bin += view.data;
			bin.resize((bin.size() + 3) & ~3);

			size_t count = view.data.size() / view.stride;

			if (view.compressed)
			{
				if (view.kind == BufferView::Kind_Index)
					compressIndexStream(bin, offset, count, view.stride);
				else
					compressVertexStream(bin, offset, count, view.stride);
			}

			comma(json);
			writeBufferView(json, view.kind, count, view.stride, offset, bin.size() - offset, view.compressed);

			bytes[view.kind] += bin.size() - offset;
			bytes_raw[view.kind] += view.data.size();
		}
		json += "]";
	}
	if (!json_accessors.empty())
	{
		json += ",\"accessors\":[";
		json += json_accessors;
		json += "]";
	}
	if (!json_images.empty())
	{
		json += ",\"images\":[";
		json += json_images;
		json += "]";
	}
	if (!json_textures.empty())
	{
		json += ",\"textures\":[";
		json += json_textures;
		json += "]";
	}
	if (!json_materials.empty())
	{
		json += ",\"materials\":[";
		json += json_materials;
		json += "]";
	}
	if (!json_meshes.empty())
	{
		json += ",\"meshes\":[";
		json += json_meshes;
		json += "]";
	}
	if (!json_skins.empty())
	{
		json += ",\"skins\":[";
		json += json_skins;
		json += "]";
	}
	if (!json_animations.empty())
	{
		json += ",\"animations\":[";
		json += json_animations;
		json += "]";
	}
	if (!json_roots.empty())
	{
		json += ",\"nodes\":[";
		json += json_nodes;
		json += "],\"scenes\":[";
		json += "{\"nodes\":[";
		json += json_roots;
		json += "]}";
		json += "],\"scene\":0";
	}

	if (settings.verbose)
	{
		printf("output: %d nodes, %d meshes\n", int(node_offset), int(mesh_offset));
		printf("output: JSON %d bytes, buffers %d bytes\n", int(json.size()), int(bin.size()));
		printf("output: buffers: vertex %d bytes, index %d bytes, skin %d bytes, time %d bytes, keyframe %d bytes, image %d bytes\n",
			int(bytes[BufferView::Kind_Vertex]), int(bytes[BufferView::Kind_Index]), int(bytes[BufferView::Kind_Skin]),
			int(bytes[BufferView::Kind_Time]), int(bytes[BufferView::Kind_Keyframe]), int(bytes[BufferView::Kind_Image]));
	}

	return true;
}

void writeU32(FILE* out, uint32_t data)
{
	fwrite(&data, 4, 1, out);
}

void quantizeMaterial(cgltf_material* meshMaterial)
{
	if (meshMaterial)
	{
		const int colorBits = 8;
		uint8_t v[4] = {
			uint8_t(meshopt_quantizeUnorm(meshMaterial->pbr_metallic_roughness.base_color_factor[0], colorBits)),
			uint8_t(meshopt_quantizeUnorm(meshMaterial->pbr_metallic_roughness.base_color_factor[1], colorBits)),
			uint8_t(meshopt_quantizeUnorm(meshMaterial->pbr_metallic_roughness.base_color_factor[2], colorBits)),
			uint8_t(meshopt_quantizeUnorm(meshMaterial->pbr_metallic_roughness.base_color_factor[3], colorBits)) };

		meshMaterial->pbr_metallic_roughness.base_color_factor[0] = (float)v[0];
		meshMaterial->pbr_metallic_roughness.base_color_factor[1] = (float)v[1];
		meshMaterial->pbr_metallic_roughness.base_color_factor[2] = (float)v[2];
		meshMaterial->pbr_metallic_roughness.base_color_factor[3] = (float)v[3];

		const int materialBits = 16;
		uint16_t m[3] = {
			uint16_t(meshopt_quantizeUnorm(meshMaterial->pbr_metallic_roughness.metallic_factor, materialBits)),
			uint16_t(meshopt_quantizeUnorm(meshMaterial->pbr_metallic_roughness.roughness_factor, materialBits)),
			uint16_t(meshopt_quantizeUnorm(meshMaterial->alpha_cutoff, materialBits)) };

		meshMaterial->pbr_metallic_roughness.metallic_factor  = (float)m[0];
		meshMaterial->pbr_metallic_roughness.roughness_factor = (float)m[1];
		meshMaterial->alpha_cutoff = (float)m[2];
	}
}

void quantizeMesh(Mesh& mesh, Settings settings, QuantizationParams params)
{
	for (size_t j = 0; j < mesh.streams.size(); ++j)
	{
		Stream & stream = mesh.streams[j];
		if (stream.type == cgltf_attribute_type_position)
		{
			float pos_rscale = params.pos_scale == 0.f ? 0.f : 1.f / params.pos_scale;

			for (size_t i = 0; i < stream.data.size(); ++i)
			{
				Attr& a = stream.data[i];

				uint16_t v[4] = {
					uint16_t(meshopt_quantizeUnorm((a.f[0] - params.pos_offset[0]) * pos_rscale, params.pos_bits)),
					uint16_t(meshopt_quantizeUnorm((a.f[1] - params.pos_offset[1]) * pos_rscale, params.pos_bits)),
					uint16_t(meshopt_quantizeUnorm((a.f[2] - params.pos_offset[2]) * pos_rscale, params.pos_bits)),
					1 };

				a.f[0] = (float)v[0];
				a.f[1] = (float)v[1];
				a.f[2] = (float)v[2];
				a.f[3] = (float)v[3];
			}
		}
		else if (stream.type == cgltf_attribute_type_texcoord)
		{
			float uv_rscale[2] = {
				params.uv_scale[0] == 0.f ? 0.f : 1.f / params.uv_scale[0],
				params.uv_scale[1] == 0.f ? 0.f : 1.f / params.uv_scale[1],
			};

			for (size_t i = 0; i < stream.data.size(); ++i)
			{
				Attr& a = stream.data[i];

				uint16_t v[2] = {
					uint16_t(meshopt_quantizeUnorm((a.f[0] - params.uv_offset[0]) * uv_rscale[0], params.uv_bits)),
					uint16_t(meshopt_quantizeUnorm((a.f[1] - params.uv_offset[1]) * uv_rscale[1], params.uv_bits)),
				};

				a.f[0] = (float)v[0];
				a.f[1] = (float)v[1];
			}
		}
		else if (stream.type == cgltf_attribute_type_normal)
		{
			int bits = settings.nrm_unit ? 8 : settings.nrm_bits;

			for (size_t i = 0; i < stream.data.size(); ++i)
			{
				Attr& a = stream.data[i];

				float nx = a.f[0], ny = a.f[1], nz = a.f[2];

				if (!settings.nrm_unit)
					rescaleNormal(nx, ny, nz);

				int8_t v[4] = {
					int8_t(meshopt_quantizeSnorm(nx, bits)),
					int8_t(meshopt_quantizeSnorm(ny, bits)),
					int8_t(meshopt_quantizeSnorm(nz, bits)),
					0 };

				a.f[0] = (float)v[0];
				a.f[1] = (float)v[1];
				a.f[2] = (float)v[2];
				a.f[3] = (float)v[3];
			}
		}
		else if (stream.type == cgltf_attribute_type_tangent)
		{
			int bits = settings.nrm_unit ? 8 : settings.nrm_bits;

			for (size_t i = 0; i < stream.data.size(); ++i)
			{
				Attr& a = stream.data[i];

				float nx = a.f[0], ny = a.f[1], nz = a.f[2], nw = a.f[3];

				if (!settings.nrm_unit)
					rescaleNormal(nx, ny, nz);

				int8_t v[4] = {
					int8_t(meshopt_quantizeSnorm(nx, bits)),
					int8_t(meshopt_quantizeSnorm(ny, bits)),
					int8_t(meshopt_quantizeSnorm(nz, bits)),
					int8_t(meshopt_quantizeSnorm(nw, 8)) };

				a.f[0] = (float)v[0];
				a.f[1] = (float)v[1];
				a.f[2] = (float)v[2];
				a.f[3] = (float)v[3];
			}
		}
		else if (stream.type == cgltf_attribute_type_color)
		{
			for (size_t i = 0; i < stream.data.size(); ++i)
			{
				Attr& a = stream.data[i];

				uint8_t v[4] = {
					uint8_t(meshopt_quantizeUnorm(a.f[0], 8)),
					uint8_t(meshopt_quantizeUnorm(a.f[1], 8)),
					uint8_t(meshopt_quantizeUnorm(a.f[2], 8)),
					uint8_t(meshopt_quantizeUnorm(a.f[3], 8)) };

				a.f[0] = (float)v[0];
				a.f[1] = (float)v[1];
				a.f[2] = (float)v[2];
				a.f[3] = (float)v[3];
			}
		}
		else if (stream.type == cgltf_attribute_type_weights)
		{
			for (size_t i = 0; i < stream.data.size(); ++i)
			{
				Attr& a = stream.data[i];

				uint8_t v[4] = {
					uint8_t(meshopt_quantizeUnorm(a.f[0], 8)),
					uint8_t(meshopt_quantizeUnorm(a.f[1], 8)),
					uint8_t(meshopt_quantizeUnorm(a.f[2], 8)),
					uint8_t(meshopt_quantizeUnorm(a.f[3], 8)) };

				renormalizeWeights(v);

				a.f[0] = (float)v[0];
				a.f[1] = (float)v[1];
				a.f[2] = (float)v[2];
				a.f[3] = (float)v[3];
			}
		}
		else if (stream.type == cgltf_attribute_type_joints)
		{
			unsigned int maxj = 0;

			for (size_t i = 0; i < stream.data.size(); ++i)
				maxj = eastl::max(maxj, unsigned(stream.data[i].f[0]));

			ASSERT(maxj <= 65535);

			if (maxj <= 255)
			{
				for (size_t i = 0; i < stream.data.size(); ++i)
				{
					Attr& a = stream.data[i];

					uint8_t v[4] = {
						uint8_t(a.f[0]),
						uint8_t(a.f[1]),
						uint8_t(a.f[2]),
						uint8_t(a.f[3]) };

					a.f[0] = (float)v[0];
					a.f[1] = (float)v[1];
					a.f[2] = (float)v[2];
					a.f[3] = (float)v[3];
				}
			}
			else
			{
				for (size_t i = 0; i < stream.data.size(); ++i)
				{
					Attr& a = stream.data[i];

					uint16_t v[4] = {
						uint16_t(a.f[0]),
						uint16_t(a.f[1]),
						uint16_t(a.f[2]),
						uint16_t(a.f[3]) };

					a.f[0] = (float)v[0];
					a.f[1] = (float)v[1];
					a.f[2] = (float)v[2];
					a.f[3] = (float)v[3];
				}
			}
		}
	}
}

int gltfpack(int argc, const char** argv)
{
	Settings settings = {};
	settings.pos_bits = 16;// 14;
	settings.tex_bits = 16;// 12;
	settings.nrm_bits = 8;
	settings.anim_freq = 30;

	const char* input = 0;
	const char* output = 0;
	bool help = false;

	for (int i = 1; i < argc; ++i)
	{
		const char* arg = argv[i];

		if (strcmp(arg, "-vp") == 0 && i + 1 < argc && isdigit(argv[i + 1][0]))
		{
			settings.pos_bits = atoi(argv[++i]);
		}
		else if (strcmp(arg, "-vt") == 0 && i + 1 < argc && isdigit(argv[i + 1][0]))
		{
			settings.tex_bits = atoi(argv[++i]);
		}
		else if (strcmp(arg, "-vn") == 0 && i + 1 < argc && isdigit(argv[i + 1][0]))
		{
			settings.nrm_bits = atoi(argv[++i]);
		}
		else if (strcmp(arg, "-vu") == 0)
		{
			settings.nrm_unit = true;
		}
		else if (strcmp(arg, "-af") == 0 && i + 1 < argc && isdigit(argv[i + 1][0]))
		{
			settings.anim_freq = atoi(argv[++i]);
		}
		else if (strcmp(arg, "-ac") == 0)
		{
			settings.anim_const = true;
		}
		else if (strcmp(arg, "-i") == 0 && i + 1 < argc && !input)
		{
			input = argv[++i];
		}
		else if (strcmp(arg, "-o") == 0 && i + 1 < argc && !output)
		{
			output = argv[++i];
		}
		else if (strcmp(arg, "-c") == 0)
		{
			settings.compress = true;
		}
		else if (strcmp(arg, "-v") == 0)
		{
			settings.verbose = true;
		}
		else if (strcmp(arg, "-h") == 0)
		{
			help = true;
		}
		else
		{
			fprintf(stderr, "Unrecognized option %s\n", arg);
			return 1;
		}
	}

	if (!input || !output || help)
	{
		fprintf(stderr, "Usage: gltfpack [options] -i input -o output\n");
		fprintf(stderr, "\n");
		fprintf(stderr, "Options:\n");
		fprintf(stderr, "-i file: input file to process, .obj/.gltf/.glb\n");
		fprintf(stderr, "-o file: output file path, .gltf/.glb\n");
		fprintf(stderr, "-vp N: use N-bit quantization for positions (default: 14; N should be between 1 and 16)\n");
		fprintf(stderr, "-vt N: use N-bit quantization for texture corodinates (default: 12; N should be between 1 and 16)\n");
		fprintf(stderr, "-vn N: use N-bit quantization for normals and tangents (default: 8; N should be between 1 and 8)\n");
		fprintf(stderr, "-vu: use unit-length normal/tangent vectors (default: off)\n");
		fprintf(stderr, "-af N: resample animations at N Hz (default: 30)\n");
		fprintf(stderr, "-ac: keep constant animation tracks even if they don't modify the node transform\n");
		fprintf(stderr, "-c: produce compressed glb files\n");
		fprintf(stderr, "-v: verbose output\n");
		fprintf(stderr, "-h: display this help and exit\n");

		return 1;
	}

	cgltf_data* data = 0;
	eastl::vector<Mesh> meshes;
    
    PathHandle workingDirectory = fsCopyWorkingDirectoryPath();
    PathHandle filePath = fsAppendPathComponent(workingDirectory, input);

    const char* iext = fsGetPathExtension(filePath).buffer;

	if (iext && (strcmp(iext, "gltf") == 0 || strcmp(iext, "GLTF") == 0 || strcmp(iext, "glb") == 0 || strcmp(iext, "GLB") == 0))
	{
        
		cgltf_options options = {};
		cgltf_result result = parse_gltf_file(&options, filePath, &data);
		result = (result == cgltf_result_success) ? cgltf_validate(data) : result;
		result = (result == cgltf_result_success) ? load_gltf_buffers(&options, data, filePath) : result;
        
		if (result != cgltf_result_success)
		{
			fprintf(stderr, "Error loading %s: %s\n", input, getError(result));
			cgltf_free(data);
			return 2;
		}

		parseMeshesGltf(data, meshes);
	}
	else
	{
		fprintf(stderr, "Error loading %s: unknown extension (expected .gltf or .glb)\n", input);
		return 2;
	}

	eastl::string json, bin;
	if (!process(data, meshes, settings, json, bin))
	{
		fprintf(stderr, "Error processing %s\n", input);
		cgltf_free(data);
		return 3;
	}

	cgltf_free(data);

	const char* oext = strrchr(output, '.');

	if (oext && (strcmp(oext, "gltf") == 0 || strcmp(oext, "GLTF") == 0))
	{
		eastl::string binpath = output;
		binpath.replace(binpath.size() - 5, 5, ".bin");

		eastl::string binname = binpath;
		eastl::string::size_type slash = binname.find_last_of("/\\");
		if (slash != eastl::string::npos)
			binname.erase(0, slash + 1);

        PathHandle outJSONPath = fsAppendPathComponent(workingDirectory, output);
        PathHandle outBinaryPath = fsAppendPathComponent(workingDirectory, output);

        FileStream* fhJSON = fsOpenFile(outJSONPath, FM_WRITE_BINARY);
        FileStream* fhBinary = fsOpenFile(outBinaryPath, FM_WRITE_BINARY);
		
		if (!fhJSON || !fhBinary)
		{
			fprintf(stderr, "Error saving %s\n", output);
			return 4;
		}

		const char * buffersURI = "{ \"buffers\":[{\"uri\":\"";
        fsWriteToStreamString(fhJSON, buffersURI);
        fsWriteToStreamString(fhBinary, buffersURI);

		const char * bytelength = "\",\"byteLength\":";
        fsWriteToStreamString(fhJSON, bytelength);
		eastl::string binSize = bin;
		memset(&binSize[0], 0, binSize.size());
		sprintf(&binSize[0], "%zu", bin.size());
        
        fsWriteToStreamString(fhJSON, binSize.c_str());
        fsWriteToStreamString(fhJSON, "}],");
        
        fsWriteToStream(fhJSON, json.c_str(), json.size());
        fsWriteToStreamString(fhJSON, "}");

        fsWriteToStream(fhBinary, bin.c_str(), bin.size());

        fsCloseStream(fhJSON);
        fsCloseStream(fhBinary);
	}
	else if (oext && (strcmp(oext, ".glb") == 0 || strcmp(oext, ".GLB") == 0))
	{
        PathHandle fhOutPath = fsAppendPathComponent(workingDirectory, output);

        FileStream* fhOut = fsOpenFile(fhOutPath, FM_WRITE_BINARY);
		if (!fhOut)
		{
			fprintf(stderr, "Error saving %s\n", output);
			return 4;
		}

		char bufferspec[64];
		sprintf(bufferspec, "{\"buffers\":[{\"byteLength\":%zu}],", bin.size());

		json.insert(0, bufferspec);
		json.push_back('}');

		while (json.size() % 4)
			json.push_back(' ');

		while (bin.size() % 4)
			bin.push_back('\0');

        fsWriteToStreamUInt32(fhOut, 0x46546C67);
        fsWriteToStreamUInt32(fhOut, 2);
        fsWriteToStreamUInt32(fhOut, uint32_t(12 + 8 + json.size() + 8 + bin.size()));
        
        fsWriteToStreamUInt32(fhOut, uint32_t(json.size()));
        fsWriteToStreamUInt32(fhOut, 0x4E4F534A);
        fsWriteToStream(fhOut, json.c_str(), json.size());
        
        fsWriteToStreamUInt32(fhOut, uint32_t(bin.size()));
        fsWriteToStreamUInt32(fhOut, 0x004E4942);
        fsWriteToStream(fhOut, bin.c_str(), bin.size());
        
        fsCloseStream(fhOut);
	}
	else
	{
		fprintf(stderr, "Error saving %s: unknown extension (expected .gltf or .glb)\n", output);
		return 4;
	}

	return 0;
}
